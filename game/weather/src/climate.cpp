#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/weather/climate.hpp>

#include <algorithm>
#include <format>
#include <numeric>
#include <string>

namespace dwe::game::weather {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::array KindNames{std::string_view{"none"},       std::string_view{"clear"},
                               std::string_view{"cloudy"},     std::string_view{"light_rain"},
                               std::string_view{"heavy_rain"}, std::string_view{"light_snow"},
                               std::string_view{"heavy_snow"}};
static_assert(KindNames.size() == WeatherKindCount);

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

} // namespace

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
                         {"no_weather"})) {
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
    if (!reader.expectKeys(node, "an area", {"id", "maps", "weights"})) {
      break;
    }
    ClimateArea area{.id = reader.text(reader.field(node, "id")),
                     .maps = readMaps(reader, reader.field(node, "maps"), "maps"),
                     .weights = readWeights(reader, reader.field(node, "weights"))};
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
  const std::int64_t hours = std::max(rules.periodHours, 1);
  // Floor division, so a time before year 0 still falls in a period.
  return gameHour >= 0 ? gameHour / hours : -((-gameHour + hours - 1) / hours);
}

std::uint64_t weatherRoll(std::uint64_t seed, std::string_view area, std::int64_t period,
                          std::uint64_t salt) {
  std::uint64_t value = splitmix(seed);
  value = splitmix(value ^ fnv1a(area));
  value = splitmix(value ^ static_cast<std::uint64_t>(period));
  return splitmix(value ^ salt);
}

WeatherKind weatherAt(const ClimateRules& rules, const ClimateArea& area, std::uint64_t seed,
                      std::int64_t period) {
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
  std::uint32_t pick =
      static_cast<std::uint32_t>(weatherRoll(seed, area.id, from, KindSalt) % total);
  for (std::size_t i = 0; i < RolledKinds.size(); ++i) {
    if (pick < area.weights.at(i)) {
      return RolledKinds.at(i);
    }
    pick -= area.weights.at(i);
  }
  return RolledKinds.back();
}

std::uint64_t weatherSeedOf(std::uint64_t gameSeed) {
  // A fixed salt, so the weather seed is not the game's seed itself.
  return splitmix(gameSeed ^ 0x5745415448455221ULL);
}

} // namespace dwe::game::weather
