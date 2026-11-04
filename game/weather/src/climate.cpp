#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/weather/climate.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>
#include <string>
#include <utility>

namespace dwe::game::weather {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::array KindNames{std::string_view{"none"},       std::string_view{"clear"},
                               std::string_view{"cloudy"},     std::string_view{"light_rain"},
                               std::string_view{"heavy_rain"}, std::string_view{"light_snow"},
                               std::string_view{"heavy_snow"}};
static_assert(KindNames.size() == WeatherKindCount);

constexpr std::array SeasonNames{std::string_view{"spring"}, std::string_view{"summer"},
                                 std::string_view{"autumn"}, std::string_view{"winter"}};
static_assert(SeasonNames.size() == SeasonCount);

// The calendar's hours in a day (fixed, as GameClock's).
constexpr std::int64_t HoursPerDay = 24;
// Seasonal weights are in hundredths of the area's.
constexpr float SeasonalScale = 100.0F;

// How many periods back a kept weather is looked for: a run never lasts longer than this.
constexpr std::int64_t LongestRun = 8;
// The salts of the two rolls of a period.
constexpr std::uint64_t KeepSalt = 1;
constexpr std::uint64_t KindSalt = 2;

std::uint64_t splitmix(std::uint64_t value) {
  value += 0x9E3779B97F4A7C15ULL;
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

std::uint64_t fnv1a(std::string_view text) {
  std::uint64_t hash = 0xCBF29CE484222325ULL;
  for (const char c : text) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 0x100000001B3ULL;
  }
  return hash;
}

std::vector<std::string> readMaps(YamlReader& reader, const YamlNode& node, std::string_view what) {
  std::vector<std::string> maps;
  for (const YamlNode& map : reader.sequence(node, what)) {
    std::string text = reader.text(map);
    if (reader.ok() && (text.empty() || text == "*")) {
      reader.fail(map, "a map pattern needs an id or a prefix before *");
    }
    maps.push_back(std::move(text));
  }
  return maps;
}

WeatherWeights readWeights(YamlReader& reader, const YamlNode& node) {
  WeatherWeights weights{};
  if (!node.isMapping()) {
    reader.fail(node, "the weights are a mapping of weather to weight");
    return weights;
  }
  for (const core::YamlEntry& entry : node.entries()) {
    const auto kind = weatherKindNamed(entry.key);
    const auto* at = std::ranges::find(RolledKinds, kind.value_or(WeatherKind::None));
    if (!kind || at == RolledKinds.end()) {
      reader.fail(entry.value, std::format("{} is not a weather an area can have", entry.key));
      continue;
    }
    weights.at(static_cast<std::size_t>(at - RolledKinds.begin())) =
        reader.integer<std::uint32_t>(entry.value);
  }
  return weights;
}

float readUnit(YamlReader& reader, const YamlNode& node, std::string_view what) {
  const float value = reader.number(node);
  if (reader.ok() && (value < 0.0F || value > 1.0F)) {
    reader.fail(node, std::format("{} is from 0 to 1", what));
  }
  return value;
}

SeasonGrade readGrade(YamlReader& reader, const YamlNode& node) {
  SeasonGrade grade;
  if (!reader.expectKeys(node, "a season's grade", {},
                         {"foliage_hue", "foliage", "saturation", "gain", "lift"})) {
    return grade;
  }
  if (const YamlNode* hue = node.find("foliage_hue")) {
    grade.foliageHue = reader.number(*hue);
    if (reader.ok() && (grade.foliageHue < 0.0F || grade.foliageHue >= 360.0F)) {
      reader.fail(*hue, "a hue is from 0 to less than 360 degrees");
    }
  }
  if (const YamlNode* foliage = node.find("foliage")) {
    grade.foliage = readUnit(reader, *foliage, "the foliage's turn");
  }
  if (const YamlNode* saturation = node.find("saturation")) {
    grade.saturation = reader.number(*saturation);
    if (reader.ok() && (grade.saturation < 0.0F || grade.saturation > 2.0F)) {
      reader.fail(*saturation, "the saturation is from 0 to 2");
    }
  }
  if (const YamlNode* gain = node.find("gain")) {
    const auto channels = reader.sequence(*gain, "gain");
    if (reader.ok() && channels.size() != grade.gain.size()) {
      reader.fail(*gain, "the gain is red, green and blue");
    }
    for (std::size_t i = 0; reader.ok() && i < channels.size(); ++i) {
      grade.gain.at(i) = reader.number(channels[i]);
      if (reader.ok() && (grade.gain.at(i) < 0.0F || grade.gain.at(i) > 2.0F)) {
        reader.fail(channels[i], "a gain is from 0 to 2");
      }
    }
  }
  if (const YamlNode* lift = node.find("lift")) {
    grade.lift = reader.number(*lift);
    if (reader.ok() && (grade.lift < -0.5F || grade.lift > 0.5F)) {
      reader.fail(*lift, "the lift is from -0.5 to 0.5");
    }
  }
  return grade;
}

SeasonRules readSeason(YamlReader& reader, const YamlNode& node) {
  SeasonRules season;
  if (!reader.expectKeys(node, "a season", {},
                         {"odds", "rain_to_snow", "grade", "petals", "leaves"})) {
    return season;
  }
  if (const YamlNode* odds = node.find("odds")) {
    if (!odds->isMapping()) {
      reader.fail(*odds, "the odds are a mapping of weather to a factor");
    } else {
      for (const core::YamlEntry& entry : odds->entries()) {
        const auto kind = weatherKindNamed(entry.key);
        const auto* at = std::ranges::find(RolledKinds, kind.value_or(WeatherKind::None));
        if (!kind || at == RolledKinds.end()) {
          reader.fail(entry.value, std::format("{} is not a weather an area can have", entry.key));
          continue;
        }
        const float factor = reader.number(entry.value);
        if (reader.ok() && (factor < 0.0F || factor > 10.0F)) {
          reader.fail(entry.value, "an odds factor is from 0 to 10");
        }
        season.odds.at(static_cast<std::size_t>(at - RolledKinds.begin())) = factor;
      }
    }
  }
  if (const YamlNode* toSnow = node.find("rain_to_snow")) {
    season.rainToSnow = readUnit(reader, *toSnow, "the rain that falls as snow");
  }
  if (const YamlNode* grade = node.find("grade")) {
    season.grade = readGrade(reader, *grade);
  }
  if (const YamlNode* petals = node.find("petals")) {
    season.petals = readUnit(reader, *petals, "the petals");
  }
  if (const YamlNode* leaves = node.find("leaves")) {
    season.leaves = readUnit(reader, *leaves, "the leaves");
  }
  return season;
}

void readSeasons(YamlReader& reader, const YamlNode& node, ClimateRules& rules) {
  if (!reader.expectKeys(node, "the seasons", {"days", "spring", "summer", "autumn", "winter"})) {
    return;
  }
  rules.seasonDays = reader.integer<std::int32_t>(reader.field(node, "days"));
  if (reader.ok() && rules.seasonDays <= 0) {
    reader.fail(reader.field(node, "days"), "a season lasts at least one day");
  }
  for (std::size_t i = 0; i < SeasonCount; ++i) {
    rules.seasons.at(i) = readSeason(reader, reader.field(node, SeasonNames.at(i)));
  }
}

// Floor division, so a time before year 0 still falls in a turn.
std::int64_t floorDiv(std::int64_t value, std::int64_t by) {
  return value >= 0 ? value / by : -((-value + by - 1) / by);
}

} // namespace

