#include "shop_screen.hpp"

#include <dwe/core/log.hpp>
#include <dwe/screens/name_keys.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <variant>

namespace dwe::app {
namespace {

constexpr std::string_view Revision = "shop.revision";
constexpr std::string_view RowsList = "shop.row";

// The choice line's options, in the order of openShop's states 4 to 6.
enum class Choice : std::uint8_t { Buy, Sell, Leave };
constexpr std::int32_t ChoiceCount = 3;
// openRecycleShop: buy or leave.
constexpr std::int32_t RecycleChoiceCount = 2;

const char* phaseName(std::uint8_t phase) {
  constexpr std::array<const char*, 5> Names{"closed", "talk", "choice", "list", "amount"};
  return phase < Names.size() ? Names.at(phase) : "?";
}

std::int32_t numberOf(const ui::UiValue& value) {
  const double* number = std::get_if<double>(&value);
  return number == nullptr ? -1 : static_cast<std::int32_t>(*number);
}

} // namespace

ShopScreen::ShopScreen(ShopScreenDesc desc) : desc_(desc) {}

bool ShopScreen::active() const {
  return phase_ != Phase::Closed;
}

void ShopScreen::update() {
  if (desc_.shop == nullptr) {
    return;
  }
  if (phase_ == Phase::Closed && desc_.shop->visit()) {
    start();
  }
  if (phase_ != Phase::Closed) {
    screens::publish(*desc_.ui, settings(), Revision);
  }
}

bool ShopScreen::handle(const ui::UiCommand& command) {
  if (phase_ == Phase::Closed) {
    return false;
  }
  const std::string& name = command.name;
  if (name == "open_menu") {
    // The pause menu does not open over the shop.
    return true;
  }
  if (name == "shop_next" && phase_ == Phase::Talk) {
    switch (after_) {
    case After::Close:
      finish();
      break;
    case After::SoldOut:
      // openRecycleShop state 9: it has nothing left, and the farewell follows.
      talk(desc_.shop->rules().lines.soldOut, After::Leave);
      break;
    case After::Leave:
      leave();
      break;
    case After::Choice:
      setPhase(Phase::Choice);
      break;
    }
  } else if (name == "shop_choice" && phase_ == Phase::Choice) {
    choose(numberOf(command.value));
  } else if (name == "shop_pick" && phase_ == Phase::List) {
    pick(numberOf(command.value));
  } else if (name == "shop_amount" && phase_ == Phase::Amount) {
    moveAmount(numberOf(command.value));
  } else if (name == "shop_amount_max" && phase_ == Phase::Amount) {
    amount_ = maxAmount();
  } else if (name == "shop_confirm" && phase_ == Phase::Amount) {
    confirm();
  } else if (name == "shop_back") {
    back();
  } else {
    return name.starts_with("shop_");
  }
  return true;
}

void ShopScreen::start() {
  traded_ = false;
  saved_ = 0;
  bitsShown_ = false;
  picked_.reset();
  desc_.ui->show(ShopMenuDocument, true);
  // openShop state 0: a keeper with nothing in stock says so and the visit ends.
  const auto stock = desc_.shop->stock();
  const session::ShopLines& lines = desc_.shop->rules().lines;
  core::logDebug("[port:shop] openShop (script_common.c:1618): state 0, {} items in stock",
                 stock.size());
  if (stock.empty()) {
    talk(lines.nothingToSell, After::Close);
  } else {
    talk(lines.greeting, After::Choice);
  }
}

void ShopScreen::talk(std::int32_t line, After after) {
  line_ = line;
  after_ = after;
  setPhase(Phase::Talk);
}

void ShopScreen::choose(std::int32_t choice) {
  if (choice < 0 || choice > static_cast<std::int32_t>(Choice::Leave)) {
    return;
  }
  // openRecycleShop: its choice is buy or leave.
  if (desc_.shop->recycling() && choice == static_cast<std::int32_t>(Choice::Sell)) {
    choice = static_cast<std::int32_t>(Choice::Leave);
  }
  switch (static_cast<Choice>(choice)) {
  case Choice::Buy:
    showList(Mode::Buy);
    break;
  case Choice::Sell:
    showList(Mode::Sell);
    break;
  case Choice::Leave:
    leave();
    break;
  }
}

void ShopScreen::showList(Mode mode) {
  // openShop states 4 and 5: shopFillBuyItemList, shopFillSellItemList.
  mode_ = mode;
  const session::ShopLines& lines = desc_.shop->rules().lines;
  line_ = mode == Mode::Buy ? lines.buyPrompt : lines.sellPrompt;
  setPhase(Phase::List);
}

void ShopScreen::pick(std::int32_t row) {
  const std::vector<Row> shown = rows();
  if (row < 0 || static_cast<std::size_t>(row) >= shown.size() ||
      !shown[static_cast<std::size_t>(row)].enabled) {
    return;
  }
  picked_ = shown[static_cast<std::size_t>(row)].item;
  if (maxAmount() < 1) {
    picked_.reset();
    return;
  }
  // createItemMenuAmountBox (script_common.c:1964): the box starts at one.
  amount_ = 1;
  setPhase(Phase::Amount);
}

void ShopScreen::moveAmount(std::int32_t delta) {
  // tickItemMenuAmountBox: up and down move by one, the other buttons by the step; the amount stays
  // between one and the most.
  amount_ = std::clamp(amount_ + delta, 1, std::max(1, maxAmount()));
}

void ShopScreen::confirm() {
  if (!picked_) {
    return;
  }
  session::GameSession& session = *desc_.session;
  const session::ShopLines& lines = desc_.shop->rules().lines;
  std::int32_t line = lines.thanks;
  if (mode_ == Mode::Buy) {
    auto bought = desc_.shop->buy(session, *picked_, amount_);
    if (!bought) {
      core::logWarning("[port:shop] tickItemMenuAmountBox: {}", bought.error().message);
      setPhase(Phase::List);
      return;
    }
    // openShop state 7: a baby partner's purchase says what the discount saved.
    saved_ = bought->saved;
    if (bought->saved > 0) {
      line = lines.discount;
    }
  } else if (auto sold = desc_.shop->sell(session, *picked_, amount_); !sold) {
    core::logWarning("[port:shop] tickItemMenuAmountBox: {}", sold.error().message);
    setPhase(Phase::List);
    return;
  }
  traded_ = true;
  picked_.reset();
  // openRecycleShop state 7: once the recycle shop has nothing left it says so and the visit ends.
  if (desc_.shop->recycling() && desc_.shop->stock().empty()) {
    talk(line, After::SoldOut);
    return;
  }
  talk(line, After::Choice);
}

void ShopScreen::back() {
  switch (phase_) {
  case Phase::Amount:
    picked_.reset();
    setPhase(Phase::List);
    break;
  case Phase::List:
    // openShop state 8: "anything else?" and the choice again.
    talk(desc_.shop->rules().lines.anythingElse, After::Choice);
    break;
  case Phase::Choice:
    leave();
    break;
  case Phase::Talk:
  case Phase::Closed:
    break;
  }
}

void ShopScreen::leave() {
  // openShop state 6: the money box closes (triggerBoxCloseFlag(2)) and the farewell says whether
  // the player traded.
  bitsShown_ = false;
  const session::ShopLines& lines = desc_.shop->rules().lines;
  talk(traded_ ? lines.farewellAfterTrade : lines.farewell, After::Close);
}

void ShopScreen::finish() {
  bitsShown_ = false;
  desc_.ui->hide(ShopMenuDocument);
  desc_.shop->close();
  setPhase(Phase::Closed);
}

void ShopScreen::setPhase(Phase phase) {
  core::logDebug("[port:shop] openShop (script_common.c:1618): {} -> {}",
                 phaseName(static_cast<std::uint8_t>(phase_)),
                 phaseName(static_cast<std::uint8_t>(phase)));
  // openShop state 3 (openRecycleShop's too): the money box opens with the choice and stays up
  // through the lists, the amount box and the keeper's lines until the farewell.
  if (phase == Phase::Choice) {
    bitsShown_ = true;
  }
  phase_ = phase;
}

std::vector<ShopScreen::Row> ShopScreen::rows() const {
  std::vector<Row> shown;
  if (mode_ == Mode::Buy) {
    for (const auto& offer : desc_.shop->stock()) {
      shown.push_back({.item = offer.item,
                       .price = desc_.shop->shownBuyPrice(offer.item),
                       .count = 0,
                       .enabled = offer.buyable});
    }
  } else {
    for (const auto& offer : desc_.shop->saleOffers()) {
      shown.push_back({.item = offer.item,
                       .price = desc_.shop->sellPrice(offer.item),
                       .count = offer.count,
                       .enabled = offer.sellable});
    }
  }
  return shown;
}

std::int32_t ShopScreen::maxAmount() const {
  if (!picked_) {
    return 0;
  }
  if (mode_ == Mode::Buy) {
    return desc_.shop->maxPurchase(*desc_.session, *picked_);
  }
  // A sale's most is the stack the player has.
  for (const auto& offer : desc_.shop->saleOffers()) {
    if (offer.item == *picked_) {
      return offer.count;
    }
  }
  return 0;
}

screens::ScreenSettings ShopScreen::settings() const {
  const session::ShopCounter& shop = *desc_.shop;
  const session::ShopLines& lines = shop.rules().lines;
  const screens::NameKeys names{nullptr, &shop.items(), nullptr};
  // The line the counter shows: the chosen line, or the choice of the shop or the recycle shop.
  std::int32_t lineShown = line_;
  if (phase_ == Phase::Choice) {
    lineShown = shop.recycling() ? lines.recycleChoices : lines.choices;
  }
  screens::ScreenSettings out{
      {.name = "shop.phase", .value = std::string{phaseName(static_cast<std::uint8_t>(phase_))}},
      {.name = "shop.mode", .value = std::string{mode_ == Mode::Buy ? "buy" : "sell"}},
      {.name = "shop.line_key", .value = shop.lineKey(lineShown)},
      {.name = "shop.choice_count",
       .value = static_cast<double>(shop.recycling() ? RecycleChoiceCount : ChoiceCount)},
      {.name = "shop.bits_key", .value = shop.lineKey(lines.bits)},
      {.name = "shop.money", .value = static_cast<double>(shop.money())},
      {.name = "shop.bits_shown", .value = bitsShown_},
      {.name = "shop.saved", .value = static_cast<double>(saved_)},
  };
  const std::vector<Row> shown = rows();
  out.push_back({.name = "shop.rows", .value = static_cast<double>(shown.size())});
  for (std::size_t i = 0; i < shown.size(); ++i) {
    out.push_back(
        {.name = screens::listEntryName(RowsList, i, "name"), .value = names.item(shown[i].item)});
    out.push_back({.name = screens::listEntryName(RowsList, i, "price"),
                   .value = static_cast<double>(shown[i].price)});
    out.push_back({.name = screens::listEntryName(RowsList, i, "count"),
                   .value = static_cast<double>(shown[i].count)});
    out.push_back(
        {.name = screens::listEntryName(RowsList, i, "enabled"), .value = shown[i].enabled});
  }
  if (picked_) {
    const std::int32_t price =
        mode_ == Mode::Buy ? shop.shownBuyPrice(*picked_) : shop.sellPrice(*picked_);
    // createItemMenuAmountBox: the total shown stops at the most money.
    const std::int64_t total =
        std::min<std::int64_t>(static_cast<std::int64_t>(price) * amount_, shop.maxMoney());
    out.push_back({.name = "shop.amount.name", .value = names.item(*picked_)});
    out.push_back({.name = "shop.amount.price", .value = static_cast<double>(price)});
    out.push_back({.name = "shop.amount.value", .value = static_cast<double>(amount_)});
    out.push_back({.name = "shop.amount.max", .value = static_cast<double>(maxAmount())});
    out.push_back({.name = "shop.amount.total", .value = static_cast<double>(total)});
    out.push_back(
        {.name = "shop.amount.step", .value = static_cast<double>(shop.rules().amountStep)});
  }
  return out;
}

} // namespace dwe::app
