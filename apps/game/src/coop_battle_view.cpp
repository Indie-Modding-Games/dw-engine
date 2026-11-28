#include "coop_battle_view.hpp"

#include <dwe/game/battle/text_keys.hpp>
#include <dwe/screens/bag_view.hpp>
#include <dwe/session/session_sound.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include <variant>

#include "bag_slots.hpp"
#include "battle_screen.hpp"
#include "partner_menu.hpp"

namespace dwe::app {

namespace {

constexpr std::int64_t PartnerSoundSlot = 4;
constexpr std::int32_t Hundred = 100;

namespace coop = game::coop;
using game::battle::Command;
using game::battle::FighterId;

static_assert(coop::LastOrderPhase ==
              static_cast<std::uint8_t>(game::battle::OrderPhase::Recovering));
static_assert(coop::LastOrderEnd == static_cast<std::uint8_t>(game::battle::OrderEnd::Replaced));
static_assert(coop::LastOrderProblem ==
              static_cast<std::uint8_t>(game::battle::OrderProblem::ByTrainer));
static_assert(coop::LastStance == static_cast<std::int32_t>(game::battle::Stance::Guard));
static_assert(coop::MaxBattleQueue == game::battle::OrderQueueSize);
static_assert(coop::LastBattleStatus ==
              static_cast<std::uint8_t>(game::battle::StatusEffect::Flatten));
static_assert(std::tuple_size_v<coop::BattleMoveIds> == game::battle::EquippedSlots);

template <typename T>
T clampedTo(std::int64_t value) {
  return static_cast<T>(std::clamp<std::int64_t>(value, 0, std::numeric_limits<T>::max()));
}

std::optional<std::uint8_t> fighterByteOf(std::optional<FighterId> fighter) {
  return fighter && *fighter < coop::MaxBattleFighters
             ? std::optional{static_cast<std::uint8_t>(*fighter)}
             : std::nullopt;
}

coop::BattleOrderShown orderShownOf(const game::battle::Order& order) {
  return {.slot = static_cast<std::uint8_t>(order.slot), .target = fighterByteOf(order.target)};
}

// Mirrors screens' battleViewOf.
void addOrderOf(coop::BattleFighterState& shown, const game::battle::Battle& battle, FighterId id) {
  const game::battle::BattleState& state = battle.state();
  if (!state.isPlayerSide(id)) {
    return;
  }
  if (const game::battle::FolderFighter* folder = battle.folderOf(id)) {
    shown.order = folder->active ? static_cast<std::uint8_t>(folder->active->slot + 1) : 0;
    shown.stance = static_cast<std::uint8_t>(folder->effectiveStance());
  } else if (state.commanded(id)) {
    shown.order = static_cast<std::uint8_t>(state.commandsOf(id).current);
  }
}

coop::BattleFolderPlate plateOf(const game::battle::Battle& battle,
                                const game::battle::FolderFighter& folder, FighterId own) {
  coop::BattleFolderPlate plate;
  if (folder.active) {
    plate.active = orderShownOf(*folder.active);
    plate.phase = static_cast<std::uint8_t>(folder.phase);
    if (folder.phaseLength > 0) {
      plate.phasePercent = clampedTo<std::uint8_t>(std::min(
          (folder.phaseLength - folder.phaseFrames) * Hundred / folder.phaseLength, Hundred));
    }
    plate.committed = folder.committed;
  }
  for (const game::battle::Order& order : folder.queue) {
    if (plate.queue.size() < coop::MaxBattleQueue) {
      plate.queue.push_back(orderShownOf(order));
    }
  }
  plate.target = fighterByteOf(battle.state().fighters[own].target);
  plate.stance = static_cast<std::uint8_t>(folder.effectiveStance());
  plate.bar = clampedTo<std::uint16_t>(folder.defenseBar);
  plate.barMax = clampedTo<std::uint16_t>(battle.defenseBarCapacity(own));
  plate.brokenSeconds = clampedTo<std::uint8_t>(
      (folder.brokenFrames + game::battle::FramesPerSecond - 1) / game::battle::FramesPerSecond);
  if (const auto& notice = folder.notice) {
    plate.notice = coop::BattleOrderNotice{
        .slot = static_cast<std::uint8_t>(notice->order.slot),
        .end = static_cast<std::uint8_t>(notice->end),
        .problem = static_cast<std::uint8_t>(notice->problem),
        .by =
            notice->by ? std::optional{static_cast<std::uint8_t>(notice->by->slot)} : std::nullopt};
  }
  return plate;
}

std::vector<std::size_t> allyRowsOf(const coop::BattleBegin& begin, std::size_t own) {
  const std::size_t enemies = begin.enemyNpcs.size();
  std::vector<std::size_t> rows;
  for (std::size_t index = 0; index < 1 + enemies + begin.allies.size(); ++index) {
    if (index != own && (index == 0 || index > enemies)) {
      rows.push_back(index);
    }
  }
  return rows;
}

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
    view.hpBarShown = fighter.hpBarShown;
    if (fighter.reviveSeconds > 0) {
      view.reviveSeconds = fighter.reviveSeconds;
    }
    if (const auto status = static_cast<game::battle::StatusEffect>(fighter.status);
        status != game::battle::StatusEffect::None) {
      view.statusKey = std::string{game::battle::statusTextKey(status)};
    }
  }
  return view;
}