std::string_view seasonName(Season season) {
  return SeasonNames.at(static_cast<std::size_t>(season));
}

std::optional<Season> seasonNamed(std::string_view name) {
  if (name == "fall") {
    return Season::Autumn;
  }
  for (std::size_t i = 0; i < SeasonNames.size(); ++i) {
    if (SeasonNames.at(i) == name) {
      return static_cast<Season>(i);
    }
  }
  return std::nullopt;
}

std::string_view weatherKindName(WeatherKind kind) {
  return KindNames.at(static_cast<std::size_t>(kind));
}

std::optional<WeatherKind> weatherKindNamed(std::string_view name) {
  std::string key{name};
  std::ranges::replace(key, '-', '_');
  if (key == "rain") {
    return WeatherKind::LightRain;
  }
  if (key == "snow") {
    return WeatherKind::LightSnow;
  }
  if (key == "sunny") {
    return WeatherKind::Clear;
  }
  for (std::size_t i = 0; i < KindNames.size(); ++i) {
    if (KindNames.at(i) == key) {
      return static_cast<WeatherKind>(i);
    }
  }
  return std::nullopt;
}

bool mapMatches(std::string_view pattern, std::string_view mapId) {
  if (pattern.ends_with('*')) {
    return mapId.starts_with(pattern.substr(0, pattern.size() - 1));
  }
  return pattern == mapId;
}

