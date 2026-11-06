#pragma once

#include <dwe/assets/item_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/session/map_head_lines.hpp>
#include <dwe/session/session_system.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::session {

class InventoryAdapter;

// The ruleset section of the item keeper (rules/<id>/item_keeper.yaml).
inline constexpr std::string_view ItemKeeperSection = "item_keeper";

// What the keeper says, as line numbers of its MAPHEAD section (shop/script_menu.c:
// tickItemKeeper): the question, the yes-or-no choice and the farewell.
struct ItemKeeperLines {
  std::int32_t ask = 0;
  std::int32_t choices = 0;
  std::int32_t farewell = 0;
};

struct ItemKeeperRules {
  // showMapHeadTextbox(..., 0x4d5): the MAPHEAD section of the keeper's lines.
  std::int32_t linesSection = 0;
  ItemKeeperLines lines;
  // How many of each item the keeper holds (itemKeeperFillItemList: 0x63).
  std::int32_t maxCount = 0;
  // keeperMoveTen: the alternative button moves this many.
  std::int32_t step = 0;
};

core::Result<ItemKeeperRules> readItemKeeperRules(const ruleset::Section& section);

// The two lists of the keeper's windows (ITEM_MENU_LEFT and ITEM_MENU_RIGHT).
enum class KeeperSide : std::uint8_t { Kept, Bag };

// What a move takes: one (keeperMoveOne), ten (keeperMoveTen) or the whole stack
// (keeperMoveStack).
enum class KeeperAmount : std::uint8_t { One, Ten, Stack };

// A row of a list: the item, how many there are, and whether it can go to the other side
// (itemKeeperFillItemList's 0x80 flag).
struct KeeperRow {
  assets::ItemHandle item;
  std::int32_t count = 0;
  bool movable = false;
};

// Who keeps the items: the NPC the routine's variable 254 names, by species.
struct ItemKeeperVisit {
  std::string keeperSpecies;
};

struct ItemKeeperDesc {
  // The mounted packs, for script/maphead; they must outlive the keeper.
  const assets::VirtualFileSystem* files = nullptr;
  // The bag; it must outlive the keeper.
  InventoryAdapter* inventory = nullptr;
};

// The item keeper the story opens (script_interp.c: CALL_ROUTINE 11 -> shop/script_menu.c:
// tickItemKeeper): it holds what the player leaves with it (SCRIPT_STATE smth[0x54 + item]), moves
// items between it and the bag, and keeps the visit open until the game's screen closes it. What it
// holds is saved.
class ItemKeeper final : public SessionSystem {
public:
  explicit ItemKeeper(ItemKeeperDesc desc);
  ~ItemKeeper() override;
  ItemKeeper(const ItemKeeper&) = delete;
  ItemKeeper& operator=(const ItemKeeper&) = delete;
  ItemKeeper(ItemKeeper&&) = delete;
  ItemKeeper& operator=(ItemKeeper&&) = delete;

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  // A ruleset without the section has no keeper: a visit then closes at once.
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;
  // A save without the section starts with nothing kept.
  core::Result<void> addSaveSections(game::save::SaveSections& sections) override;
  core::Result<void> startNewGame(GameSession& session) override;

  void open(ItemKeeperVisit visit);
  void close();
  [[nodiscard]] const std::optional<ItemKeeperVisit>& visit() const { return visit_; }
  [[nodiscard]] bool available() const { return rules_.has_value(); }
  [[nodiscard]] const ItemKeeperRules& rules() const;
  [[nodiscard]] const assets::ItemTable& items() const;

  // How many of the item the keeper holds.
  [[nodiscard]] std::int32_t keptCount(assets::ItemHandle item) const;
  // itemKeeperFillItemList: the kept items in the order of their numbers; a row can move while the
  // bag has a free slot or a stack of it that is not full.
  [[nodiscard]] std::vector<KeeperRow> keptRows() const;
  // itemKeeperFillItemList: one row per bag slot, empty ones included; a row can move while the
  // keeper holds fewer than the most of it.
  [[nodiscard]] std::vector<std::optional<KeeperRow>> bagRows() const;
  // keeperMoveOne, keeperMoveTen and keeperMoveStack: moves the row's item from `from` to the
  // other side; false when the row cannot move or the amount does not fit (nothing changes).
  bool move(KeeperSide from, const KeeperRow& row, KeeperAmount amount);

  // The text key of a keeper's line (resolveMapHeadEntry); empty when MAPHEAD's listing does not
  // have it.
  [[nodiscard]] std::string lineKey(std::int32_t line) const;

private:
  class Serializer;

  bool take(const KeeperRow& row, KeeperAmount amount);
  bool leave(const KeeperRow& row, KeeperAmount amount);
  void setKept(assets::ItemHandle item, std::int32_t count);

  ItemKeeperDesc desc_;
  std::optional<ItemKeeperRules> rules_;
  std::optional<ItemKeeperVisit> visit_;
  std::unique_ptr<Serializer> serializer_;
  // What the keeper holds, by item id.
  std::map<std::string, std::int32_t, std::less<>> kept_;
  // MAPHEAD's sections as jump tables of text keys, read on the first visit.
  std::optional<MapHeadLineTables> lineTables_;
};

} // namespace dwe::session
