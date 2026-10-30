#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/item_keeper.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>
#include <utility>

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view LinesSectionKey = "lines_section";
constexpr std::string_view LinesKey = "lines";
constexpr std::string_view AskKey = "ask";
constexpr std::string_view ChoicesKey = "choices";
constexpr std::string_view FarewellKey = "farewell";
constexpr std::string_view MaxCountKey = "max_count";
constexpr std::string_view StepKey = "step";
// The save section of what the keeper holds.
constexpr std::string_view KeeperSaveName = "item_keeper";
constexpr std::string_view KeptKey = "kept";
constexpr std::string_view ItemKey = "item";
constexpr std::string_view CountKey = "count";

std::int32_t nonNegative(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  const YamlNode& node = reader.field(parent, key);
  const auto value = reader.integer<std::int32_t>(node);
  if (reader.ok() && value < 0) {
    reader.fail(node, std::format("{} cannot be negative", key));
  }
  return value;
}

core::Result<void> checkItemKeeperSection(const ruleset::Section& section) {
  if (auto read = readItemKeeperRules(section); !read) {
    return std::unexpected{read.error()};
  }
  return {};
}

} // namespace

core::Result<ItemKeeperRules> readItemKeeperRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, ItemKeeperSection,
                         {LinesSectionKey, LinesKey, MaxCountKey, StepKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  ItemKeeperRules rules{.linesSection = nonNegative(reader, root, LinesSectionKey),
                        .lines = {},
                        .maxCount = nonNegative(reader, root, MaxCountKey),
                        .step = nonNegative(reader, root, StepKey)};
  const YamlNode& lines = reader.field(root, LinesKey);
  if (reader.expectKeys(lines, LinesKey, {AskKey, ChoicesKey, FarewellKey})) {
    rules.lines = {.ask = nonNegative(reader, lines, AskKey),
                   .choices = nonNegative(reader, lines, ChoicesKey),
                   .farewell = nonNegative(reader, lines, FarewellKey)};
  }
  return reader.finish(std::move(rules), section.file);
}

class ItemKeeper::Serializer final : public game::save::SectionSerializer {
public:
  explicit Serializer(ItemKeeper& keeper) : keeper_(&keeper) {}

  [[nodiscard]] core::Result<YamlNode> write() const override {
    YamlNode kept = YamlNode::sequence();
    for (const auto& [item, count] : keeper_->kept_) {
      YamlNode entry = YamlNode::mapping();
      entry.set(std::string{ItemKey}, YamlNode::scalar(item));
      entry.set(std::string{CountKey}, YamlNode::scalar(std::to_string(count)));
      kept.append(std::move(entry));
    }
    YamlNode root = YamlNode::mapping();
    root.set(std::string{KeptKey}, std::move(kept));
    return root;
  }

  core::Result<void> read(const YamlNode& state) override {
    YamlReader reader;
    std::map<std::string, std::int32_t, std::less<>> kept;
    if (reader.expectKeys(state, KeeperSaveName, {KeptKey})) {
      for (const YamlNode& entry : reader.sequence(reader.field(state, KeptKey), KeptKey)) {
        if (reader.expectKeys(entry, KeptKey, {ItemKey, CountKey})) {
          const std::string item = reader.text(reader.field(entry, ItemKey));
          if (const std::int32_t count = nonNegative(reader, entry, CountKey); count > 0) {
            kept[item] = count;
          }
        }
      }
    }
    auto read = reader.finish(std::move(kept), std::string{KeeperSaveName});
    if (!read) {
      return std::unexpected{read.error()};
    }
    keeper_->kept_ = std::move(*read);
    return {};
  }

  core::Result<void> readMissing() override {
    keeper_->kept_.clear();
    return {};
  }

private:
  ItemKeeper* keeper_;
};

ItemKeeper::ItemKeeper(ItemKeeperDesc desc)
    : desc_(desc), serializer_(std::make_unique<Serializer>(*this)) {}

ItemKeeper::~ItemKeeper() = default;

ruleset::SectionChecks ItemKeeper::sectionChecks() const {
  return {{std::string{ItemKeeperSection}, checkItemKeeperSection}};
}

core::Result<void> ItemKeeper::readRules(const ruleset::Ruleset& rules) {
  const ruleset::Section* section = rules.section(ItemKeeperSection);
  if (section == nullptr) {
    rules_.reset();
    return {};
  }
  auto read = readItemKeeperRules(*section);
  if (!read) {
    return std::unexpected{read.error()};
  }
  rules_ = std::move(*read);
  return {};
}

core::Result<void> ItemKeeper::addSaveSections(game::save::SaveSections& sections) {
  return sections.add(std::string{KeeperSaveName}, *serializer_, game::save::MigrationChain{});
}

core::Result<void> ItemKeeper::startNewGame(GameSession& /*session*/) {
  kept_.clear();
  visit_.reset();
  return {};
}

void ItemKeeper::open(ItemKeeperVisit visit) {
  if (!rules_ || desc_.inventory == nullptr) {
    core::logDebug("[port:keeper] tickItemKeeper (shop/script_menu.c:892): the ruleset has no "
                   "item_keeper section; the visit closes at once");
    return;
  }
  if (!lineTables_ && desc_.files != nullptr) {
    if (auto tables = readMapHeadLineTables(*desc_.files); tables) {
      lineTables_ = std::move(*tables);
    } else {
      core::logWarning("[port:keeper] resolveMapHeadEntry: {}", tables.error().message);
    }
  }
  visit_ = std::move(visit);
}

