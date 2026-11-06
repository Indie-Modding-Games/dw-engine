#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/session/discard_menu.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/story_adapter.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <limits>
#include <utility>

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view ConfirmSectionKey = "confirm_section";
constexpr std::string_view ConfirmLineKey = "confirm_line";
constexpr std::string_view CancelTriggerKey = "cancel_trigger";
constexpr std::string_view ChosenVariableKey = "chosen_variable";
constexpr std::string_view NoItemKey = "no_item";

std::int32_t inRange(YamlReader& reader, const YamlNode& parent, std::string_view key,
                     std::int32_t most = std::numeric_limits<std::int32_t>::max()) {
  const YamlNode& node = reader.field(parent, key);
  const auto value = reader.integer<std::int32_t>(node);
  if (reader.ok() && (value < 0 || value > most)) {
    reader.fail(node, std::format("{} must be from 0 to {}", key, most));
  }
  return value;
}

core::Result<void> checkDiscardMenuSection(const ruleset::Section& section) {
  if (auto read = readDiscardMenuRules(section); !read) {
    return std::unexpected{read.error()};
  }
  return {};
}

} // namespace

core::Result<DiscardMenuRules> readDiscardMenuRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(
          root, DiscardMenuSection,
          {ConfirmSectionKey, ConfirmLineKey, CancelTriggerKey, ChosenVariableKey, NoItemKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  constexpr std::int32_t ByteMost = 0xFF;
  DiscardMenuRules rules{.confirmSection = inRange(reader, root, ConfirmSectionKey),
                         .confirmLine = inRange(reader, root, ConfirmLineKey),
                         .cancelTrigger = inRange(reader, root, CancelTriggerKey),
                         .chosenVariable = inRange(reader, root, ChosenVariableKey, ByteMost),
                         .noItem = inRange(reader, root, NoItemKey, ByteMost)};
  return reader.finish(std::move(rules), section.file);
}

DiscardMenu::DiscardMenu(DiscardMenuDesc desc) : desc_(desc) {}

DiscardMenu::~DiscardMenu() = default;

ruleset::SectionChecks DiscardMenu::sectionChecks() const {
  return {{std::string{DiscardMenuSection}, checkDiscardMenuSection}};
}

core::Result<void> DiscardMenu::readRules(const ruleset::Ruleset& rules) {
  const ruleset::Section* section = rules.section(DiscardMenuSection);
  if (section == nullptr) {
    rules_.reset();
    return {};
  }
  auto read = readDiscardMenuRules(*section);
  if (!read) {
    return std::unexpected{read.error()};
  }
  rules_ = std::move(*read);
  return {};
}

core::Result<void> DiscardMenu::startNewGame(GameSession& /*session*/) {
  open_ = false;
  return {};
}

const DiscardMenuRules& DiscardMenu::rules() const {
  DWE_ASSERT(rules_.has_value());
  return *rules_;
}

const assets::ItemTable& DiscardMenu::items() const {
  DWE_ASSERT(desc_.inventory != nullptr);
  return desc_.inventory->items();
}

bool DiscardMenu::open() {
  if (!rules_ || desc_.inventory == nullptr || desc_.story == nullptr) {
    core::logDebug(
        "[port:discard] openDiscardItem (script_common.c:1815): no discard_menu section, "
        "bag or story; the routine closes at once");
    return false;
  }
  const auto found = rows();
  const bool any =
      std::ranges::any_of(found, [](const auto& row) { return row && row->choosable; });
  if (!any) {
    // State 0 without an item to pick: setTrigger(3), writePStat(0xfe, 0xff).
    core::logDebug("[port:discard] openDiscardItem (script_common.c:1815): nothing to pick");
    auto& state = desc_.story->state();
    if (auto set = state.setTrigger(static_cast<std::int32_t>(rules_->cancelTrigger), true); !set) {
      core::logWarning("[port:discard] openDiscardItem: {}", set.error().message);
    }
    if (auto set = state.setVariable(static_cast<std::int32_t>(rules_->chosenVariable),
                                     static_cast<std::uint8_t>(rules_->noItem));
        !set) {
      core::logWarning("[port:discard] openDiscardItem: {}", set.error().message);
    }
    return false;
  }
  if (!lineTables_ && desc_.files != nullptr) {
    if (auto tables = readMapHeadLineTables(*desc_.files); tables) {
      lineTables_ = std::move(*tables);
    } else {
      core::logWarning("[port:discard] resolveMapHeadEntry: {}", tables.error().message);
    }
  }
  core::logDebug("[port:discard] openDiscardItem (script_common.c:1815): state 3, {} slots, "
                 "cancel {}",
                 found.size(), cancellable() ? "allowed" : "refused");
  open_ = true;
  return true;
}

std::vector<std::optional<DiscardRow>> DiscardMenu::rows() const {
  std::vector<std::optional<DiscardRow>> shown;
  if (desc_.inventory == nullptr) {
    return shown;
  }
  for (const auto& slot : desc_.inventory->inventory().slots()) {
    if (!slot) {
      shown.emplace_back();
      continue;
    }
    // The pick goes to a one-byte variable: an item a pack numbers past it cannot be named there.
    const assets::ItemEntry& entry = items().at(slot->item);
    const bool named = rules_ && entry.index >= 0 && entry.index < rules_->noItem;
    shown.push_back(DiscardRow{
        .item = slot->item, .count = slot->count, .choosable = entry.droppable != 0 && named});
  }
  return shown;
}

bool DiscardMenu::choose(const DiscardRow& row) {
  if (!open_ || !row.choosable) {
    return false;
  }
  return finish(items().at(row.item).index);
}

bool DiscardMenu::cancel() {
  if (!open_ || !cancellable()) {
    return false;
  }
  return finish(rules_->noItem);
}

bool DiscardMenu::cancellable() const {
  if (!rules_ || desc_.story == nullptr) {
    return false;
  }
  const auto set = desc_.story->state().trigger(static_cast<std::int32_t>(rules_->cancelTrigger));
  return set && *set;
}

bool DiscardMenu::finish(std::int64_t item) {
  // tickSingleCardShop: writePStat(0xfe, item) and unsetTrigger(3); the cancel writes 0xff.
  auto& state = desc_.story->state();
  if (auto set = state.setVariable(static_cast<std::int32_t>(rules_->chosenVariable),
                                   static_cast<std::uint8_t>(item));
      !set) {
    core::logWarning("[port:discard] openDiscardItem: {}", set.error().message);
    return false;
  }
  if (auto set = state.setTrigger(static_cast<std::int32_t>(rules_->cancelTrigger), false); !set) {
    core::logWarning("[port:discard] openDiscardItem: {}", set.error().message);
    return false;
  }
  core::logDebug("[port:discard] openDiscardItem (script_common.c:1815): state 4, item {}", item);
  open_ = false;
  return true;
}

std::string DiscardMenu::confirmKey() const {
  if (!lineTables_ || !rules_) {
    return {};
  }
  const auto table = lineTables_->find(rules_->confirmSection);
  const auto line = static_cast<std::size_t>(rules_->confirmLine);
  if (table == lineTables_->end() || line >= table->second.size()) {
    return {};
  }
  return table->second[line];
}

} // namespace dwe::session
