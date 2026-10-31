#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/inventory/inventory.hpp>
#include <dwe/game/story/story_state.hpp>
#include <dwe/session/bird_transport.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/story_adapter.hpp>

#include <format>
#include <limits>
#include <utility>

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view LinesSectionKey = "lines_section";
constexpr std::string_view LinesKey = "lines";
constexpr std::string_view GreetingKey = "greeting";
constexpr std::string_view FarewellKey = "farewell";
constexpr std::string_view ConfirmKey = "confirm";
constexpr std::string_view ChoicesKey = "choices";
constexpr std::string_view BitsKey = "bits";
constexpr std::string_view BitsSectionKey = "bits_section";
constexpr std::string_view MapVariableKey = "map_variable";
constexpr std::string_view EntryVariableKey = "entry_variable";
constexpr std::string_view TravelSectionKey = "travel_section";
constexpr std::string_view ChoiceLockKey = "choice_lock_trigger";
constexpr std::string_view RefuseSoundKey = "refuse_sound";
constexpr std::string_view SlotKey = "slot";
constexpr std::string_view SoundKey = "sound";
constexpr std::string_view DestinationsKey = "destinations";
constexpr std::string_view MapKey = "map";
constexpr std::string_view EntryKey = "entry";
constexpr std::string_view TriggerKey = "trigger";
constexpr std::string_view CostKey = "cost";

constexpr std::int32_t ByteMost = 0xFF;
constexpr std::int32_t TriggerMost = 799;
constexpr std::int32_t SectionMost = 0xFFFF;
// calculateBirdramonMenuStrings writes the price in four digits.
constexpr std::int32_t CostMost = 9999;
// birdraFillTargetList keeps a destination's index in seven bits.
constexpr std::size_t DestinationsMost = 0x7F;

std::int32_t inRange(YamlReader& reader, const YamlNode& parent, std::string_view key,
                     std::int32_t most = std::numeric_limits<std::int32_t>::max()) {
  const YamlNode& node = reader.field(parent, key);
  const auto value = reader.integer<std::int32_t>(node);
  if (reader.ok() && (value < 0 || value > most)) {
    reader.fail(node, std::format("{} must be from 0 to {}", key, most));
  }
  return value;
}

core::Result<void> checkBirdTransportSection(const ruleset::Section& section) {
  if (auto read = readBirdTransportRules(section); !read) {
    return std::unexpected{read.error()};
  }
  return {};
}

void setVariable(game::story::StoryState& story, std::int32_t variable, std::int32_t value) {
  if (auto set = story.setVariable(variable, static_cast<std::uint8_t>(value)); !set) {
    core::logWarning("[port:bird] tickBirdraTransport: {}", set.error().message);
  }
}

} // namespace

core::Result<BirdTransportRules> readBirdTransportRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, BirdTransportSection,
                         {LinesSectionKey, BitsSectionKey, LinesKey, MapVariableKey,
                          EntryVariableKey, TravelSectionKey, ChoiceLockKey, RefuseSoundKey,
                          DestinationsKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  BirdTransportRules rules{.linesSection = inRange(reader, root, LinesSectionKey),
                           .bitsSection = inRange(reader, root, BitsSectionKey),
                           .lines = {},
                           .mapVariable = inRange(reader, root, MapVariableKey, ByteMost),
                           .entryVariable = inRange(reader, root, EntryVariableKey, ByteMost),
                           .travelSection = inRange(reader, root, TravelSectionKey, SectionMost),
                           .choiceLockTrigger = inRange(reader, root, ChoiceLockKey, TriggerMost),
                           .refuseSound = {},
                           .destinations = {}};
  const YamlNode& lines = reader.field(root, LinesKey);
  if (reader.expectKeys(lines, LinesKey,
                        {GreetingKey, FarewellKey, ConfirmKey, ChoicesKey, BitsKey})) {
    rules.lines = {.greeting = inRange(reader, lines, GreetingKey),
                   .farewell = inRange(reader, lines, FarewellKey),
                   .confirm = inRange(reader, lines, ConfirmKey),
                   .choices = inRange(reader, lines, ChoicesKey),
                   .bits = inRange(reader, lines, BitsKey)};
  }
  const YamlNode& sound = reader.field(root, RefuseSoundKey);
  if (reader.expectKeys(sound, RefuseSoundKey, {SlotKey, SoundKey})) {
    rules.refuseSound = {.slot = inRange(reader, sound, SlotKey),
                         .sound = inRange(reader, sound, SoundKey)};
  }
  const YamlNode& destinations = reader.field(root, DestinationsKey);
  for (const YamlNode& place : reader.sequence(destinations, DestinationsKey)) {
    if (reader.expectKeys(place, DestinationsKey, {MapKey, EntryKey, TriggerKey, CostKey})) {
      rules.destinations.push_back({.map = inRange(reader, place, MapKey, ByteMost),
                                    .entry = inRange(reader, place, EntryKey, ByteMost),
                                    .trigger = inRange(reader, place, TriggerKey, TriggerMost),
                                    .cost = inRange(reader, place, CostKey, CostMost)});
    }
  }
  if (reader.ok() && rules.destinations.size() > DestinationsMost) {
    reader.fail(destinations,
                std::format("{} must have at most {} places", DestinationsKey, DestinationsMost));
  }
  return reader.finish(std::move(rules), section.file);
}

