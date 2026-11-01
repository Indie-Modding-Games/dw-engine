#include <dwe/assets/yaml_reader.hpp>
#include <dwe/game/curling/curling.hpp>

#include <algorithm>
#include <format>
#include <string_view>

namespace dwe::game::curling {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view SectionText = "the curling section";
constexpr std::string_view StoryKey = "story";
constexpr std::string_view OpponentVariableKey = "opponent_variable";
constexpr std::string_view ResultVariableKey = "result_variable";
constexpr std::string_view StoneTypesKey = "stone_types";
constexpr std::string_view WeightsKey = "weights";
constexpr std::string_view FrictionKey = "friction";
constexpr std::string_view MinMoveKey = "min_move";
constexpr std::string_view PowerKey = "power";
constexpr std::string_view StepKey = "step";
constexpr std::string_view LimitKey = "limit";
constexpr std::string_view ThrowBonusKey = "throw_bonus";
constexpr std::string_view OpponentPowerOffsetsKey = "opponent_power_offsets";
constexpr std::string_view RingsKey = "rings";
constexpr std::string_view XKey = "x";
constexpr std::string_view ZKey = "z";
constexpr std::string_view RadiusKey = "radius";
constexpr std::string_view PointsKey = "points";
constexpr std::string_view ObjectsKey = "objects";
constexpr std::string_view PerfectScoreKey = "perfect_score";
// The types a stone can have (KAR_D_8005AB8C holds 0 to 3; type 3 is pegged).
constexpr std::int32_t TypeCount = 4;

std::int32_t integer(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  return reader.integer<std::int32_t>(reader.field(parent, key));
}

template <std::size_t Size>
std::array<std::int32_t, Size> fixed(YamlReader& reader, const YamlNode& parent,
                                     std::string_view key) {
  std::array<std::int32_t, Size> values{};
  const auto items = reader.sequence(reader.field(parent, key), key, Size);
  for (std::size_t index = 0; index < items.size() && index < Size; ++index) {
    values.at(index) = reader.integer<std::int32_t>(items[index]);
  }
  return values;
}

void readRings(YamlReader& reader, const YamlNode& root, CurlingRules& rules) {
  for (const YamlNode& ring : reader.sequence(reader.field(root, RingsKey), RingsKey)) {
    if (!reader.expectKeys(ring, RingsKey, {XKey, ZKey, RadiusKey, PointsKey}, {ObjectsKey})) {
      continue;
    }
    Ring& read = rules.rings.emplace_back(Ring{.x = integer(reader, ring, XKey),
                                               .z = integer(reader, ring, ZKey),
                                               .radius = integer(reader, ring, RadiusKey),
                                               .points = integer(reader, ring, PointsKey),
                                               .objects = {}});
    if (const YamlNode* objects = ring.find(ObjectsKey); objects != nullptr) {
      for (const YamlNode& object : reader.sequence(*objects, ObjectsKey)) {
        read.objects.push_back(reader.integer<std::int32_t>(object));
      }
    }
  }
}

} // namespace

core::Result<CurlingRules> parseCurlingRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, SectionText,
                         {StoryKey, StoneTypesKey, WeightsKey, FrictionKey, MinMoveKey, PowerKey,
                          OpponentPowerOffsetsKey, RingsKey, PerfectScoreKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  CurlingRules rules;
  const YamlNode& story = reader.field(root, StoryKey);
  if (reader.expectKeys(story, StoryKey, {OpponentVariableKey, ResultVariableKey})) {
    rules.opponentVariable = integer(reader, story, OpponentVariableKey);
    rules.resultVariable = integer(reader, story, ResultVariableKey);
  }
  for (const YamlNode& type : reader.sequence(reader.field(root, StoneTypesKey), StoneTypesKey)) {
    rules.stoneTypes.push_back(reader.integer<std::int32_t>(type));
  }
  rules.weights = fixed<4>(reader, root, WeightsKey);
  const auto friction = fixed<2>(reader, root, FrictionKey);
  rules.frictionKeep = friction[0];
  rules.frictionOf = friction[1];
  rules.minMove = integer(reader, root, MinMoveKey);
  const YamlNode& power = reader.field(root, PowerKey);
  if (reader.expectKeys(power, PowerKey, {StepKey, LimitKey, ThrowBonusKey})) {
    rules.powerStep = integer(reader, power, StepKey);
    rules.powerLimit = integer(reader, power, LimitKey);
    rules.throwBonus = integer(reader, power, ThrowBonusKey);
  }
  rules.opponentPowerOffsets = fixed<2>(reader, root, OpponentPowerOffsetsKey);
  readRings(reader, root, rules);
  rules.perfectScore = integer(reader, root, PerfectScoreKey);
  if (!reader.ok()) {
    return std::unexpected{reader.error(section.file)};
  }
  if (rules.stoneTypes.empty() || std::ranges::any_of(rules.stoneTypes, [](std::int32_t type) {
        return type < 0 || type >= TypeCount;
      })) {
    return core::fail(std::format("{}: a stone type is not 0 to 3", section.file));
  }
  if (rules.frictionOf <= 0 || rules.powerStep <= 0 ||
      std::ranges::any_of(rules.weights, [](std::int32_t weight) { return weight <= 0; })) {
    return core::fail(std::format("{}: a weight or divisor is not positive", section.file));
  }
  return rules;
}

} // namespace dwe::game::curling
