#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/inventory/card_shop.hpp>
#include <dwe/game/inventory/merit_shop.hpp>
#include <dwe/session/field_items_adapter.hpp>
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

class CardsAdapter;
class InventoryAdapter;
class StoryAdapter;

// The ruleset section of the merit exchange (rules/<id>/merit_shop.yaml).
inline constexpr std::string_view MeritShopSection = "merit_shop";

// What the keeper and the player say, as line numbers of the exchange's MAPHEAD section
// (shop/script_menu.c: openMeritShop, createMeritCardTradeDialogue): the greeting (state 0), the
// player's three choices (state 3), the yes-or-no of a card (state 7) and of an item (state 0xb),
// the lines over the card list (state 4) and the item list (state 5), and the first line of a
// card's worth: the card's rarity is added to it.
struct MeritShopLines {
  std::int32_t greeting = 0;
  std::int32_t choices = 0;
  std::int32_t giveCard = 0;
  std::int32_t takeItem = 0;
  std::int32_t cardPrompt = 0;
  std::int32_t itemPrompt = 0;
  std::int32_t cardWorth = 0;
};

struct MeritShopRules {
  // showMapHeadTextbox(..., 0x4d4): the section of the lines.
  std::int32_t linesSection = 0;
  MeritShopLines lines;
  // createShopBitsBox(0) -> tickShopBitsBox: the points box is line 7 of a keeper's section
  // (getShopkeeperLine: 0x4c4 for a keeper the shops do not list).
  std::int32_t pointsSection = 0;
  std::int32_t pointsLine = 0;
  // openMeritShop state 8: the merit stops here (9999).
  std::int32_t mostMerit = 0;
  // tickSelectionDialogue: while this trigger is set cancel does not answer a choice.
  std::int32_t choiceLockTrigger = 0;
  // createMeritCardTradeDialogue and readSelectedItemMerit: an empty row, or an item that cannot be
  // taken, plays this sound.
  SoundCue failSound;
  // playShopSoundOnlyInSavannah: a trade plays this sound, only on this map.
  std::string tradeMap;
  SoundCue tradeSound;
};

core::Result<MeritShopRules> readMeritShopRules(const ruleset::Section& section);

struct MeritShopDesc {
  // The mounted packs, for script/maphead; they must outlive the exchange.
  const assets::VirtualFileSystem* files = nullptr;
  // The player's cards, the bag and the story's triggers; they must outlive the exchange.
  CardsAdapter* cards = nullptr;
  InventoryAdapter* inventory = nullptr;
  StoryAdapter* story = nullptr;
};

// The merit exchange the story opens (script_interp.c: CALL_ROUTINE 1 -> shop/script_menu.c:
// openMeritShop): the player gives cards for merit points (the player record's merit) and takes the
// items that have a merit price. The game's screen shows the visit and closes it.
class MeritShop final : public SessionSystem {
public:
  explicit MeritShop(MeritShopDesc desc);

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  // A ruleset without the section has no exchange: routine 1 then closes at once.
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;
  core::Result<void> startNewGame(GameSession& session) override;
  void tick(GameSession& session, const ruleset::ClockAdvance& advance) override;
  void afterLoad(GameSession& session) override;

  // Opens a visit; false (nothing opens) without the rules, the cards, the bag or a game.
  bool open();
  void close();
  [[nodiscard]] bool visiting() const { return visiting_; }
  [[nodiscard]] bool available() const;
  [[nodiscard]] const MeritShopRules& rules() const;

  [[nodiscard]] std::int32_t merit() const;
  // The cards and the items, for the lists' names; only while available().
  [[nodiscard]] const game::inventory::CardCollection& cards() const;
  [[nodiscard]] const assets::ItemTable& items() const;

  // shopFillSellCardList: the card list, one row per card number (nothing for one not held).
  [[nodiscard]] std::vector<std::optional<game::inventory::CardSaleRow>> cardRows() const;
  // createMeritCardTradeDialogue: what a copy of the card is worth, and the line that says it (the
  // card's rarity past cardWorth); nothing for a card the player does not hold.
  [[nodiscard]] std::optional<std::int32_t> cardWorth(std::int32_t card) const;
  [[nodiscard]] std::int32_t worthLine(std::int32_t card) const;
  // openMeritShop state 8: the merit grows by the card's worth (up to the most) and a copy goes.
  // False, changing nothing, for a card the player does not hold.
  bool giveCard(std::int32_t card);

  // shopFillMeritItemList: the item list.
  [[nodiscard]] std::vector<game::inventory::MeritOffer> itemOffers() const;
  // openMeritShop state 0xc: the item's price is paid in merit and one goes in the bag. False,
  // changing nothing, for an item that cannot be taken now.
  bool takeItem(assets::ItemHandle item);

  // tickSelectionDialogue: whether cancel answers a choice (the last one).
  [[nodiscard]] bool choiceCancellable() const;

  void playFailSound();
  void playTradeSound(std::string_view mapId);

  // The text key of a line of the exchange's section (resolveMapHeadEntry); empty when MAPHEAD's
  // listing does not have it.
  [[nodiscard]] std::string lineKey(std::int32_t line) const;
  // The points box's line.
  [[nodiscard]] std::string pointsKey() const;

private:
  [[nodiscard]] std::string keyOf(std::int32_t section, std::int32_t line) const;
  void playSound(const SoundCue& cue);
  void setMerit(std::int32_t merit);

  MeritShopDesc desc_;
  std::optional<MeritShopRules> rules_;
  bool visiting_ = false;
  // The session whose player record holds the merit: the last one that ticked, loaded or started.
  GameSession* session_ = nullptr;
  std::optional<MapHeadLineTables> lineTables_;
};

} // namespace dwe::session