void ItemKeeper::close() {
  visit_.reset();
}

const ItemKeeperRules& ItemKeeper::rules() const {
  DWE_ASSERT(rules_.has_value());
  return *rules_;
}

const assets::ItemTable& ItemKeeper::items() const {
  DWE_ASSERT(desc_.inventory != nullptr);
  return desc_.inventory->items();
}

std::int32_t ItemKeeper::keptCount(assets::ItemHandle item) const {
  const auto found = kept_.find(items().at(item).id);
  return found == kept_.end() ? 0 : found->second;
}

void ItemKeeper::setKept(assets::ItemHandle item, std::int32_t count) {
  const std::string& id = items().at(item).id;
  if (count <= 0) {
    kept_.erase(id);
  } else {
    kept_[id] = count;
  }
}

std::vector<KeeperRow> ItemKeeper::keptRows() const {
  const game::inventory::Inventory& bag = desc_.inventory->inventory();
  const std::int32_t maxStack = bag.rules().bag.maxStack;
  const auto slots = bag.slots();
  const bool freeSlot = std::ranges::any_of(slots, [](const auto& slot) { return !slot; });
  std::vector<KeeperRow> rows;
  for (const assets::ItemEntry& entry : items().entries()) {
    const auto found = kept_.find(entry.id);
    if (found == kept_.end() || found->second <= 0) {
      continue;
    }
    const assets::ItemHandle item = items().handleOf(entry);
    const bool roomInStack = std::ranges::any_of(slots, [&](const auto& slot) {
      return slot && slot->item == item && slot->count != maxStack;
    });
    rows.push_back({.item = item, .count = found->second, .movable = freeSlot || roomInStack});
  }
  // itemKeeperFillItemList walks the items by number.
  std::ranges::stable_sort(rows, {},
                           [this](const KeeperRow& row) { return items().at(row.item).index; });
  return rows;
}

std::vector<std::optional<KeeperRow>> ItemKeeper::bagRows() const {
  std::vector<std::optional<KeeperRow>> rows;
  for (const auto& slot : desc_.inventory->inventory().slots()) {
    if (!slot) {
      rows.emplace_back();
      continue;
    }
    rows.push_back(KeeperRow{.item = slot->item,
                             .count = slot->count,
                             .movable = keptCount(slot->item) != rules().maxCount});
  }
  return rows;
}

bool ItemKeeper::move(KeeperSide from, const KeeperRow& row, KeeperAmount amount) {
  if (!rules_ || !row.movable) {
    return false;
  }
  const bool moved = from == KeeperSide::Kept ? take(row, amount) : leave(row, amount);
  if (moved) {
    core::logDebug("[port:keeper] keeperMove (shop/script_menu.c:2061): {} of {} from the {}, kept "
                   "now {}",
                   amount == KeeperAmount::One   ? "one"
                   : amount == KeeperAmount::Ten ? "ten"
                                                 : "the stack",
                   items().at(row.item).id, from == KeeperSide::Kept ? "keeper" : "bag",
                   keptCount(row.item));
  }
  return moved;
}

// From the keeper to the bag: the bag's stack of the item stops at its most.
bool ItemKeeper::take(const KeeperRow& row, KeeperAmount amount) {
  game::inventory::Inventory& bag = desc_.inventory->inventory();
  const std::int32_t maxStack = bag.rules().bag.maxStack;
  const std::int32_t inBag = bag.countOf(row.item);
  const std::int32_t kept = keptCount(row.item);
  std::int32_t count = 0;
  switch (amount) {
  case KeeperAmount::One:
    if (inBag >= maxStack) {
      return false;
    }
    count = 1;
    break;
  case KeeperAmount::Ten:
    if (kept < rules_->step || inBag > maxStack - rules_->step) {
      return false;
    }
    count = rules_->step;
    break;
  case KeeperAmount::Stack:
    count = std::min(kept, maxStack - inBag);
    break;
  }
  if (count <= 0 || count > kept) {
    return false;
  }
  setKept(row.item, kept - count);
  bag.add(row.item, count);
  return true;
}

// From the bag to the keeper: the keeper holds at most maxCount of an item.
bool ItemKeeper::leave(const KeeperRow& row, KeeperAmount amount) {
  game::inventory::Inventory& bag = desc_.inventory->inventory();
  const std::int32_t kept = keptCount(row.item);
  std::int32_t count = 0;
  switch (amount) {
  case KeeperAmount::One:
    if (bag.countOf(row.item) == 0) {
      return false;
    }
    count = 1;
    break;
  case KeeperAmount::Ten:
    if (row.count < rules_->step || kept > rules_->maxCount - rules_->step) {
      return false;
    }
    count = rules_->step;
    break;
  case KeeperAmount::Stack:
    count = std::min(row.count, rules_->maxCount - kept);
    break;
  }
  if (count <= 0) {
    return false;
  }
  bag.remove(row.item, count);
  setKept(row.item, kept + count);
  return true;
}

std::string ItemKeeper::lineKey(std::int32_t line) const {
  if (!lineTables_ || !rules_ || line < 0) {
    return {};
  }
  const auto table = lineTables_->find(rules_->linesSection);
  if (table == lineTables_->end() || static_cast<std::size_t>(line) >= table->second.size()) {
    return {};
  }
  return table->second[static_cast<std::size_t>(line)];
}

} // namespace dwe::session
