#include "merit_shop_screen.hpp"

#include <dwe/core/log.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/screens/bag_view.hpp>
#include <dwe/screens/name_keys.hpp>

#include <array>
#include <cstddef>
#include <utility>
#include <variant>
#include <vector>

namespace dwe::app {
namespace {

using Phase = MeritShopScreen::Phase;

constexpr std::string_view Revision = "merit_shop.revision";
constexpr std::string_view RowList = "merit_shop.row";
// showMapheadSelection(1, 0xfd, 3, ...): show a card, use the points, leave.
constexpr std::int32_t MainChoices = 3;
constexpr std::int32_t ShowCard = 0;
constexpr std::int32_t UsePoints = 1;
// showMapheadSelection(2 or 3, 0xfd, 2, ...): yes and cancel.
constexpr std::int32_t YesNoChoices = 2;
constexpr std::int32_t Yes = 0;
// The player's slot of the name table (script_value.c: getSpeakerName, speaker 0xFD).
constexpr std::size_t PlayerNameSlot = 0;

const char* phaseName(Phase phase) {
  constexpr std::array<const char*, 8> Names{"closed",    "greeting",   "choice",
                                             "card_list", "card_worth", "card_choice",
                                             "item_list", "item_choice"};
  const auto at = static_cast<std::size_t>(std::to_underlying(phase));
  return at < Names.size() ? Names.at(at) : "?";
}

bool choosing(Phase phase) {
  return phase == Phase::Choice || phase == Phase::CardChoice || phase == Phase::ItemChoice;
}

bool cardPhase(Phase phase) {
  return phase == Phase::CardList || phase == Phase::CardWorth || phase == Phase::CardChoice;
}

bool itemPhase(Phase phase) {
  return phase == Phase::ItemList || phase == Phase::ItemChoice;
}

} // namespace

MeritShopScreen::MeritShopScreen(MeritShopScreenDesc desc) : desc_(desc) {}

bool MeritShopScreen::active() const {
  return phase_ != Phase::Closed;
}

bool MeritShopScreen::take(const session::StoryScreenRequest& request, const FieldScene& scene) {
  if (request.screen != session::StoryScreen::MeritShop) {
    return false;
  }
  waiting_ = request.waits;
  if (desc_.shop == nullptr || !desc_.shop->open()) {
    return true;
  }
  // openMeritShop reads the keeper from variable 254 (showMapHeadTextbox): the map's NPC with that
  // script id, by its species.
  keeperSpecies_.clear();
  for (const auto& npc : scene.map().npcs) {
    if (npc.scriptId == request.first && npc.digimon) {
      keeperSpecies_ = *npc.digimon;
    }
  }
  mapId_ = scene.map().id;
  list_ = {};
  choice_ = 0;
  card_.reset();
  item_.reset();
  phase_ = Phase::Greeting;
  desc_.ui->show(MeritShopDocument, true);
  core::logDebug("[port:merit] openMeritShop (shop/script_menu.c:788): keeper '{}' of script id {}",
                 keeperSpecies_, request.first);
  return true;
}

bool MeritShopScreen::handle(const ui::UiCommand& command) {
  if (phase_ == Phase::Closed) {
    return false;
  }
  if (command.name == "open_menu") {
    // The game's menu does not open over the exchange.
    return true;
  }
  if (command.name != "merit_shop_action") {
    return command.name.starts_with("merit_shop_");
  }
  if (const auto* action = std::get_if<std::string>(&command.value)) {
    act(*action);
  }
  return true;
}

void MeritShopScreen::press(const input::InputState& input) {
  if (phase_ != Phase::Closed) {
    jumpHeld_ = input.isDown("walk");
  }
}

void MeritShopScreen::act(std::string_view action) {
  switch (phase_) {
  case Phase::Closed:
    return;
  case Phase::Greeting:
    // State 1 (tickConfirmDialogue): only confirm goes on, to state 3.
    if (action == "confirm") {
      choice_ = 0;
      phase_ = Phase::Choice;
    }
    return;
  case Phase::CardWorth:
    // The worth read, state 7 asks to give it.
    if (action == "confirm") {
      choice_ = Yes;
      phase_ = Phase::CardChoice;
    }
    return;
  case Phase::Choice:
  case Phase::CardChoice:
  case Phase::ItemChoice:
    actInChoice(action);
    return;
  case Phase::CardList:
  case Phase::ItemList:
    actInList(action);
    return;
  }
}

std::int32_t MeritShopScreen::choiceCount() const {
  return phase_ == Phase::Choice ? MainChoices : YesNoChoices;
}

void MeritShopScreen::actInChoice(std::string_view action) {
  const std::int32_t count = choiceCount();
  if (action == "up" || action == "down") {
    // tickSelectionDialogue: the cursor wraps around.
    choice_ = (choice_ + (action == "up" ? count - 1 : 1)) % count;
  } else if (action == "confirm") {
    answer(choice_);
  } else if (action == "cancel") {
    // Cancel answers the last choice, unless trigger 0x31 is set.
    if (desc_.shop->choiceCancellable()) {
      answer(count - 1);
    } else {
      desc_.shop->playFailSound();
    }
  }
}

void MeritShopScreen::answer(std::int32_t picked) {
  switch (phase_) {
  case Phase::Choice:
    if (picked == ShowCard) {
      openList(Phase::CardList);
    } else if (picked == UsePoints) {
      openList(Phase::ItemList);
    } else {
      // State 6: the points box closes and so does the visit.
      close();
    }
    return;
  case Phase::CardChoice:
    // State 8 on yes: the card goes for its worth; state 9 on cancel. Both close the list and go
    // back to the three choices.
    if (picked == Yes && card_ && desc_.shop->giveCard(*card_)) {
      desc_.shop->playTradeSound(mapId_);
    }
    break;
  case Phase::ItemChoice:
    // State 0xc on yes: the item for its price; state 0xd on cancel.
    if (picked == Yes && item_ && desc_.shop->takeItem(*item_)) {
      desc_.shop->playTradeSound(mapId_);
    }
    break;
  default:
    return;
  }
  card_.reset();
  item_.reset();
  choice_ = 0;
  phase_ = Phase::Choice;
}

void MeritShopScreen::openList(Phase list) {
  // createCardMenu and createItemMenu: a new list starts at its first row.
  list_ = {};
  phase_ = list;
}

std::int32_t MeritShopScreen::rowCount() const {
  if (phase_ == Phase::CardList) {
    return static_cast<std::int32_t>(desc_.shop->cardRows().size());
  }
  return static_cast<std::int32_t>(desc_.shop->itemOffers().size());
}

void MeritShopScreen::actInList(std::string_view action) {
  if (action == "cancel") {
    // tickCardMenu and tickItemMenu: state 0xa closes the list, back to the three choices.
    choice_ = 0;
    phase_ = Phase::Choice;
    return;
  }
  const std::int32_t count = rowCount();
  if (action == "up") {
    jumpHeld_ ? list_.toTop() : list_.up();
  } else if (action == "down") {
    jumpHeld_ ? list_.toBottom(count) : list_.down(count);
  } else if (action == "confirm") {
    phase_ == Phase::CardList ? pickCard() : pickItem();
  }
}

void MeritShopScreen::pickCard() {
  const auto rows = desc_.shop->cardRows();
  const auto at = static_cast<std::size_t>(list_.index());
  const std::optional<std::int32_t> worth =
      at < rows.size() && rows[at] ? desc_.shop->cardWorth(rows[at]->card) : std::nullopt;
  // createMeritCardTradeDialogue: an empty row fails with its sound.
  if (!worth) {
    desc_.shop->playFailSound();
    return;
  }
  card_ = rows[at]->card;
  worth_ = *worth;
  phase_ = Phase::CardWorth;
}

void MeritShopScreen::pickItem() {
  const auto offers = desc_.shop->itemOffers();
  const auto at = static_cast<std::size_t>(list_.index());
  // readSelectedItemMerit: an item the player cannot take fails with its sound.
  if (at >= offers.size() || !offers[at].buyable) {
    desc_.shop->playFailSound();
    return;
  }
  item_ = offers[at].item;
  choice_ = Yes;
  phase_ = Phase::ItemChoice;
}

void MeritShopScreen::close() {
  desc_.ui->hide(MeritShopDocument);
  desc_.shop->close();
  card_.reset();
  item_.reset();
  phase_ = Phase::Closed;
}

void MeritShopScreen::update(session::StoryScenes& scenes) {
  if (phase_ != Phase::Closed) {
    screens::publish(*desc_.ui, settings(), Revision);
    return;
  }
  if (waiting_) {
    waiting_ = false;
    scenes.screenClosed();
  }
}

std::string MeritShopScreen::playerName() const {
  if (desc_.session != nullptr) {
    if (const std::string& name = desc_.session->names().tamer; !name.empty()) {
      return name;
    }
  }
  return localization::tr(screens::mainLabelKey(screens::SpeciesNamesBlock, PlayerNameSlot));
}

screens::ScreenSettings MeritShopScreen::settings() const {
  const session::MeritShop& shop = *desc_.shop;
  const session::MeritShopLines& lines = shop.rules().lines;
  std::int32_t line = lines.greeting;
  switch (phase_) {
  case Phase::Choice:
    line = lines.choices;
    break;
  case Phase::CardList:
    line = lines.cardPrompt;
    break;
  case Phase::CardWorth:
    line = card_ ? shop.worthLine(*card_) : lines.cardPrompt;
    break;
  case Phase::CardChoice:
    line = lines.giveCard;
    break;
  case Phase::ItemList:
    line = lines.itemPrompt;
    break;
  case Phase::ItemChoice:
    line = lines.takeItem;
    break;
  case Phase::Closed:
  case Phase::Greeting:
    break;
  }
  const screens::NameKeys names{species_, &desc_.shop->items(), nullptr};
  const bool player = choosing(phase_);
  const bool cards = cardPhase(phase_);
  screens::ScreenSettings out{
      {.name = "merit_shop.phase", .value = std::string{phaseName(phase_)}},
      {.name = "merit_shop.list",
       .value = std::string{cards ? "cards" : (itemPhase(phase_) ? "items" : "")}},
      // showMapheadSelection(..., 0xfd, ...): the choices are the player's.
      {.name = "merit_shop.speaker",
       .value = player || keeperSpecies_.empty() ? std::string{} : names.species(keeperSpecies_)},
      {.name = "merit_shop.player", .value = player ? playerName() : std::string{}},
      {.name = "merit_shop.line_key", .value = shop.lineKey(line)},
      {.name = "merit_shop.shopvar", .value = static_cast<double>(worth_)},
      {.name = "merit_shop.choice", .value = static_cast<double>(choice_)},
      // createShopBitsBox(0) at state 3, closed with the visit at state 6.
      {.name = "merit_shop.points_shown", .value = phase_ != Phase::Greeting},
      {.name = "merit_shop.points_key", .value = shop.pointsKey()},
      {.name = "merit_shop.points", .value = static_cast<double>(shop.merit())},
      {.name = "merit_shop.top", .value = static_cast<double>(list_.top)},
      {.name = "merit_shop.cursor", .value = static_cast<double>(list_.cursor)},
  };
  if (cards) {
    const auto rows = shop.cardRows();
    out.push_back({.name = "merit_shop.total", .value = static_cast<double>(rows.size())});
    for (std::int32_t i = 0; i < DiscardVisibleRows; ++i) {
      const auto at = static_cast<std::size_t>(list_.top + i);
      const std::optional<game::inventory::CardSaleRow> row =
          at < rows.size() ? rows[at] : std::nullopt;
      const assets::CardEntry* entry = row ? shop.cards().table().find(row->card) : nullptr;
      const auto slot = static_cast<std::size_t>(i);
      out.push_back({.name = screens::listEntryName(RowList, slot, "name"),
                     .value = entry != nullptr ? names.species(entry->digimon) : std::string{}});
      out.push_back({.name = screens::listEntryName(RowList, slot, "value"),
                     .value = static_cast<double>(row ? row->copies : 0)});
      out.push_back({.name = screens::listEntryName(RowList, slot, "rarity"),
                     .value = static_cast<double>(entry != nullptr ? entry->rarity : -1)});
      out.push_back({.name = screens::listEntryName(RowList, slot, "stuck"), .value = false});
      out.push_back(
          {.name = screens::listEntryName(RowList, slot, "icon"), .value = std::string{}});
      out.push_back(
          {.name = screens::listEntryName(RowList, slot, "icon_rect"), .value = std::string{}});
    }
  } else if (itemPhase(phase_)) {
    const auto offers = shop.itemOffers();
    out.push_back({.name = "merit_shop.total", .value = static_cast<double>(offers.size())});
    for (std::int32_t i = 0; i < DiscardVisibleRows; ++i) {
      const auto at = static_cast<std::size_t>(list_.top + i);
      const game::inventory::MeritOffer* offer = at < offers.size() ? &offers[at] : nullptr;
      const auto slot = static_cast<std::size_t>(i);
      const std::optional<screens::ItemIconView> icon =
          offer != nullptr ? screens::itemIconOf(shop.items().at(offer->item)) : std::nullopt;
      out.push_back({.name = screens::listEntryName(RowList, slot, "name"),
                     .value = offer != nullptr ? names.item(offer->item) : std::string{}});
      out.push_back({.name = screens::listEntryName(RowList, slot, "value"),
                     .value = static_cast<double>(offer != nullptr ? offer->price : 0)});
      out.push_back({.name = screens::listEntryName(RowList, slot, "rarity"), .value = -1.0});
      // calculateItemMenuStrings: an item the player cannot take is red (colour 3).
      out.push_back({.name = screens::listEntryName(RowList, slot, "stuck"),
                     .value = offer != nullptr && !offer->buyable});
      out.push_back({.name = screens::listEntryName(RowList, slot, "icon"),
                     .value = icon ? icon->image : std::string{}});
      out.push_back({.name = screens::listEntryName(RowList, slot, "icon_rect"),
                     .value = icon ? icon->rect : std::string{}});
    }
  }
  return out;
}

} // namespace dwe::app