const ClimateArea* ClimateRules::areaOf(std::string_view mapId, bool fixedTimeOfDay) const {
  if (fixedTimeOfDay || mapId.empty()) {
    return nullptr;
  }
  if (std::ranges::any_of(noWeather,
                          [&](const std::string& pattern) { return mapMatches(pattern, mapId); })) {
    return nullptr;
  }
  for (const ClimateArea& area : areas) {
    if (std::ranges::any_of(
            area.maps, [&](const std::string& pattern) { return mapMatches(pattern, mapId); })) {
      return &area;
    }
  }
  return nullptr;
}

core::Result<ClimateRules> parseClimateRules(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{core::Error{std::format("field weather: {}", tree.error().message)}};
  }
  YamlReader reader;
  ClimateRules rules;
  if (!reader.expectKeys(*tree, "the field weather",
                         {"period_hours", "keep_chance", "fade_seconds", "areas"},
                         {"no_weather", "seasons"})) {
    return std::unexpected{reader.error("field weather")};
  }
  rules.periodHours = reader.integer<std::int32_t>(reader.field(*tree, "period_hours"));
  if (reader.ok() && rules.periodHours <= 0) {
    reader.fail(reader.field(*tree, "period_hours"), "the period is at least one hour");
  }
  rules.keepChance = reader.number(reader.field(*tree, "keep_chance"));
  if (reader.ok() && (rules.keepChance < 0.0F || rules.keepChance >= 1.0F)) {
    reader.fail(reader.field(*tree, "keep_chance"), "the chance is from 0 to less than 1");
  }
  rules.fadeSeconds = reader.number(reader.field(*tree, "fade_seconds"));
  if (reader.ok() && rules.fadeSeconds < 0.0F) {
    reader.fail(reader.field(*tree, "fade_seconds"), "the fade is not negative");
  }
  if (const YamlNode* none = tree->find("no_weather")) {
    rules.noWeather = readMaps(reader, *none, "no_weather");
  }
  for (const YamlNode& node : reader.sequence(reader.field(*tree, "areas"), "areas")) {
    if (!reader.expectKeys(node, "an area", {"id", "maps", "weights"},
                           {"season_strength", "snow_from_rain"})) {
      break;
    }
    ClimateArea area{.id = reader.text(reader.field(node, "id")),
                     .maps = readMaps(reader, reader.field(node, "maps"), "maps"),
                     .weights = readWeights(reader, reader.field(node, "weights"))};
    if (const YamlNode* strength = node.find("season_strength")) {
      area.seasonStrength = readUnit(reader, *strength, "the season's strength");
    }
    if (const YamlNode* snow = node.find("snow_from_rain")) {
      area.snowFromRain = reader.boolean(*snow);
    }
    if (reader.ok() && std::ranges::any_of(rules.areas, [&](const ClimateArea& other) {
          return other.id == area.id;
        })) {
      reader.fail(node, std::format("area {} is listed twice", area.id));
    }
    if (reader.ok() && std::reduce(area.weights.begin(), area.weights.end(), 0U) == 0U) {
      reader.fail(node, std::format("area {} has no weather with a weight", area.id));
    }
    rules.areas.push_back(std::move(area));
  }
  if (const YamlNode* seasons = tree->find("seasons")) {
    readSeasons(reader, *seasons, rules);
  }
  return reader.finish(std::move(rules), "field weather");
}