std::string moveKeyOf(const CoopBattleViewSource& source, std::size_t fighter, std::size_t slot,
                      const screens::NameKeys& names) {
  const std::vector<coop::BattleMoveIds>& moves = source.begin->moves;
  if (fighter >= moves.size() || slot >= moves[fighter].size() || moves[fighter][slot].empty()) {
    return {};
  }
  return names.move(moves[fighter][slot]);
}

// Mirrors screens' allyOrderKeyOf.
std::string allyOrderKeyOf(const CoopBattleViewSource& source, std::size_t index,
                           const screens::NameKeys& names) {
  if (index >= source.fighters.size()) {
    return {};
  }
  const coop::BattleFighterState& fighter = source.fighters[index];
  if (source.begin->folder) {
    return fighter.order > 0 ? moveKeyOf(source, index, fighter.order - 1U, names)
                             : std::format("battle.stance.{}",
                                           screens::stanceName(
                                               static_cast<game::battle::Stance>(fighter.stance)));
  }
  const auto command = static_cast<Command>(fighter.order);
  if (command == Command::None) {
    return {};
  }
  if (command < Command::Technique1) {
    return screens::commandLabelKey(command);
  }
  return moveKeyOf(source, index,
                   command == Command::Finisher ? game::battle::FinisherSlot
                                                : static_cast<std::size_t>(fighter.order) -
                                                      static_cast<std::size_t>(Command::Technique1),
                   names);
}

screens::CommandEntry commandEntryOf(const CoopBattleViewSource& source, unsigned number,
                                     const screens::NameKeys& names) {
  const auto command = static_cast<Command>(number);
  screens::CommandEntry entry;
  entry.command = command;
  entry.labelKey = coopOrderLabelKey(command, source.begin->folder);
  entry.current = source.frame->command == number;
  if (source.partner != nullptr && source.moves != nullptr) {
    if (std::string label = screens::commandLabelOf(command, *source.partner, names);
        !label.empty() && command >= Command::Technique1) {
      entry.labelKey = std::move(label);
    }
    entry.icon = screens::commandIconOf(command, *source.partner, *source.moves);
  } else if (command < Command::Technique1) {
    entry.icon = std::format("command-{}", number);
  }
  return entry;
}

// Mirrors screens' battleViewOf for the guest's own partner's Folder plate.
void addFolderOf(screens::BattleView& view, const CoopBattleViewSource& source,
                 const coop::BattleFolderPlate& plate, std::size_t own,
                 const screens::NameKeys& names) {
  const coop::BattleBegin& begin = *source.begin;
  const auto foe = [&](std::optional<std::uint8_t> target) -> const screens::FighterView* {
    if (!target || *target == 0 || *target > begin.enemyNpcs.size()) {
      return nullptr;
    }
    const auto found =
        std::ranges::find(view.fighters, std::size_t{*target}, &screens::FighterView::id);
    return found != view.fighters.end() ? &*found : nullptr;
  };
  const auto foeKey = [&](std::optional<std::uint8_t> target) {
    const screens::FighterView* fighter = foe(target);
    return fighter != nullptr ? fighter->nameKey : std::string{};
  };
  const auto foeTag = [&](std::optional<std::uint8_t> target) {
    const screens::FighterView* fighter = foe(target);
    return fighter != nullptr ? fighter->tag : std::string{};
  };
  const auto moveOf = [&](std::uint8_t slot) { return moveKeyOf(source, own, slot, names); };
  screens::BattleView::Folder folder;
  if (plate.active) {
    folder.moveKey = moveOf(plate.active->slot);
    folder.phase =
        std::string{screens::orderPhaseName(static_cast<game::battle::OrderPhase>(plate.phase))};
    folder.phasePercent = plate.phasePercent;
    folder.activeTargetKey = foeKey(plate.active->target);
    folder.activeTargetTag = foeTag(plate.active->target);
    folder.committed = plate.committed;
  }
  for (const coop::BattleOrderShown& order : plate.queue) {
    folder.queue.push_back(moveOf(order.slot));
    folder.queueTargets.push_back(foeKey(order.target));
    folder.queueTargetTags.push_back(foeTag(order.target));
  }
  folder.targetKey = foeKey(plate.target);
  folder.targetTag = foeTag(plate.target);
  folder.stance = std::string{screens::stanceName(static_cast<game::battle::Stance>(plate.stance))};
  folder.bar = plate.bar;
  folder.barMax = plate.barMax;
  folder.broken = plate.brokenSeconds > 0;
  folder.brokenSeconds = plate.brokenSeconds;
  if (const auto& notice = plate.notice) {
    const auto problem = static_cast<game::battle::OrderProblem>(notice->problem);
    folder.noticeKey =
        screens::orderEndKey(static_cast<game::battle::OrderEnd>(notice->end), problem);
    folder.noticeMoveKey = moveOf(notice->slot);
    folder.noticeReasonKey = screens::orderProblemKey(problem);
    if (notice->by) {
      folder.noticeByKey = moveOf(*notice->by);
    }
  }
  view.folder = std::move(folder);
  const auto aims = [](const coop::BattleOrderShown& order, std::size_t id) {
    return order.target == id;
  };
  for (screens::FighterView& fighter : view.fighters) {
    fighter.targeted = plate.target == fighter.id && fighter.id != own;
    fighter.queued = fighter.id != own &&
                     ((plate.active && aims(*plate.active, fighter.id)) ||
                      std::ranges::any_of(plate.queue, [&](const coop::BattleOrderShown& order) {
                        return aims(order, fighter.id);
                      }));
  }
}

} // namespace

