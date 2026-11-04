#include "card_shop_screen.hpp"

#include <dwe/core/log.hpp>
#include <dwe/screens/name_keys.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>
#include <variant>

namespace dwe::app {
namespace {

using game::inventory::SoldCard;
using session::CardShopKind;

constexpr std::string_view Revision = "card_shop.revision";
constexpr std::string_view RowList = "card_shop.row";
// tickSingleCardShop: left is yes, right is no.
constexpr std::int32_t Yes = 0;
constexpr std::int32_t No = 1;
// tickItemMenuAmountBox: the alternative button moves the amount by ten.
constexpr std::int32_t AmountStep = 10;

const char* phaseName(std::uint8_t phase) {
  constexpr std::array<const char*, 7> Names{"closed", "talk",  "list",   "confirm",
                                             "amount", "packs", "summary"};
  return phase < Names.size() ? Names.at(phase) : "?";
}

} // namespace

CardShopScreen::CardShopScreen(CardShopScreenDesc desc) : desc_(desc) {}

bool CardShopScreen::active() const {
  return phase_ != Phase::Closed;
}

bool CardShopScreen::take(const session::StoryScreenRequest& request, const FieldScene& scene) {
  const bool buying = request.screen == session::StoryScreen::CardBuy;
  const bool packs = request.screen == session::StoryScreen::CardPacks;
  if (!buying && !packs && request.screen != session::StoryScreen::CardSell) {
    return false;
  }
  waiting_ = request.waits;
  kind_ = buying ? CardShopKind::Buy : (packs ? CardShopKind::Pack : CardShopKind::Sell);
  if (desc_.shop == nullptr || !desc_.shop->open(kind_)) {
    return true;
  }
  // openBuyCardMenu and openSellCardMenu read the keeper from variable 254: the map's NPC with
  // that script id, by its species.
  keeperSpecies_.clear();
  for (const auto& npc : scene.map().npcs) {
    if (npc.scriptId == request.first && npc.digimon) {
      keeperSpecies_ = *npc.digimon;
    }
  }
  mapId_ = scene.map().id;
  list_ = {};
  choice_ = Yes;
  picked_.reset();
  summary_.clear();
  // The card_shop document draws the boxes: the amount box (script_common.c:
  // renderItemMenuAmountBox, the card, name and price over its two renderHorizontalLine rules) and
  // the "is it OK?" box (renderSingleCardShop: the question, yes and no, the cursor on the choice).
  desc_.ui->show(CardShopDocument, true);
  if (packs) {
    // The bulk purchase: the script has paid one pack; the count starts there.
    packs_.reset(desc_.shop->packLimit());
    phase_ = Phase::Packs;
    core::logDebug("[port:cards] bulk purchase: up to {} packs", packs_.most);
    return true;
  }
  // openBuyCardMenu state 0: without a card left in the stock the keeper says so and the menu does
  // not open (hasAnyCardToBuy).
  if (buying && !game::inventory::hasCardOnSale(desc_.shop->offers())) {
    phase_ = Phase::Talk;
  } else {
    phase_ = Phase::List;
  }
  core::logDebug("[port:cards] {} (shop/script_menu.c): keeper '{}' of script id {}, {}",
                 buying ? "openBuyCardMenu" : "openSellCardMenu", keeperSpecies_, request.first,
                 phaseName(std::to_underlying(phase_)));
  return true;
}

bool CardShopScreen::handle(const ui::UiCommand& command) {
  if (phase_ == Phase::Closed) {
    return false;
  }
  if (command.name == "open_menu") {
    // The game's menu does not open over the card menu.
    return true;
  }
  if (command.name != "card_shop_action") {
    return command.name.starts_with("card_shop_");
  }
  if (const auto* action = std::get_if<std::string>(&command.value)) {
    act(*action);
  }
  return true;
}

void CardShopScreen::press(const input::InputState& input) {
  if (phase_ == Phase::Closed) {
    return;
  }
  jumpHeld_ = input.isDown("walk");
  tenHeld_ = input.isDown("alt");
  // tickItemMenuAmountBox: square takes the most.
  if (phase_ == Phase::Amount && input.wasPressed("extra") && picked_) {
    amount_ = desc_.shop->cards().copies(*picked_);
  }
  if (phase_ == Phase::Packs && input.wasPressed("extra")) {
    packs_.act("extra");
  }
}

void CardShopScreen::act(std::string_view action) {
  switch (phase_) {
  case Phase::Closed:
    return;
  case Phase::Talk:
    // openBuyCardMenu state 1 then 2: the keeper's line read, the visit ends.
    if (action == "confirm" || action == "cancel") {
      close();
    }
    return;
  case Phase::List:
    actInList(action);
    return;
  case Phase::Confirm:
    actInConfirm(action);
    return;
  case Phase::Amount:
    actInAmount(action);
    return;
  case Phase::Packs:
    actInPacks(action);
    return;
  case Phase::Summary:
    actInSummary(action);
    return;
  }
}

void CardShopScreen::actInPacks(std::string_view action) {
  if (packs_.act(action)) {
    return;
  }
  if (action != "confirm" && action != "cancel") {
    return;
  }
  // Cancel keeps the one pack the script has paid. Each pack is a single purchase: its price, then
  // its draws, in the same order (CardShop::openPacks).
  const std::int32_t count = action == "confirm" ? packs_.value : 1;
  const std::vector<session::PackCard> drawn = desc_.shop->openPacks(count);
  packsOpened_ = count;
  cardsDrawn_ = static_cast<std::int32_t>(drawn.size());
  summary_ = session::summarizePacks(drawn);
  list_ = {};
  phase_ = Phase::Summary;
}

void CardShopScreen::actInSummary(std::string_view action) {
  const auto count = static_cast<std::int32_t>(summary_.size());
  if (action == "up") {
    jumpHeld_ ? list_.toTop() : list_.up();
  } else if (action == "down") {
    jumpHeld_ ? list_.toBottom(count) : list_.down(count);
  } else if (action == "confirm" || action == "cancel") {
    close();
  }
}

void CardShopScreen::actInList(std::string_view action) {
  if (action == "cancel") {
    // tickCardMenu: cancel closes the menu (state 4).
    close();
    return;
  }
  const auto count = static_cast<std::int32_t>(rows().size());
  if (action == "up") {
    jumpHeld_ ? list_.toTop() : list_.up();
  } else if (action == "down") {
    jumpHeld_ ? list_.toBottom(count) : list_.down(count);
  } else if (action == "confirm") {
    pick();
  }
}

void CardShopScreen::pick() {
  const std::optional<Row> row = current();
  // createSingleCardShopMenu and createItemMenuAmountBox: an empty row, or a card the player cannot
  // buy, fails with its sound.
  if (!row || !row->card || !row->choosable) {
    desc_.shop->playFailSound();
    return;
  }
  picked_ = row->card;
  if (kind_ == CardShopKind::Buy) {
    choice_ = Yes;
    phase_ = Phase::Confirm;
  } else {
    // createItemMenuAmountBox: the box starts at one, up to the copies held.
    amount_ = 1;
    phase_ = Phase::Amount;
  }
}

void CardShopScreen::actInConfirm(std::string_view action) {
  if (action == "left" || action == "right") {
    choice_ = action == "left" ? Yes : No;
    return;
  }
  if (action == "confirm" && choice_ == Yes) {
    // tickSingleCardShop: the card is bought, the slot sold, and the whole menu closes.
    if (desc_.shop->buy(static_cast<std::size_t>(list_.index()))) {
      desc_.shop->playTradeSound(mapId_);
      close();
      return;
    }
    phase_ = Phase::List;
    return;
  }
  if (action == "confirm" || action == "cancel") {
    // No, or cancel: the box closes and the list takes the buttons again.
    picked_.reset();
    phase_ = Phase::List;
  }
}

void CardShopScreen::actInAmount(std::string_view action) {
  if (!picked_) {
    phase_ = Phase::List;
    return;
  }
  const std::int32_t most = desc_.shop->cards().copies(*picked_);
  if (action == "up" && amount_ != most) {
    // tickItemMenuAmountBox: ten more stays put past the most.
    const std::int32_t step = tenHeld_ ? AmountStep : 1;
    if (amount_ + step <= most) {
      amount_ += step;
    }
  } else if (action == "down" && amount_ != 1) {
    // Ten fewer stays put at none or below.
    const std::int32_t step = tenHeld_ ? AmountStep : 1;
    if (amount_ - step > 0) {
      amount_ -= step;
    }
  } else if (action == "cancel") {
    picked_.reset();
    phase_ = Phase::List;
  } else if (action == "confirm") {
    // The copies go, their price is paid, and the whole menu closes (state 4).
    if (desc_.shop->sell(*picked_, amount_)) {
      desc_.shop->playTradeSound(mapId_);
      close();
      return;
    }
    picked_.reset();
    phase_ = Phase::List;
  }
}

void CardShopScreen::close() {
  desc_.ui->hide(CardShopDocument);
  desc_.shop->close();
  picked_.reset();
  summary_.clear();
  phase_ = Phase::Closed;
}

void CardShopScreen::update(session::StoryScenes& scenes) {
  if (phase_ != Phase::Closed) {
    screens::publish(*desc_.ui, settings(), Revision);
    return;
  }
  if (waiting_) {
    waiting_ = false;
    scenes.screenClosed();
  }
}

std::vector<CardShopScreen::Row> CardShopScreen::rows() const {
  std::vector<Row> shown;
  const session::CardShop& shop = *desc_.shop;
  if (kind_ == CardShopKind::Pack) {
    for (const session::PackSummaryRow& row : summary_) {
      shown.push_back({.card = row.card,
                       .price = 0,
                       .copies = row.count,
                       .choosable = true,
                       .unheld = row.isNew});
    }
    return shown;
  }
  if (kind_ == CardShopKind::Buy) {
    for (const game::inventory::CardOffer& offer : shop.offers()) {
      if (offer.card == SoldCard) {
        shown.push_back({});
        continue;
      }
      shown.push_back({.card = offer.card,
                       .price = offer.price,
                       .copies = 0,
                       .choosable = offer.buyable,
                       .unheld = offer.unheld});
    }
    return shown;
  }
  for (const auto& row : shop.saleRows()) {
    if (!row) {
      shown.push_back({});
      continue;
    }
    shown.push_back({.card = row->card,
                     .price = shop.sellPrice(row->card).value_or(0),
                     .copies = row->copies,
                     .choosable = true,
                     .unheld = false});
  }
  return shown;
}

std::optional<CardShopScreen::Row> CardShopScreen::current() const {
  const std::vector<Row> shown = rows();
  const auto at = static_cast<std::size_t>(list_.index());
  return at < shown.size() ? std::optional{shown[at]} : std::nullopt;
}

std::string CardShopScreen::cardName(std::int32_t card) const {
  const assets::CardEntry* entry = desc_.shop->cards().table().find(card);
  if (entry == nullptr) {
    return {};
  }
  return screens::NameKeys{species_, nullptr, nullptr}.species(entry->digimon);
}

std::int32_t CardShopScreen::rarityOf(std::int32_t card) const {
  const assets::CardEntry* entry = desc_.shop->cards().table().find(card);
  return entry == nullptr ? -1 : entry->rarity;
}

screens::ScreenSettings CardShopScreen::settings() const {
  const session::CardShop& shop = *desc_.shop;
  const session::CardShopLines& lines = shop.rules().lines;
  const bool buying = kind_ == CardShopKind::Buy;
  const bool packs = kind_ == CardShopKind::Pack;
  std::int32_t line = buying ? lines.today : lines.sellPrompt;
  if (phase_ == Phase::Talk) {
    line = lines.soldOut;
  }
  const std::vector<Row> shown = rows();
  screens::ScreenSettings out{
      {.name = "card_shop.phase", .value = std::string{phaseName(std::to_underlying(phase_))}},
      {.name = "card_shop.mode", .value = std::string{packs ? "packs" : (buying ? "buy" : "sell")}},
      {.name = "card_shop.speaker",
       .value = screens::NameKeys{species_, nullptr, nullptr}.species(keeperSpecies_)},
      {.name = "card_shop.line_key", .value = packs ? std::string{} : shop.lineKey(line)},
      {.name = "card_shop.total", .value = static_cast<double>(shown.size())},
      {.name = "card_shop.top", .value = static_cast<double>(list_.top)},
      {.name = "card_shop.cursor", .value = static_cast<double>(list_.cursor)},
      {.name = "card_shop.choice", .value = static_cast<double>(choice_)},
      {.name = "card_shop.confirm_key", .value = shop.confirmKey()},
  };
  for (std::int32_t i = 0; i < DiscardVisibleRows; ++i) {
    const auto at = static_cast<std::size_t>(list_.top + i);
    const std::optional<Row> row = at < shown.size() ? std::optional{shown[at]} : std::nullopt;
    const bool card = row && row->card;
    const auto entry = static_cast<std::size_t>(i);
    // calculateCardMenuStrings: on the buy list a card the player cannot buy is colour 3, one the
    // player holds none of colour 7, and the rest colour 1.
    std::string ink;
    if (card && buying) {
      ink = !row->choosable ? "stuck" : (row->unheld ? "new" : "");
    } else if (card && packs && row->unheld) {
      // The bulk purchase's summary marks a card the player held none of with the same ink.
      ink = "new";
    }
    out.push_back({.name = screens::listEntryName(RowList, entry, "name"),
                   .value = card ? cardName(*row->card) : std::string{}});
    out.push_back({.name = screens::listEntryName(RowList, entry, "price"),
                   .value = static_cast<double>(card ? row->price : 0)});
    out.push_back({.name = screens::listEntryName(RowList, entry, "count"),
                   .value = static_cast<double>(card ? row->copies : 0)});
    out.push_back({.name = screens::listEntryName(RowList, entry, "rarity"),
                   .value = static_cast<double>(card ? rarityOf(*row->card) : -1)});
    out.push_back({.name = screens::listEntryName(RowList, entry, "ink"), .value = ink});
  }
  if (packs) {
    std::int32_t fresh = 0;
    for (const session::PackSummaryRow& row : summary_) {
      fresh += row.isNew ? 1 : 0;
    }
    const std::int64_t price = shop.rules().packPrice;
    out.push_back({.name = "card_shop.packs.value", .value = static_cast<double>(packs_.value)});
    out.push_back({.name = "card_shop.packs.most", .value = static_cast<double>(packs_.most)});
    out.push_back({.name = "card_shop.packs.price", .value = static_cast<double>(price)});
    out.push_back(
        {.name = "card_shop.packs.total", .value = static_cast<double>(price * packs_.value)});
    out.push_back({.name = "card_shop.packs.opened", .value = static_cast<double>(packsOpened_)});
    out.push_back({.name = "card_shop.packs.cards", .value = static_cast<double>(cardsDrawn_)});
    out.push_back({.name = "card_shop.packs.new", .value = static_cast<double>(fresh)});
  }
  if (picked_ && phase_ == Phase::Amount) {
    const std::int32_t price = shop.sellPrice(*picked_).value_or(0);
    // updateItemMenuAmountBoxString: the total shown stops at the most money.
    const std::int64_t total =
        std::min<std::int64_t>(static_cast<std::int64_t>(price) * amount_, shop.maxMoney());
    out.push_back({.name = "card_shop.amount.name", .value = cardName(*picked_)});
    out.push_back(
        {.name = "card_shop.amount.rarity", .value = static_cast<double>(rarityOf(*picked_))});
    out.push_back({.name = "card_shop.amount.price", .value = static_cast<double>(price)});
    out.push_back({.name = "card_shop.amount.value", .value = static_cast<double>(amount_)});
    out.push_back({.name = "card_shop.amount.total", .value = static_cast<double>(total)});
  }
  return out;
}

} // namespace dwe::app