core::Result<ClimateRules> loadClimateRules(const assets::VirtualFileSystem& files) {
  auto id = assets::AssetId::parse("field/weather");
  if (!id) {
    return std::unexpected{id.error()};
  }
  auto bytes = files.read(*id);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  return parseClimateRules(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
}

std::int64_t periodOf(const ClimateRules& rules, std::int64_t gameHour) {
  return floorDiv(gameHour, std::max(rules.periodHours, 1));
}

Season seasonOfDay(const ClimateRules& rules, std::int64_t day) {
  if (!rules.hasSeasons()) {
    return Season::Spring;
  }
  const std::int64_t turn = floorDiv(day, rules.seasonDays);
  const auto count = static_cast<std::int64_t>(SeasonCount);
  return static_cast<Season>(((turn % count) + count) % count);
}

Season seasonOfHour(const ClimateRules& rules, std::int64_t gameHour) {
  return seasonOfDay(rules, floorDiv(gameHour, HoursPerDay));
}

WeatherWeights seasonalWeights(const ClimateRules& rules, const ClimateArea& area, Season season) {
  const SeasonRules& of = rules.season(season);
  const float strength = std::clamp(area.seasonStrength, 0.0F, 1.0F);
  std::array<float, RolledKinds.size()> weights{};
  for (std::size_t i = 0; i < weights.size(); ++i) {
    const float factor = 1.0F + (strength * (of.odds.at(i) - 1.0F));
    weights.at(i) = static_cast<float>(area.weights.at(i)) * std::max(factor, 0.0F);
  }
  if (area.snowFromRain) {
    // Light rain falls as light snow and heavy as heavy: RolledKinds keeps them two apart.
    const float toSnow = std::clamp(of.rainToSnow * strength, 0.0F, 1.0F);
    for (const auto [rain, snow] : {std::pair{WeatherKind::LightRain, WeatherKind::LightSnow},
                                    std::pair{WeatherKind::HeavyRain, WeatherKind::HeavySnow}}) {
      const auto r =
          static_cast<std::size_t>(std::ranges::find(RolledKinds, rain) - RolledKinds.begin());
      const auto n =
          static_cast<std::size_t>(std::ranges::find(RolledKinds, snow) - RolledKinds.begin());
      const float moved = weights.at(r) * toSnow;
      weights.at(r) -= moved;
      weights.at(n) += moved;
    }
  }
  WeatherWeights out{};
  for (std::size_t i = 0; i < out.size(); ++i) {
    out.at(i) = static_cast<std::uint32_t>(std::lround(weights.at(i) * SeasonalScale));
  }
  if (std::reduce(out.begin(), out.end(), 0U) == 0U) {
    return area.weights;
  }
  return out;
}

SeasonGrade seasonalGrade(const ClimateRules& rules, const ClimateArea& area, Season season) {
  const SeasonGrade& of = rules.season(season).grade;
  const float strength = std::clamp(area.seasonStrength, 0.0F, 1.0F);
  const auto toward = [&](float neutral, float value) {
    return neutral + (strength * (value - neutral));
  };
  SeasonGrade grade{.foliageHue = of.foliageHue,
                    .foliage = toward(0.0F, of.foliage),
                    .saturation = toward(1.0F, of.saturation),
                    .lift = toward(0.0F, of.lift)};
  for (std::size_t i = 0; i < grade.gain.size(); ++i) {
    grade.gain.at(i) = toward(1.0F, of.gain.at(i));
  }
  return grade;
}

std::uint64_t weatherRoll(std::uint64_t seed, std::string_view area, std::int64_t period,
                          std::uint64_t salt) {
  std::uint64_t value = splitmix(seed);
  value = splitmix(value ^ fnv1a(area));
  value = splitmix(value ^ static_cast<std::uint64_t>(period));
  return splitmix(value ^ salt);
}

WeatherKind weatherAt(const ClimateRules& rules, const ClimateArea& area, std::uint64_t seed,
                      std::int64_t period, bool seasonal) {
  const std::uint32_t total = std::reduce(area.weights.begin(), area.weights.end(), 0U);
  if (total == 0U) {
    return WeatherKind::None;
  }
  // The period whose roll set the weather: the latest one, from this one back, that did not keep
  // the weather before it.
  std::int64_t from = period - LongestRun;
  for (std::int64_t at = period; at > period - LongestRun; --at) {
    const std::uint64_t keep = weatherRoll(seed, area.id, at, KeepSalt);
    // The top 24 bits as a fraction of 1.
    const float chance = static_cast<float>(keep >> 40U) / static_cast<float>(1U << 24U);
    if (chance >= rules.keepChance) {
      from = at;
      break;
    }
  }
  // With seasons, the kind comes from the weights of the season the rolling period starts in, so a
  // weather rolled late in one season may last into the next.
  const WeatherWeights weights =
      seasonal && rules.hasSeasons()
          ? seasonalWeights(
                rules, area,
                seasonOfHour(rules, from * std::int64_t{std::max(rules.periodHours, 1)}))
          : area.weights;
  const std::uint32_t sum = std::reduce(weights.begin(), weights.end(), 0U);
  std::uint32_t pick = static_cast<std::uint32_t>(weatherRoll(seed, area.id, from, KindSalt) % sum);
  for (std::size_t i = 0; i < RolledKinds.size(); ++i) {
    if (pick < weights.at(i)) {
      return RolledKinds.at(i);
    }
    pick -= weights.at(i);
  }
  return RolledKinds.back();
}

std::uint64_t weatherSeedOf(std::uint64_t gameSeed) {
  // A fixed salt, so the weather seed is not the game's seed itself.
  return splitmix(gameSeed ^ 0x5745415448455221ULL);
}

} // namespace dwe::game::weather