std::string coopOrderLabelKey(Command command, bool folder) {
  if (folder &&
      game::battle::folderCommandTextKey(command) != game::battle::commandTextKey(command)) {
    return std::string{game::battle::folderCommandTextKey(command)};
  }
  if (std::string key = screens::commandLabelKey(command); !key.empty()) {
    return key;
  }
  switch (command) {
  case Command::Technique1:
    return "battle.command.technique_1";
  case Command::Technique2:
    return "battle.command.technique_2";
  case Command::Technique3:
    return "battle.command.technique_3";
  case Command::Finisher:
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

std::vector<coop::BattleMoveIds> coopBattleMovesOf(const game::battle::Battle& battle) {
  const game::battle::BattleState& state = battle.state();
  std::vector<coop::BattleMoveIds> moves;
  for (FighterId id = 0; id < state.fighters.size() && moves.size() < coop::MaxBattleFighters;
       ++id) {
    coop::BattleMoveIds& ids = moves.emplace_back();
    if (!state.isPlayerSide(id)) {
      continue;
    }
    for (std::size_t slot = 0; slot < ids.size(); ++slot) {
      if (const auto& move = state.fighters[id].moves.at(slot)) {
        ids.at(slot) = battle.moves().at(*move).id;
      }
    }
  }
  return moves;
}

coop::BattleFrame coopBattleFrameOf(const game::battle::Battle& battle, FighterId own) {
  const game::battle::BattleState& state = battle.state();
  coop::BattleFrame frame;
  const game::battle::CommandState& commands = state.commandsOf(own);
  frame.command = static_cast<std::uint8_t>(commands.current);
  for (const Command command : commands.menu) {
    frame.menu.push_back(static_cast<std::uint8_t>(command));
  }
  frame.bagAvailable = battle.bagAvailableFor(own);
  frame.finisherCharging = state.finisher.charging;
  if (const game::battle::Fighter& partner = state.fighters[own]; partner.hasFinisher()) {
    frame.finisherProgress = clampedTo<std::uint16_t>(partner.finisherProgress);
    frame.finisherGoal = clampedTo<std::uint16_t>(partner.finisherGoal);
  }
  if (const game::battle::FolderFighter* folder = battle.folderOf(own)) {
    frame.folder = plateOf(battle, *folder, own);
  }
  for (FighterId id = 0;
       id < state.fighters.size() && frame.fighters.size() < coop::MaxBattleFighters; ++id) {
    const game::battle::Fighter& fighter = state.fighters[id];
    const std::optional<std::int32_t> revive = screens::reviveSecondsOf(battle, id);
    coop::BattleFighterState& shown = frame.fighters.emplace_back(
        coop::BattleFighterState{.hp = fighter.hp,
                                 .mp = fighter.mp,
                                 .maxHp = fighter.stats.maxHp,
                                 .maxMp = fighter.stats.maxMp,
                                 .down = fighter.hp <= 0,
                                 .reviveSeconds = clampedTo<std::uint8_t>(revive.value_or(0))});
    shown.status = static_cast<std::uint8_t>(screens::shownStatusOf(fighter.status));
    shown.hpBarShown = battle.hpBarShown(id);
    addOrderOf(shown, battle, id);
  }
  return frame;
}

std::optional<std::size_t> coopNextThrowTarget(const coop::BattleBegin& begin,
                                               coop::BattleRole role, coop::PlayerId self,
                                               std::optional<std::size_t> current, int step) {
  const std::vector<std::size_t> rows = allyRowsOf(begin, coopOwnFighterOf(begin, role, self));
  const auto count = static_cast<std::ptrdiff_t>(rows.size()) + 1;
  std::ptrdiff_t at = 0;
  if (current) {
    if (const auto found = std::ranges::find(rows, *current); found != rows.end()) {
      at = std::distance(rows.begin(), found) + 1;
    }
  }
  at = (((at + step) % count) + count) % count;
  return at == 0 ? std::nullopt : std::optional{rows[static_cast<std::size_t>(at - 1)]};
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
  for (const std::size_t index : allyRowsOf(begin, own)) {
    screens::FighterView& ally = view.allies.emplace_back(fighterViewOf(source, index, names));
    ally.orderKey = allyOrderKeyOf(source, index, names);
    ally.throwTarget = source.throwTarget == index;
  }
  view.reviveSeconds = view.fighters.front().reviveSeconds;
  view.bagOpen = source.bagOpen;
  if (const coop::BattleFrame* frame = source.frame) {
    view.bagAvailable = frame->bagAvailable;
    view.finisher.progress = frame->finisherProgress;
    view.finisher.goal = frame->finisherGoal;
    view.finisher.ready = frame->finisherGoal > 0 && frame->finisherProgress >= frame->finisherGoal;
    view.finisher.charging = frame->finisherCharging;
    if (source.role != coop::BattleRole::Helper) {
      for (const std::uint8_t number : frame->menu) {
        screens::CommandEntry& entry =
            view.menu.emplace_back(commandEntryOf(source, number, names));
        entry.available = entry.command != Command::Finisher || view.finisher.ready;
      }
    }
    if (frame->folder) {
      addFolderOf(view, source, *frame->folder, own, names);
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

std::optional<std::string_view> coopHudPhaseOf(coop::BattlePhase phase) {
  switch (phase) {
  case coop::BattlePhase::Intro:
    return IntroPhase;
  case coop::BattlePhase::Fight:
    return FightPhase;
  case coop::BattlePhase::Victory:
    return VictoryPhase;
  case coop::BattlePhase::End:
  case coop::BattlePhase::Closing:
    break;
  }
  return std::nullopt;
}

void showCoopHudBag(ui::Ui& ui, const screens::ScreenSettings& bag,
                    const screens::BattleView& hud) {
  screens::publish(ui, bag, screens::BagRevisionSetting);
  screens::publish(ui, screens::settingsOf(hud), screens::BattleRevisionSetting);
  ui.setSetting(screens::BagResultSetting, std::string{});
  ui.setSetting(PartnerMenuStartPageSetting, std::string{PartnerMenuItemsPage});
  ui.setSetting(PartnerMenuContextSetting, std::string{PartnerMenuBattleContext});
  ui.setSetting(BagThrowPickSetting, !hud.allies.empty());
  ui.show(PartnerMenuDocument, true);
}

std::string_view coopOutcomeName(std::optional<coop::BattleOutcome> outcome) {
  if (!outcome) {
    return {};
  }
  switch (*outcome) {
  case coop::BattleOutcome::Victory:
    return "victory";
  case coop::BattleOutcome::Fled:
    return "fled";
  case coop::BattleOutcome::Defeat:
    return "defeat";
  }
  return {};
}

std::int32_t CoopThrowPick::take() {
  const std::optional<std::size_t> target = lit_ ? lit_ : kept_;
  kept_.reset();
  return target ? static_cast<std::int32_t>(*target) + 1 : 0;
}

void closeCoopHudBag(ui::Ui& ui, CoopThrowPick& throwTarget) {
  throwTarget.close();
  ui.setSetting(PartnerMenuContextSetting, std::string{});
  ui.setSetting(BagThrowPickSetting, false);
  ui.hide(PartnerMenuDocument);
}

std::optional<std::size_t> coopHudThrownSlot(const ui::UiCommand& command) {
  const auto* number = std::get_if<double>(&command.value);
  if ((command.name != BattleThrowCommand && command.name != BagUseCommand) || number == nullptr) {
    return std::nullopt;
  }
  return slotOf(*number);
}

void closeFieldBag(ui::Ui& ui) {
  const ui::UiValue context = ui.setting(PartnerMenuContextSetting);
  const auto* name = std::get_if<std::string>(&context);
  if (ui.isVisible(PartnerMenuDocument) && (name == nullptr || *name != PartnerMenuBattleContext)) {
    ui.hide(PartnerMenuDocument);
  }
}

void withdrawCoopAlly(game::battle::Battle& battle, FighterId ally) {
  const game::battle::AllyState* state = battle.state().allyOf(ally);
  if (state == nullptr || state->released) {
    return;
  }
  battle.closeBag(ally);
  static_cast<void>(battle.command(Command::Flee, ally));
  battle.releaseAlly(ally);
}

} // namespace dwe::app
