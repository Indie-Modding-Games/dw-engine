#include "shop_job_rules_panel.hpp"

#include <dwe/screens/bag_view.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/session/shop_job_adapter.hpp>
#include <dwe/session/story_scenes.hpp>

#include <optional>

namespace dwe::app {

namespace {

constexpr std::string_view CloseCommand = "shop_job_rules_close";

double number(std::int64_t value) {
  return static_cast<double>(value);
}

} // namespace

screens::ScreenSettings shopJobRulesSettings(const ShopJobRulesView& view) {
  return {
      {.name = "shop_job_rules.better_pay", .value = number(view.tiers.betterPay)},
      {.name = "shop_job_rules.joins", .value = number(view.tiers.joins)},
      {.name = "shop_job_rules.bonus", .value = number(view.tiers.bonus)},
      {.name = "shop_job_rules.bonus_item", .value = view.bonusItem},
      {.name = "shop_job_rules.bonus_icon", .value = view.bonusIcon},
      {.name = "shop_job_rules.bonus_icon_rect", .value = view.bonusIconRect},
      {.name = "shop_job_rules.customers", .value = number(view.customers)},
      {.name = "shop_job_rules.talk_needs", .value = static_cast<double>(view.talkNeeds)},
      {.name = "shop_job_rules.improved", .value = view.improved},
  };
}

ShopJobRulesView ShopJobRulesPanel::view() const {
  ShopJobRulesView view;
  if (desc_.scenes != nullptr) {
    if (const auto& story = desc_.scenes->rules().shopJob; story.has_value()) {
      view.tiers.betterPay = story->tallyFrom(story->betterPayFrom);
      view.tiers.joins = story->tallyFrom(story->joinsFrom);
      view.customers = story->customers;
    }
  }
  const game::shop_job::ShopJobRules* rules = desc_.job != nullptr ? desc_.job->rules() : nullptr;
  if (rules == nullptr) {
    return view;
  }
  view.improved = desc_.job->improved();
  view.talkNeeds = rules->talkNeeds;
  view.customers = static_cast<std::int64_t>(rules->customers());
  view.tiers.bonus = rules->bonusFrom;
  if (desc_.items != nullptr) {
    if (const std::optional<assets::ItemHandle> item = desc_.items->handleOf(rules->bonusItem)) {
      const screens::NameKeys names{nullptr, desc_.items, nullptr};
      view.bonusItem = names.item(*item);
      if (const auto icon = screens::itemIconOf(desc_.items->at(*item))) {
        view.bonusIcon = icon->image;
        view.bonusIconRect = icon->rect;
      }
    }
  }
  return view;
}

void ShopJobRulesPanel::show() {
  if (desc_.ui == nullptr) {
    return;
  }
  screens::publish(*desc_.ui, shopJobRulesSettings(view()), ShopJobRulesRevisionSetting);
  desc_.ui->show(ShopJobRulesDocument, true);
  shown_ = true;
}

void ShopJobRulesPanel::close() {
  if (!shown_ || desc_.ui == nullptr) {
    return;
  }
  desc_.ui->hide(ShopJobRulesDocument);
  shown_ = false;
  if (closed_) {
    closed_();
  }
}

void ShopJobRulesPanel::press(bool helpPressed, bool settingsPressed, bool canOpen) {
  if (shown_) {
    if (helpPressed || settingsPressed) {
      close();
    }
    return;
  }
  if (helpPressed && canOpen) {
    show();
  }
}

bool ShopJobRulesPanel::handle(const ui::UiCommand& command) {
  if (command.name == ShopJobRulesCommand) {
    show();
    return true;
  }
  if (command.name == CloseCommand) {
    close();
    return true;
  }
  return false;
}

} // namespace dwe::app
