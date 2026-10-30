#pragma once

#include <dwe/assets/item_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/session/map_head_lines.hpp>
#include <dwe/session/session_system.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::session {

class InventoryAdapter;
class StoryAdapter;

// The ruleset section of the menu that picks an item from the bag (rules/<id>/discard_menu.yaml).
inline constexpr std::string_view DiscardMenuSection = "discard_menu";

struct DiscardMenuRules {
  // createSingleCardShopMenu: showMapHeadTextbox(0, ..., 0x4d8), the "is it OK?" box.
  std::int32_t confirmSection = 0;
  std::int32_t confirmLine = 0;
  // openDiscardItem: while this trigger is set the player may close the menu without an item, and
  // it stays set when the bag has nothing to pick.
  std::int64_t cancelTrigger = 0;
  // The variable the picked item's number goes to (writePStat(0xfe)), and the number that says
  // none.
  std::int64_t chosenVariable = 0;
  std::int64_t noItem = 0;
};

core::Result<DiscardMenuRules> readDiscardMenuRules(const ruleset::Section& section);

// A bag slot as the menu lists it (shopFillSellItemList): the item, its count and whether it can be
// picked (ITEM_PARA droppable, the 0x80 flag).
struct DiscardRow {
  assets::ItemHandle item;
  std::int32_t count = 0;
  bool choosable = false;
};

struct DiscardMenuDesc {
  // The mounted packs, for script/maphead; they must outlive the menu.
  const assets::VirtualFileSystem* files = nullptr;
  // The bag and the story's trigger and variable; they must outlive the menu.
  InventoryAdapter* inventory = nullptr;
  StoryAdapter* story = nullptr;
};

// The menu the story opens to pick an item from the bag (script_interp.c: CALL_ROUTINE 0 ->
// script_common.c: openDiscardItem): to throw one away when the bag is full, or to hand one over.
// It lists every bag slot, lets the player pick a droppable item and writes the pick to the story;
// the story then removes it (routine 13) or reads it. The story waits while it is open.
class DiscardMenu final : public SessionSystem {
public:
  explicit DiscardMenu(DiscardMenuDesc desc);
  ~DiscardMenu() override;
  DiscardMenu(const DiscardMenu&) = delete;
  DiscardMenu& operator=(const DiscardMenu&) = delete;
  DiscardMenu(DiscardMenu&&) = delete;
  DiscardMenu& operator=(DiscardMenu&&) = delete;

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  // A ruleset without the section has no menu: the routine then closes at once.
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;
  core::Result<void> startNewGame(GameSession& session) override;

  // openDiscardItem state 0: opens the menu when the bag holds an item to pick; otherwise the
  // story learns that nothing was picked (the cancel trigger set, no item) and it stays closed.
  // Whether it opened.
  bool open();
  [[nodiscard]] bool isOpen() const { return open_; }
  [[nodiscard]] bool available() const { return rules_.has_value(); }
  [[nodiscard]] const DiscardMenuRules& rules() const;
  [[nodiscard]] const assets::ItemTable& items() const;

  // shopFillSellItemList: one row per bag slot, empty ones included.
  [[nodiscard]] std::vector<std::optional<DiscardRow>> rows() const;
  // tickSingleCardShop's yes: the item goes to the story, the cancel trigger is cleared and the
  // menu closes. False (nothing changes) for a row that cannot be picked.
  bool choose(const DiscardRow& row);
  // tickItemMenu's cancel: only while the story allows it (the cancel trigger set); the story then
  // reads no item and the trigger cleared. False when the menu stays open.
  bool cancel();
  // Whether cancel closes the menu now.
  [[nodiscard]] bool cancellable() const;

  // The text key of the "is it OK?" box (resolveMapHeadEntry); empty when MAPHEAD's listing does
  // not have it.
  [[nodiscard]] std::string confirmKey() const;

private:
  bool finish(std::int64_t item);

  DiscardMenuDesc desc_;
  std::optional<DiscardMenuRules> rules_;
  bool open_ = false;
  std::optional<MapHeadLineTables> lineTables_;
};

} // namespace dwe::session
