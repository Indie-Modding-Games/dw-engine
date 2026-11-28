#include "coop_battle_view.hpp"

#include <dwe/game/battle/text_keys.hpp>
#include <dwe/session/session_sound.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <map>

namespace dwe::app {

namespace {

constexpr std::int64_t PartnerSoundSlot = 4;

namespace coop = game::coop;

screens::FighterView fighterViewOf(const CoopBattleViewSource& source, std::size_t index,
                                   const screens::NameKeys& names) {
  screens::FighterView view;
  view.id = index;
  if (index < source.begin->species.size()) {
    view.nameKey = names.species(source.begin->species[index]);
  }
  if (index < source.fighters.size()) {
    const coop::BattleFighterState& fighter = source.fighters[index];
    view.hp = fighter.hp;
    view.maxHp = fighter.maxHp;
    view.mp = fighter.mp;
    view.maxMp = fighter.maxMp;
    view.fainted = fighter.down;
    if (fighter.reviveSeconds > 0) {
      view.reviveSeconds = fighter.reviveSeconds;
    }
  }
  return view;
}

screens::CommandEntry commandEntryOf(const CoopBattleViewSource& source, unsigned number,
                                     const screens::NameKeys& names) {
  const auto command = static_cast<game::battle::Command>(number);
  screens::CommandEntry entry;
  entry.command = command;
  entry.labelKey = coopOrderLabelKey(command, source.begin->folder);
  entry.current = source.frame->command == number;
  if (source.partner != nullptr && source.moves != nullptr) {
    if (std::string label = screens::commandLabelOf(command, *source.partner, names);
        !label.empty() && command >= game::battle::Command::Technique1) {
      entry.labelKey = std::move(label);
    }
    entry.icon = screens::commandIconOf(command, *source.partner, *source.moves);
  } else if (command < game::battle::Command::Technique1) {
    entry.icon = std::format("command-{}", number);
  }
  return entry;
}

} // namespace

std::string coopOrderLabelKey(game::battle::Command command, bool folder) {
  if (folder &&
      game::battle::folderCommandTextKey(command) != game::battle::commandTextKey(command)) {
    return std::string{game::battle::folderCommandTextKey(command)};
  }
  if (std::string key = screens::commandLabelKey(command); !key.empty()) {
    return key;
  }
  switch (command) {
  case game::battle::Command::Technique1:
    return "battle.command.technique_1";
  case game::battle::Command::Technique2:
    return "battle.command.technique_2";
  case game::battle::Command::Technique3:
    return "battle.command.technique_3";
  case game::battle::Command::Finisher:
    return "battle.command.finisher";
  default:
    return "battle.command.none";
  }
}

std::size_t coopOwnFighterOf(const coop::BattleBegin& begin, coop::BattleRole role,
                             coop::PlayerId self) {
  if (role == coop::BattleRole::Ally) {
    if (const auto ally = std::ranges::find(begin.allies, self); ally != begin.allies.end()) {
      return 1 + begin.enemyNpcs.size() +
             static_cast<std::size_t>(std::distance(begin.allies.begin(), ally));
    }
  }
  return 0;
}

screens::BattleView coopBattleViewOf(const CoopBattleViewSource& source,
                                     const screens::NameKeys& names) {
  const coop::BattleBegin& begin = *source.begin;
  const std::size_t enemies = begin.enemyNpcs.size();
  const std::size_t own = coopOwnFighterOf(begin, source.role, source.self);
  screens::BattleView view;
  view.fighters.push_back(fighterViewOf(source, own, names));
  std::map<std::string, int> sameSpecies;
  for (std::size_t enemy = 1; enemy <= enemies && enemy < begin.species.size(); ++enemy) {
    ++sameSpecies[begin.species[enemy]];
  }
  std::map<std::string, char> nextTag;
  for (std::size_t enemy = 1; enemy <= enemies; ++enemy) {
    screens::FighterView fighter = fighterViewOf(source, enemy, names);
    if (enemy < begin.species.size() && sameSpecies[begin.species[enemy]] > 1) {
      char& tag = nextTag.try_emplace(begin.species[enemy], 'A').first->second;
      fighter.tag = std::string(1, tag++);
    }
    view.fighters.push_back(std::move(fighter));
  }
  for (std::size_t index = 0; index < 1 + enemies + begin.allies.size(); ++index) {
    if (index != own && (index == 0 || index > enemies)) {
      view.allies.push_back(fighterViewOf(source, index, names));
    }
  }
  view.reviveSeconds = view.fighters.front().reviveSeconds;
  view.bagOpen = source.bagOpen;
  if (const coop::BattleFrame* frame = source.frame) {
    view.bagAvailable = frame->bagAvailable;
    view.finisher.charging = frame->finisherCharging;
    if (source.role != coop::BattleRole::Helper) {
      for (unsigned number = 1; number <= static_cast<unsigned>(game::battle::Command::Finisher);
           ++number) {
        if ((frame->menu & (1U << number)) != 0) {
          view.menu.push_back(commandEntryOf(source, number, names));
        }
      }
    }
  }
  return view;
}

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