std::vector<BirdRow> birdRows(const BirdTransportRules& rules, const game::story::StoryState& story,
                              std::int32_t money) {
  std::vector<BirdRow> rows;
  for (std::size_t at = 0; at < rules.destinations.size(); ++at) {
    const BirdDestination& place = rules.destinations[at];
    if (!story.trigger(place.trigger).value_or(false)) {
      continue;
    }
    // The 0x80 flag: the cost is at most the money.
    rows.push_back({.destination = at, .affordable = place.cost <= money});
  }
  return rows;
}

BirdTransport::BirdTransport(BirdTransportDesc desc) : desc_(desc) {}

BirdTransport::~BirdTransport() = default;

ruleset::SectionChecks BirdTransport::sectionChecks() const {
  return {{std::string{BirdTransportSection}, checkBirdTransportSection}};
}

core::Result<void> BirdTransport::readRules(const ruleset::Ruleset& rules) {
  const ruleset::Section* section = rules.section(BirdTransportSection);
  if (section == nullptr) {
    rules_.reset();
    return {};
  }
  auto read = readBirdTransportRules(*section);
  if (!read) {
    return std::unexpected{read.error()};
  }
  rules_ = std::move(*read);
  return {};
}

core::Result<void> BirdTransport::startNewGame(GameSession& /*session*/) {
  close();
  return {};
}

const BirdTransportRules& BirdTransport::rules() const {
  DWE_ASSERT(rules_.has_value());
  return *rules_;
}

std::int32_t BirdTransport::money() const {
  return desc_.inventory != nullptr ? desc_.inventory->inventory().money() : 0;
}

bool BirdTransport::open() {
  if (!rules_ || desc_.inventory == nullptr || desc_.story == nullptr) {
    core::logDebug("[port:bird] tickBirdraTransport (shop/script_menu.c:987): no bird_transport "
                   "section, money or story; the routine closes at once");
    return false;
  }
  if (!lineTables_ && desc_.files != nullptr) {
    if (auto tables = readMapHeadLineTables(*desc_.files); tables) {
      lineTables_ = std::move(*tables);
    } else {
      core::logWarning("[port:bird] resolveMapHeadEntry: {}", tables.error().message);
    }
  }
  rows_ = birdRows(*rules_, desc_.story->state(), money());
  picked_.reset();
  open_ = true;
  core::logDebug("[port:bird] tickBirdraTransport (shop/script_menu.c:987): state 0, {} places",
                 rows_.size());
  return true;
}

void BirdTransport::close() {
  open_ = false;
  rows_.clear();
  picked_.reset();
}

bool BirdTransport::pick(std::size_t row) {
  if (row >= rows_.size() || !rows_[row].affordable) {
    return false;
  }
  picked_ = row;
  return true;
}

std::int32_t BirdTransport::travel() {
  DWE_ASSERT(picked_.has_value() && *picked_ < rows_.size());
  const BirdDestination& place = rules_->destinations[rows_[*picked_].destination];
  game::story::StoryState& story = desc_.story->state();
  setVariable(story, rules_->mapVariable, place.map);
  setVariable(story, rules_->entryVariable, place.entry);
  // MONEY -= SHOP_VARIABLE: the price the confirm kept.
  desc_.inventory->inventory().spendMoney(place.cost);
  core::logDebug("[port:bird] tickBirdraTransport (shop/script_menu.c:1021): state 5, map {} "
                 "entry {}, {} bits; MAPHEAD section {:#x}",
                 place.map, place.entry, place.cost, rules_->travelSection);
  return rules_->travelSection;
}

bool BirdTransport::choiceCancellable() const {
  return rules_ && desc_.story != nullptr &&
         !desc_.story->state().trigger(rules_->choiceLockTrigger).value_or(false);
}

std::string BirdTransport::lineKey(std::int32_t line) const {
  return rules_ ? sectionLine(rules_->linesSection, line) : std::string{};
}

std::string BirdTransport::bitsKey() const {
  return rules_ ? sectionLine(rules_->bitsSection, rules_->lines.bits) : std::string{};
}

std::string BirdTransport::sectionLine(std::int32_t section, std::int32_t line) const {
  if (!lineTables_) {
    return {};
  }
  const auto table = lineTables_->find(section);
  const auto at = static_cast<std::size_t>(line);
  if (table == lineTables_->end() || line < 0 || at >= table->second.size()) {
    return {};
  }
  return table->second[at];
}

} // namespace dwe::session
