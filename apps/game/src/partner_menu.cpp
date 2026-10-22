#include "partner_menu.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/inventory/inventory.hpp>
#include <dwe/game/inventory/use.hpp>
#include <dwe/screens/bag_view.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/screens/partner_status_view.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "bag_slots.hpp"
#include "messages.hpp"

namespace dwe::app {
namespace {

// The pause menu, which the partner menu replaces when it opens from it.
constexpr std::string_view PauseMenuDocument = "pause_menu";

// The keys of what a command on the bag did, with {name} the item's name.
constexpr std::string_view UsedKey = "partner_menu.bag_result.used";
constexpr std::string_view RefusedKey = "partner_menu.bag_result.refused";
constexpr std::string_view NotUsableHereKey = "partner_menu.bag_result.not_usable_here";
constexpr std::string_view NotInBagKey = "partner_menu.bag_result.not_in_bag";
constexpr std::string_view UselessKey = "partner_menu.bag_result.useless";
constexpr std::string_view MovedKey = "partner_menu.bag_result.moved";
constexpr std::string_view DroppedKey = "partner_menu.bag_result.dropped";
constexpr std::string_view SortedKey = "partner_menu.bag_result.sorted";
constexpr std::string_view FailedKey = "partner_menu.bag_result.failed";
// The item a bag result names, as the key of its name.
constexpr std::string_view BagResultItemSetting = "bag.result_item";

constexpr std::string_view PraisedKey = "partner_menu.care_result.praised";
constexpr std::string_view ScoldedKey = "partner_menu.care_result.scolded";

constexpr char MoveSeparator = ' ';

std::string_view keyOf(game::inventory::UseOutcome outcome) {
  switch (outcome) {
  case game::inventory::UseOutcome::Used:
    return UsedKey;
  case game::inventory::UseOutcome::Refused:
    return RefusedKey;
  case game::inventory::UseOutcome::NotUsableHere:
    return NotUsableHereKey;
  case game::inventory::UseOutcome::NotInBag:
    return NotInBagKey;
  case game::inventory::UseOutcome::Useless:
    return UselessKey;
  }
  return FailedKey;
}

std::optional<std::size_t> parseSlot(std::string_view text) {
  std::size_t slot = 0;
  const auto [rest, error] = std::from_chars(text.data(), text.data() + text.size(), slot);
  if (error != std::errc{} || rest != text.data() + text.size()) {
    return std::nullopt;
  }
  return slot;
}

} // namespace

core::Result<void> PartnerMenu::giveItem(std::string_view argument) {
  const std::size_t separator = argument.find(MoveSeparator);
  if (separator == std::string_view::npos) {
    return core::fail(core::formatMessage(messages::BadGiveItem, argument));
  }
  const std::string_view id = argument.substr(0, separator);
  const std::optional<std::size_t> count = parseSlot(argument.substr(separator + 1));
  if (!count || *count == 0) {
    return core::fail(core::formatMessage(messages::BadGiveItem, argument));
  }
  const std::optional<assets::ItemHandle> item = desc_.inventory->items().handleOf(id);
  if (!item) {
    return core::fail(core::formatMessage(messages::UnknownItem, id));
  }
  const game::inventory::AddResult added =
      desc_.inventory->inventory().add(*item, static_cast<std::int32_t>(*count));
  core::logInfo("{}", core::formatMessage(messages::GaveItem, id, *count, added.added));
  publish();
  return {};
}

core::Result<void> PartnerMenu::useItem(std::string_view id) {
  const std::optional<assets::ItemHandle> item = desc_.inventory->items().handleOf(id);
  if (!item) {
    return core::fail(core::formatMessage(messages::UnknownItem, id));
  }
  auto used = desc_.inventory->use(*desc_.session, *item);
  if (!used) {
    return std::unexpected{used.error()};
  }
  const std::string_view outcome = keyOf(used->outcome);
  core::logInfo("{}", core::formatMessage(messages::UsedItem, id, outcome));
  bagResult(outcome, item);
  return {};
}

void PartnerMenu::update() {
  if (desc_.ui->isVisible(PartnerMenuDocument)) {
    publish();
  }
}

bool PartnerMenu::handle(const ui::UiCommand& command) {
  const auto* number = std::get_if<double>(&command.value);
  if (command.name == OpenPartnerMenuCommand) {
    open();
  } else if (command.name == BagUseCommand && number != nullptr) {
    use(*number);
  } else if (command.name == BagMoveCommand && std::holds_alternative<std::string>(command.value)) {
    move(std::get<std::string>(command.value));
  } else if (command.name == BagDropCommand && number != nullptr) {
    drop(*number);
  } else if (command.name == BagSortCommand && number != nullptr) {
    sort(*number);
  } else if (command.name == PraiseCommand) {
    care(false);
  } else if (command.name == ScoldCommand) {
    care(true);
  } else {
    return false;
  }
  return true;
}

void PartnerMenu::open() {
  desc_.ui->hide(PauseMenuDocument);
  desc_.ui->setSetting(screens::BagResultSetting, std::string{});
  desc_.ui->setSetting(BagResultItemSetting, std::string{});
  desc_.ui->setSetting(CareResultSetting, std::string{});
  publish();
  desc_.ui->show(PartnerMenuDocument, true);
}

screens::ScreenSettings PartnerMenu::statusSettings() const {
  if (!desc_.session->playing()) {
    return {};
  }
  const screens::NameKeys names{desc_.species, &desc_.inventory->items(), nullptr};
  return screens::settingsOf(screens::partnerStatusViewOf(desc_.session->partnerView(), names));
}

void PartnerMenu::publish() const {
  if (!desc_.session->playing()) {
    return;
  }
  const screens::NameKeys names{desc_.species, &desc_.inventory->items(), nullptr};
  screens::publish(
      *desc_.ui,
      screens::settingsOf(screens::partnerStatusViewOf(desc_.session->partnerView(), names)),
      screens::PartnerRevisionSetting);
  screens::publish(*desc_.ui,
                   screens::settingsOf(screens::bagViewOf(desc_.inventory->inventory(),
                                                          desc_.inventory->items(), names)),
                   screens::BagRevisionSetting);
}

void PartnerMenu::bagResult(std::string_view key, std::optional<assets::ItemHandle> item) const {
  const screens::NameKeys names{desc_.species, &desc_.inventory->items(), nullptr};
  desc_.ui->setSetting(BagResultItemSetting, item ? names.item(*item) : std::string{});
  desc_.ui->setSetting(screens::BagResultSetting, std::string{key});
  publish();
}

void PartnerMenu::use(double value) {
  const std::optional<assets::ItemHandle> inSlot =
      itemAt(desc_.inventory->inventory().slots(), slotOf(value));
  if (!inSlot) {
    bagResult(NotInBagKey, std::nullopt);
    return;
  }
  const assets::ItemHandle item = *inSlot;
  auto used = desc_.inventory->use(*desc_.session, item);
  if (!used) {
    core::logWarning(
        "{}", core::formatMessage(messages::BagCommandFailed, BagUseCommand, used.error().message));
    bagResult(FailedKey, item);
    return;
  }
  bagResult(keyOf(used->outcome), item);
}

void PartnerMenu::move(const std::string& text) {
  const std::size_t separator = text.find(MoveSeparator);
  const std::optional<std::size_t> from =
      separator == std::string::npos ? std::nullopt
                                     : parseSlot(std::string_view{text}.substr(0, separator));
  const std::optional<std::size_t> to =
      separator == std::string::npos ? std::nullopt
                                     : parseSlot(std::string_view{text}.substr(separator + 1));
  if (!from || !to) {
    core::logWarning("{}", core::formatMessage(messages::BadBagMove, text));
    bagResult(FailedKey, std::nullopt);
    return;
  }
  const std::optional<assets::ItemHandle> item = itemAt(desc_.inventory->inventory().slots(), from);
  if (auto moved = desc_.inventory->inventory().move(*from, *to); !moved) {
    core::logWarning("{}", core::formatMessage(messages::BagCommandFailed, BagMoveCommand,
                                               moved.error().message));
    bagResult(FailedKey, item);
    return;
  }
  bagResult(MovedKey, item);
}

void PartnerMenu::drop(double value) {
  const std::optional<std::size_t> slot = slotOf(value);
  const std::optional<assets::ItemHandle> inSlot =
      itemAt(desc_.inventory->inventory().slots(), slot);
  if (!slot || !inSlot) {
    bagResult(NotInBagKey, std::nullopt);
    return;
  }
  const assets::ItemHandle item = *inSlot;
  if (auto dropped = desc_.inventory->inventory().discard(*slot, desc_.inventory->items(),
                                                          game::inventory::UseContext::Field);
      !dropped) {
    core::logWarning("{}", core::formatMessage(messages::BagCommandFailed, BagDropCommand,
                                               dropped.error().message));
    bagResult(FailedKey, item);
    return;
  }
  bagResult(DroppedKey, item);
}

void PartnerMenu::sort(double value) {
  const std::optional<std::size_t> mode = slotOf(value);
  if (!mode) {
    bagResult(FailedKey, std::nullopt);
    return;
  }
  if (auto sorted = desc_.inventory->inventory().sort(*mode, desc_.inventory->items()); !sorted) {
    core::logWarning("{}", core::formatMessage(messages::BagCommandFailed, BagSortCommand,
                                               sorted.error().message));
    bagResult(FailedKey, std::nullopt);
    return;
  }
  bagResult(SortedKey, std::nullopt);
}

void PartnerMenu::care(bool scold) {
  if (!desc_.session->playing()) {
    return;
  }
  if (scold) {
    desc_.session->scold();
  } else {
    desc_.session->praise();
  }
  desc_.ui->setSetting(CareResultSetting, std::string{scold ? ScoldedKey : PraisedKey});
  publish();
}

} // namespace dwe::app
