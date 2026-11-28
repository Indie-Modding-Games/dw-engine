#include "coop_battle_view.hpp"

#include <dwe/session/session_sound.hpp>

#include <cstddef>

namespace dwe::app {

namespace {

constexpr std::int64_t PartnerSoundSlot = 4;

} // namespace

std::int64_t CoopBattleSounds::slotOf(std::int16_t figureSlot) const {
  const auto found = slots.find(figureSlot);
  return found != slots.end() ? found->second : PartnerSoundSlot;
}

CoopBattleSounds coopBattleSoundsOf(const game::coop::BattleBegin& begin,
                                    game::coop::PlayerId self) {
  CoopBattleSounds sounds;
  const auto speciesAt = [&begin](std::size_t fighter) {
    return fighter < begin.species.size() ? begin.species[fighter] : std::string{};
  };
  for (std::size_t enemy = 0; enemy < begin.enemyNpcs.size(); ++enemy) {
    sounds.enemies.push_back(speciesAt(enemy + 1));
    sounds.slots[begin.enemyNpcs[enemy]] = PartnerSoundSlot + 1 + static_cast<std::int64_t>(enemy);
  }
  const auto other = [&](std::int16_t figureSlot, std::size_t fighter) {
    if (sounds.partners.size() >= session::AllySoundSlots) {
      return;
    }
    sounds.slots[figureSlot] =
        session::FirstAllySoundSlot + static_cast<std::int64_t>(sounds.partners.size());
    sounds.partners.push_back(speciesAt(fighter));
  };
  if (begin.starter != self) {
    other(game::coop::PartnerFigureSlot, 0);
  }
  for (std::size_t ally = 0; ally < begin.allies.size(); ++ally) {
    if (begin.allies[ally] != self) {
      other(game::coop::allyFigureSlot(begin.allies[ally]), 1 + begin.enemyNpcs.size() + ally);
    }
  }
  return sounds;
}

CoopThrowFollower::Step CoopThrowFollower::follow(std::span<const game::coop::BattleThrow> throws) {
  Step step;
  for (const game::coop::BattleThrow& thrown : throws) {
    const bool landed = thrown.stage == game::coop::BattleThrowStage::Landed;
    if (started_.insert(thrown.serial).second && !landed) {
      step.started.push_back(thrown.thrower);
    }
    if (landed) {
      if (landed_.insert(thrown.serial).second) {
        step.landed.push_back(thrown);
      }
    } else if (!landed_.contains(thrown.serial)) {
      step.shown = thrown;
    }
  }
  return step;
}

void CoopThrowFollower::reset() {
  started_.clear();
  landed_.clear();
}

std::vector<std::string> CoopThrowPurse::due(std::span<const game::coop::BattleThrow> throws,
                                             game::coop::PlayerId self, std::uint32_t frame) {
  std::vector<std::string> items;
  for (const game::coop::BattleThrow& thrown : throws) {
    if (thrown.thrower == self && paid_.insert(thrown.serial).second) {
      items.push_back(thrown.item);
      asked_.reset();
    }
  }
  if (asked_ && frame - *asked_ > WaitFrames) {
    asked_.reset();
  }
  return items;
}

void CoopThrowPurse::reset() {
  asked_.reset();
  paid_.clear();
}

} // namespace dwe::app
