#include "shop_haggle_screen.hpp"

#include <dwe/core/log.hpp>
#include <dwe/screens/bag_view.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/session/shop_job_adapter.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/tournament_adapter.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <variant>

#include "shop_job_rules_panel.hpp"

namespace dwe::app {

namespace {

namespace shop_job = game::shop_job;

constexpr std::string_view OfferCommand = "shop_haggle_offer";
constexpr std::string_view TalkCommand = "shop_haggle_talk";
constexpr std::string_view DismissCommand = "shop_haggle_dismiss";
constexpr std::string_view RowList = "shop_haggle.row";
constexpr std::string_view OfferList = "shop_haggle.offer";
// The face of no reaction yet: the document shows none.
constexpr double NoFace = -1.0;
// The text keys of the species' characters and the moods: shop_job.character.<name>,
// shop_job.mood.<name>.
constexpr std::string_view CharacterKey = "shop_job.character.{}";
constexpr std::string_view MoodKey = "shop_job.mood.{}";

double number(std::int64_t value) {
  return static_cast<double>(value);
}

std::string setting(std::string_view name) {
  return std::format("shop_haggle.{}", name);
}

} // namespace

screens::ScreenSettings shopHaggleSettings(const ShopHaggleView& view) {
  screens::ScreenSettings out;
  const auto put = [&out](std::string_view name, ui::UiValue value) {
    out.push_back({.name = setting(name), .value = std::move(value)});
  };
  put("tally", number(view.tally));
  put("goal", number(view.perfectScore));
  put("better_pay", number(view.tiers.betterPay));
  put("joins", number(view.tiers.joins));
  put("bonus", number(view.tiers.bonus));
  put("customer", number(static_cast<std::int64_t>(view.customer)));
  put("customers", number(static_cast<std::int64_t>(view.customers)));
  if (view.rules == nullptr || view.haggle == nullptr) {
    put("shown", false);
    return out;
  }
  const shop_job::ShopJobRules& rules = *view.rules;
  const shop_job::Haggle& haggle = *view.haggle;
  const shop_job::Customer& customer = haggle.customer();
  const shop_job::SpeciesRules& species = rules.species.at(customer.species);
  const shop_job::MoodRules& mood = rules.moods.at(customer.mood);
  const shop_job::ItemRules& item = rules.items.at(customer.item);
  put("shown", true);
  put("species", view.names.species);
  put("character", std::format(CharacterKey, species.character));
  put("character_name", species.character);
  put("mood", std::format(MoodKey, mood.name));
  put("mood_name", mood.name);
  put("patience", static_cast<double>(haggle.patience()));
  put("patience_max", static_cast<double>(species.patience));
  put("talked", haggle.talked());
  put("can_talk", haggle.canTalk());
  put("item", view.names.item);
  put("species_icon", view.names.speciesIcon);
  put("species_icon_rect", view.names.speciesIconRect);
  put("item_icon", view.names.itemIcon);
  put("item_icon_rect", view.names.itemIconRect);
  put("regular_level", static_cast<double>(rules.regularLevel));
  put("regular_price",
      static_cast<double>(item.prices.at(static_cast<std::size_t>(rules.regularLevel))));
  const auto& offers = haggle.offers();
  if (offers.empty()) {
    put("face", NoFace);
    put("reaction", std::string{});
  } else {
    const shop_job::ReactionRules& last = rules.rulesOf(offers.back().reaction);
    put("face", static_cast<double>(last.face));
    put("reaction", last.name);
  }
  put("rows", static_cast<double>(shop_job::Levels));
  for (shop_job::Level level = 0; level < shop_job::NoSale; ++level) {
    const auto row = static_cast<std::size_t>(level);
    const shop_job::ReactionRules& likely = rules.rulesOf(haggle.predicted(level));
    const bool offered = std::ranges::any_of(
        offers, [level](const shop_job::Offer& offer) { return offer.level == level; });
    out.push_back({.name = screens::listEntryName(RowList, row, "price"),
                   .value = static_cast<double>(item.prices.at(row))});
    out.push_back({.name = screens::listEntryName(RowList, row, "profit"),
                   .value = number(rules.profit(customer.item, level))});
    out.push_back({.name = screens::listEntryName(RowList, row, "chance"),
                   .value = static_cast<double>(haggle.chance(level))});
    out.push_back({.name = screens::listEntryName(RowList, row, "reaction"), .value = likely.name});
    out.push_back({.name = screens::listEntryName(RowList, row, "face"),
                   .value = static_cast<double>(likely.face)});
    out.push_back({.name = screens::listEntryName(RowList, row, "offered"), .value = offered});
  }
  put("offers", static_cast<double>(offers.size()));
  for (std::size_t index = 0; index < offers.size(); ++index) {
    const shop_job::ReactionRules& reaction = rules.rulesOf(offers[index].reaction);
    out.push_back({.name = screens::listEntryName(OfferList, index, "price"),
                   .value = static_cast<double>(
                       item.prices.at(static_cast<std::size_t>(offers[index].level)))});
    out.push_back(
        {.name = screens::listEntryName(OfferList, index, "reaction"), .value = reaction.name});
    out.push_back({.name = screens::listEntryName(OfferList, index, "face"),
                   .value = static_cast<double>(reaction.face)});
  }
  return out;
}

ShopHaggleScreen::ShopHaggleScreen(ShopHaggleScreenDesc desc) : desc_(desc) {}

bool ShopHaggleScreen::take(const session::StoryScreenRequest& request,
                            const session::StoryScenes& /*scenes*/) {
  if (request.screen != session::StoryScreen::ShopHaggle) {
    return false;
  }
  const shop_job::Haggle* haggle = desc_.job != nullptr ? desc_.job->haggle() : nullptr;
  if (haggle == nullptr || desc_.ui == nullptr || !request.waits) {
    // No customer at the counter: the story goes on as if it were sent away.
    over_ = request.waits;
    return true;
  }
  if (desc_.rules != nullptr) {
    tiers_ = desc_.rules->view().tiers;
  }
  publish();
  desc_.ui->show(ShopHaggleDocument, true);
  open_ = true;
  // The rules once per job, as its first customer reaches the counter.
  if (desc_.rules != nullptr && desc_.job->customerIndex() == 0 && haggle->offers().empty() &&
      !haggle->talked()) {
    desc_.rules->show();
  }
  return true;
}

bool ShopHaggleScreen::handle(const ui::UiCommand& command) {
  if (!open_ || (command.name != OfferCommand && command.name != TalkCommand &&
                 command.name != DismissCommand)) {
    return false;
  }
  bool played = false;
  if (command.name == OfferCommand) {
    const double* level = std::get_if<double>(&command.value);
    if (level != nullptr && *level >= 0.0 && *level < static_cast<double>(shop_job::NoSale)) {
      played = desc_.job->play(session::ShopJobAction::Offer, static_cast<shop_job::Level>(*level));
    }
  } else if (command.name == TalkCommand) {
    played = desc_.job->play(session::ShopJobAction::Talk);
  } else {
    played = desc_.job->play(session::ShopJobAction::Dismiss);
  }
  if (played) {
    // The story plays the turn's lines with the counter out of the way.
    desc_.ui->hide(ShopHaggleDocument);
    if (desc_.rules != nullptr) {
      desc_.rules->close();
    }
    open_ = false;
    over_ = true;
  }
  return true;
}

void ShopHaggleScreen::update(session::StoryScenes& scenes) {
  if (!over_) {
    return;
  }
  over_ = false;
  if (desc_.job != nullptr) {
    if (auto told = desc_.job->finishTurn(); !told) {
      core::logWarning("{}", told.error().message);
    }
  }
  scenes.screenClosed();
}

void ShopHaggleScreen::refocus() {
  if (open_ && desc_.ui != nullptr) {
    desc_.ui->show(ShopHaggleDocument, true);
  }
}

// The species' sprite as the tournament board draws it (bracketSpriteCell, its first face).
void ShopHaggleScreen::speciesSprite(std::int32_t number, ShopHaggleNames& names) const {
  if (desc_.tournament == nullptr) {
    return;
  }
  const game::tournament::BracketLayout* layout = desc_.tournament->bracketLayout();
  const game::tournament::BracketRules* rules = desc_.tournament->bracketRules();
  if (layout == nullptr || rules == nullptr || rules->pages.empty()) {
    return;
  }
  const game::tournament::BracketCell cell =
      game::tournament::bracketSpriteCell(*layout, number, 0);
  const std::size_t page =
      std::min(static_cast<std::size_t>(std::max(cell.page, 0)), rules->pages.size() - 1);
  names.speciesIcon = std::format("/{}?palette={}", rules->pages.at(page), cell.palette);
  names.speciesIconRect = std::format("{} {} {} {}", cell.u, cell.v, cell.width, cell.height);
}

ShopHaggleView ShopHaggleScreen::view() const {
  ShopHaggleView view{.tiers = tiers_};
  if (desc_.story != nullptr) {
    view.tally = desc_.story->shopTally();
  }
  if (desc_.job == nullptr) {
    return view;
  }
  view.rules = desc_.job->rules();
  view.haggle = desc_.job->haggle();
  if (const shop_job::Lineup* line = desc_.job->lineup()) {
    view.perfectScore = line->perfectScore;
    view.customers = line->customers.size();
  }
  view.customer = desc_.job->customerIndex() + 1;
  if (view.rules == nullptr || view.haggle == nullptr) {
    return view;
  }
  const shop_job::Customer& customer = view.haggle->customer();
  const std::int32_t number = view.rules->species.at(customer.species).species;
  const screens::NameKeys names{desc_.species, desc_.items, nullptr};
  if (desc_.species != nullptr) {
    const auto row = std::ranges::find(desc_.species->digimon, number, &assets::DigimonRow::index);
    if (row != desc_.species->digimon.end()) {
      view.names.species = names.species(row->id);
    }
  }
  speciesSprite(number, view.names);
  if (desc_.items != nullptr) {
    if (const auto item = desc_.items->handleOf(view.rules->items.at(customer.item).id)) {
      view.names.item = names.item(*item);
      if (const auto icon = screens::itemIconOf(desc_.items->at(*item))) {
        view.names.itemIcon = icon->image;
        view.names.itemIconRect = icon->rect;
      }
    }
  }
  return view;
}

void ShopHaggleScreen::publish() const {
  screens::publish(*desc_.ui, shopHaggleSettings(view()), ShopHaggleRevisionSetting);
}

} // namespace dwe::app
