#include "battle_screen.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/battle/text_keys.hpp>
#include <dwe/game/inventory/use.hpp>
#include <dwe/screens/bag_view.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "bag_slots.hpp"
#include "messages.hpp"

namespace dwe::app {

core::Result<void> BattleScreen::start(session::BattleRequest request) {
    if (active()) {
        return core::fail(std::string{messages::BattleAlreadyRunning});
    }
    std::string enemies;
    for (const game::battle::Combatant& enemy : request.enemies) {
        enemies += enemies.empty() ? enemy.species : std::string{messages::ListSeparator} + enemy.species;
    }
    const std::string map = request.map;
    auto battle = desc_.battle->start(*desc_.session, std::move(request));
    if (!battle) {
        return std::unexpected{battle.error()};
    }
    battle_.emplace(std::move(*battle));
    core::logInfo("{}", core::formatMessage(messages::BattleStarted, enemies, map));
    desc_.ui->setSetting(BattlePhaseSetting, std::string{FightPhase});
    publish();
    desc_.ui->show(BattleDocument, true);
    return {};
}

void BattleScreen::tick(double seconds) {
    if (!battle_) {
        return;
    }
    battle_->advance(seconds);
    publish();
    if (battle_->finished()) {
        finish();
    }
}

bool BattleScreen::handle(const ui::UiCommand& command) {
    if (command.name == BattleCommandName) {
        const auto* number = std::get_if<double>(&command.value);
        if (battle_ && number != nullptr) {
            const auto chosen = static_cast<game::battle::Command>(static_cast<int>(*number));
            if (auto taken = battle_->command(chosen); !taken) {
                core::logInfo("{}", core::formatMessage(messages::BattleCommandRefused,
                                                        game::battle::commandTextKey(chosen), taken.error().message));
            }
        }
        return true;
    }
    if (command.name == BattleChargeCommand) {
        if (battle_) {
            battle_->pressFinisherCharge();
        }
        return true;
    }
    if (command.name == BattleBagOpenCommand) {
        openBag();
        return true;
    }
    if (command.name == BattleBagCloseCommand) {
        if (battle_) {
            battle_->closeBag();
            publish();
        }
        return true;
    }
    if (command.name == BattleThrowCommand) {
        if (const auto* number = std::get_if<double>(&command.value); number != nullptr) {
            throwItem(*number);
        }
        return true;
    }
    if (command.name == BattleCloseCommand) {
        close();
        return true;
    }
    return false;
}

screens::NameKeys BattleScreen::names() const {
    return {desc_.species, desc_.inventory != nullptr ? &desc_.inventory->items() : nullptr, &desc_.battle->catalog()};
}

void BattleScreen::publish() const {
    if (!battle_) {
        return;
    }
    screens::publish(*desc_.ui, screens::settingsOf(screens::battleViewOf(*battle_, names())),
                     screens::BattleRevisionSetting);
}

void BattleScreen::openBag() {
    if (!battle_) {
        return;
    }
    if (auto opened = battle_->openBag(); !opened) {
        core::logInfo("{}", core::formatMessage(messages::BattleBagRefused, opened.error().message));
        return;
    }
    // inventory.c: selectOptionMenu: in battle only the items of a battle colour can be thrown.
    screens::publish(*desc_.ui,
                     screens::settingsOf(screens::bagViewOf(desc_.inventory->inventory(), desc_.inventory->items(),
                                                            names(), game::inventory::UseContext::Battle)),
                     screens::BagRevisionSetting);
    publish();
}

void BattleScreen::throwItem(double slot) {
    if (!battle_) {
        return;
    }
    const std::optional<std::int32_t> item = itemAt(desc_.inventory->inventory().slots(), slotOf(slot));
    if (!item) {
        core::logInfo("{}", core::formatMessage(messages::BattleThrowRefused, slot, messages::EmptyBagSlot));
        return;
    }
    auto thrown = desc_.battle->throwItem(*battle_, *item);
    if (!thrown) {
        core::logInfo("{}", core::formatMessage(messages::BattleThrowRefused, slot, thrown.error().message));
    } else if (thrown->outcome != game::inventory::UseOutcome::Used) {
        core::logInfo("{}", core::formatMessage(messages::BattleThrowRefused, slot, messages::ItemNotThrowable));
    } else {
        core::logInfo("{}", core::formatMessage(messages::BattleItemThrown, *item));
    }
    publish();
}

void BattleScreen::finish() {
    const std::optional<game::battle::BattleResult> finished = battle_ ? battle_->result() : std::nullopt;
    battle_.reset();
    if (!finished) {
        return;
    }
    const game::battle::BattleResult& result = *finished;
    core::logInfo("{}", core::formatMessage(messages::BattleFinished, game::battle::outcomeTextKey(result.outcome)));
    auto aftermath = desc_.battle->finish(*desc_.session, result);
    if (!aftermath) {
        core::logError("{}", core::formatMessage(messages::BattleStartFailed, aftermath.error().message));
        aftermath = session::BattleAftermath{};
    }
    screens::publish(*desc_.ui, screens::settingsOf(screens::battleEndViewOf(result, *aftermath, names())),
                     screens::BattleRevisionSetting);
    desc_.ui->setSetting(BattlePhaseSetting, std::string{EndPhase});
    ending_ = true;
}

void BattleScreen::close() {
    if (!ending_) {
        return;
    }
    ending_ = false;
    desc_.ui->hide(BattleDocument);
}

} // namespace dwe::app
