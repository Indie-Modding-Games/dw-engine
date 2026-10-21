#include "battle_screen.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/battle/text_keys.hpp>

#include <optional>
#include <string>
#include <utility>
#include <variant>

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
    screens::publish(*desc_.ui, screens::settingsOf(screens::battleViewOf(*battle_, names())),
                     screens::BattleRevisionSetting);
    desc_.ui->show(BattleDocument, true);
    return {};
}

void BattleScreen::tick(double seconds) {
    if (!battle_) {
        return;
    }
    battle_->advance(seconds);
    screens::publish(*desc_.ui, screens::settingsOf(screens::battleViewOf(*battle_, names())),
                     screens::BattleRevisionSetting);
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
    if (command.name == BattleCloseCommand) {
        close();
        return true;
    }
    return false;
}

screens::NameKeys BattleScreen::names() const {
    return {desc_.species, desc_.inventory != nullptr ? &desc_.inventory->items() : nullptr, &desc_.battle->catalog()};
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
