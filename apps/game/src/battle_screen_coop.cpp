// The battle screen's part in a co-op battle (docs/architecture/multiplayer.md, §6 decision 3): an
// engine addition, the original has no online play. The battle runs here, on the host; the other
// player's commands, cheers and items arrive from its own game.
#include <dwe/core/log.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/inventory_adapter.hpp>

#include <cmath>
#include <optional>
#include <string>
#include <string_view>

#include "battle_screen.hpp"
#include "field_scene.hpp"

namespace dwe::app {

namespace {

using game::battle::BattleKind;

game::battle::Point battlePointOf(glm::vec3 world) {
  return {.x = static_cast<std::int32_t>(std::lround(world.x)),
          .z = static_cast<std::int32_t>(std::lround(world.z))};
}

} // namespace

game::coop::BattlePhase BattleScreen::coopPhase() const {
  if (intro_) {
    return game::coop::BattlePhase::Intro;
  }
  if (battle_) {
    return game::coop::BattlePhase::Fight;
  }
  if (victoryPoseSeconds_ || matchEndSeconds_) {
    return game::coop::BattlePhase::Victory;
  }
  if (ending_) {
    return game::coop::BattlePhase::End;
  }
  return game::coop::BattlePhase::Closing;
}

void BattleScreen::addCoopAllies(session::BattleRequest& request) const {
  if (desc_.scene == nullptr || request.kind != BattleKind::Normal || request.timeLimit ||
      request.tamerless) {
    return;
  }
  // The enemies scale by the partners that fight and the players who help (an engine addition,
  // docs/architecture/coop-difficulty.md); one partner without helpers keeps the original's.
  request.coopScaling = coopScaling_;
  const game::field::Field& field = desc_.scene->field();
  for (const CoopAlly& ally : coopAllies_) {
    const auto player = field.player(ally.owner);
    const auto partner = field.partner(ally.owner);
    if (!player || !partner) {
      continue;
    }
    request.allies.push_back({.visitor = ally.visitor,
                              .partner = battlePointOf(partner->position),
                              .tamer = battlePointOf(player->position),
                              .owner = ally.owner});
    core::logInfo("coop: player {}'s partner joins the battle", ally.owner);
  }
}

std::optional<game::battle::FighterId> BattleScreen::fighterOf(game::coop::PlayerId owner) const {
  if (!battle_) {
    return std::nullopt;
  }
  for (const game::battle::AllyState& ally : battle_->state().allies) {
    if (ally.owner == owner) {
      return ally.id;
    }
  }
  // The fighting partner is the visitor's in a visitor's battle, this game's own otherwise.
  if (owner == (visiting_ ? visitorOwner_ : game::coop::HostPlayerId)) {
    return game::battle::PartnerId;
  }
  return std::nullopt;
}

void BattleScreen::releaseAlly(game::coop::PlayerId owner) {
  if (!battle_) {
    return;
  }
  for (const game::battle::AllyState& ally : battle_->state().allies) {
    if (ally.owner == owner) {
      battle_->releaseAlly(ally.id);
      core::logInfo("coop: player {}'s partner fights on its own call", owner);
    }
  }
}

std::optional<std::int32_t> BattleScreen::npcOfFighter(game::battle::FighterId id) const {
  if (id == game::battle::PartnerId || id > fighterNpcs_.size()) {
    return std::nullopt;
  }
  return fighterNpcs_[id - 1];
}

game::battle::FighterId BattleScreen::throwTargetOf(game::battle::FighterId thrower) const {
  if (battle_ && !battle_->state().fighters.at(thrower).gone()) {
    return thrower;
  }
  return game::battle::PartnerId;
}

std::optional<game::battle::FighterId>
BattleScreen::coopTargetOf(std::optional<game::battle::FighterId> target) const {
  if (!battle_ || !target) {
    return std::nullopt;
  }
  const game::battle::BattleState& state = battle_->state();
  if (*target >= state.fighters.size() || !state.isPlayerSide(*target) ||
      state.fighters.at(*target).gone()) {
    return std::nullopt;
  }
  return target;
}

bool BattleScreen::commandFromPlayer(game::coop::PlayerId owner, game::battle::Command command) {
  const std::optional<game::battle::FighterId> fighter = fighterOf(owner);
  if (!battle_ || intro_ || !fighter || (*fighter == game::battle::PartnerId && !visiting_)) {
    return false;
  }
  if (auto taken = battle_->command(command, *fighter); !taken) {
    core::logInfo("coop: player {}'s command {} was refused: {}", owner, static_cast<int>(command),
                  taken.error().message);
    return false;
  }
  if (*fighter == game::battle::PartnerId) {
    shout(command);
  }
  publish();
  return true;
}

bool BattleScreen::orderFromPlayer(game::coop::PlayerId owner, std::int32_t order) {
  const std::optional<game::battle::FighterId> fighter = fighterOf(owner);
  if (!battle_ || intro_ || !fighter || (*fighter == game::battle::PartnerId && !visiting_)) {
    return false;
  }
  const auto slot = static_cast<std::size_t>(order % game::coop::OrderSlots);
  const std::int32_t target = (order / game::coop::OrderSlots) - 1;
  if (auto taken = battle_->order(
          slot,
          target < 0 ? std::nullopt
                     : std::optional<game::battle::FighterId>{static_cast<game::battle::FighterId>(
                           target)},
          *fighter);
      !taken) {
    core::logInfo("coop: player {}'s order {} was refused: {}", owner, order,
                  taken.error().message);
    return false;
  }
  publish();
  return true;
}

bool BattleScreen::stanceFromPlayer(game::coop::PlayerId owner, game::battle::Stance stance) {
  const std::optional<game::battle::FighterId> fighter = fighterOf(owner);
  if (!battle_ || intro_ || !fighter || (*fighter == game::battle::PartnerId && !visiting_)) {
    return false;
  }
  if (auto taken = battle_->setStance(stance, *fighter); !taken) {
    return false;
  }
  publish();
  return true;
}

void BattleScreen::cheer(game::coop::PlayerId from) {
  if (!battle_ || intro_) {
    return;
  }
  const game::battle::FinisherState& finisher = battle_->state().finisher;
  if (finisher.charging && fighterOf(from) == finisher.fighter) {
    battle_->pressFinisherCharge();
  } else {
    battle_->cheerFinisher();
  }
}

bool BattleScreen::throwLentItem(game::coop::PlayerId from, std::string_view itemId,
                                 std::optional<game::battle::FighterId> target) {
  if (!battle_ || intro_ || desc_.inventory == nullptr) {
    return false;
  }
  const std::optional<assets::ItemHandle> item = desc_.inventory->items().handleOf(itemId);
  if (!item) {
    core::logInfo("coop: the other player's item {} is not in this game's items", itemId);
    return false;
  }
  game::inventory::Inventory& bag = desc_.inventory->inventory();
  if (!bag.add(*item, 1).taken()) {
    core::logInfo("coop: no room to lend the other player's item {}", itemId);
    return false;
  }
  // A player with a partner of its own in the battle throws from where it stands, at it or at the
  // partner it picked; a helper throws as this game's player does (#363), at the one it picked.
  const std::optional<game::battle::FighterId> own = fighterOf(from);
  const game::battle::FighterId thrower = own.value_or(game::battle::PartnerId);
  const std::optional<game::battle::FighterId> picked = coopTargetOf(target);
  auto thrown = own || picked ? desc_.battle->throwItem(*battle_, *item, thrower,
                                                        picked.value_or(throwTargetOf(thrower)))
                              : desc_.battle->throwItem(*battle_, *item);
  if (!thrown || thrown->outcome != game::inventory::UseOutcome::Used) {
    bag.remove(*item, 1);
    core::logInfo("coop: the other player's item {} was not thrown{}", itemId,
                  thrown ? std::string{} : ": " + thrown.error().message);
    return false;
  }
  core::logInfo("coop: player {} threw {}", from, itemId);
  // Its body throws it where this game draws one; the fighting partner's thrower is this game's
  // own player in a helper's throw (#363).
  startThrow(*item, thrower, own ? std::optional<game::coop::PlayerId>{from} : std::nullopt);
  publish();
  return true;
}

} // namespace dwe::app
