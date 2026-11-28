#include <dwe/game/battle/text_keys.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/screens/battle_view.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace dwe::screens {
namespace {

namespace battle = game::battle;

// The end box's lines the battle module has no key for: what a defeat takes and who joins the city.
constexpr std::string_view ItemLostTextKey = "battle.result.item_lost";
constexpr std::string_view LivesLostTextKey = "battle.result.lives_lost";
constexpr std::string_view RecruitJoinedTextKey = "battle.result.recruit_joined";
// The original's marks the end box's lines join (main: BTL_STR_YELLOW, BTL_STR_WHITE_WAIT).
constexpr std::string_view Yellow = "{color:7}";
constexpr std::string_view WhiteWait = "{color:1}{wait}";
constexpr std::string_view NewRow = "{br}";
// battle_hud.c: BTL_appendMPBonusText ends with the percent and "%!#W" (main: BTL_STR_5..20,
// BTL_STR_MP_BONUS_PERCENT).
constexpr std::string_view MpBonusPercentTextKey = "battle.result.mp_bonus_percent";
// The setting a document reads when a number has no value (no revive countdown).
constexpr double NoValue = -1.0;
// The fields of the lists' entries.
constexpr std::string_view NameField = "name";
constexpr std::string_view HpField = "hp";
constexpr std::string_view MaxHpField = "max_hp";
constexpr std::string_view MpField = "mp";
constexpr std::string_view MaxMpField = "max_mp";
constexpr std::string_view FaintedField = "fainted";
constexpr std::string_view HpBarField = "hp_bar";
constexpr std::string_view StatusField = "status";
constexpr std::string_view ReviveSecondsField = "revive_seconds";
constexpr std::string_view IdField = "id";
constexpr std::string_view TagField = "tag";
constexpr std::string_view TargetedField = "targeted";
constexpr std::string_view QueuedField = "queued";
constexpr std::string_view OrderField = "order";
constexpr std::string_view CommandField = "command";
constexpr std::string_view LabelField = "label";
constexpr std::string_view AvailableField = "available";
constexpr std::string_view CurrentField = "current";
constexpr std::string_view IconField = "icon";
constexpr std::string_view MpCostField = "mp_cost";
constexpr std::string_view ProblemField = "problem";
constexpr std::string_view AreaField = "area";
constexpr std::string_view FriendlyFireField = "friendly_fire";
// BTL_COMMAND_ICON_UVS and BTL_SPECIAL_ICON_UVS: seven icons in each row of BTLCOMND.
constexpr std::int32_t IconsPerRow = 7;
constexpr std::string_view PartsField = "parts";
// The pieces of a line go to the document joined by this mark, which no key or mark holds.
constexpr char PartSeparator = '|';
constexpr std::string_view AmountField = "amount";
constexpr std::string_view SubjectField = "subject";
constexpr std::string_view MaxField = "max";
constexpr std::string_view ValueField = "value";
constexpr std::string_view CountSuffix = ".count";

battle::StatusEffect statusOf(const battle::StatusState& status) {
  if (status.flattened) {
    return battle::StatusEffect::Flatten;
  }
  if (status.stunned) {
    return battle::StatusEffect::Stun;
  }
  if (status.confused) {
    return battle::StatusEffect::Confusion;
  }
  return status.poisoned ? battle::StatusEffect::Poison : battle::StatusEffect::None;
}

FighterView fighterViewOf(const battle::Fighter& fighter, bool hpBarShown, const NameKeys& names) {
  const battle::StatusEffect status = statusOf(fighter.status);
  return {.nameKey = names.species(fighter.combatant.species),
          .hp = fighter.hp,
          .maxHp = fighter.stats.maxHp,
          .mp = fighter.mp,
          .maxMp = fighter.stats.maxMp,
          .fainted = fighter.fainted,
          .hpBarShown = hpBarShown,
          .statusKey = status == battle::StatusEffect::None
                           ? std::string{}
                           : std::string{battle::statusTextKey(status)},
          .reviveSeconds = std::nullopt,
          .id = 0,
          .tag = {},
          .targeted = false,
          .queued = false,
          .orderKey = {}};
}

bool gaugeFull(const battle::Fighter& fighter) {
  return fighter.hasFinisher() && fighter.finisherGoal > 0 &&
         fighter.finisherProgress >= fighter.finisherGoal;
}

void addFighter(ScreenSettings& settings, std::size_t index, const FighterView& fighter,
                std::string_view list = BattleFighterList) {
  const auto add = [&](std::string_view field, ui::UiValue value) {
    settings.push_back({.name = listEntryName(list, index, field), .value = std::move(value)});
  };
  add(NameField, fighter.nameKey);
  add(HpField, static_cast<double>(fighter.hp));
  add(MaxHpField, static_cast<double>(fighter.maxHp));
  add(MpField, static_cast<double>(fighter.mp));
  add(MaxMpField, static_cast<double>(fighter.maxMp));
  add(FaintedField, fighter.fainted);
  add(HpBarField, fighter.hpBarShown);
  add(StatusField, fighter.statusKey);
  if (list == BattleAllyList) {
    add(ReviveSecondsField,
        fighter.reviveSeconds ? static_cast<double>(*fighter.reviveSeconds) : NoValue);
    add(OrderField, fighter.orderKey);
    return;
  }
  add(IdField, static_cast<double>(fighter.id));
  add(TagField, fighter.tag);
  add(TargetedField, fighter.targeted);
  add(QueuedField, fighter.queued);
}

void addCommand(ScreenSettings& settings, std::size_t index, const CommandEntry& entry) {
  const auto add = [&](std::string_view field, ui::UiValue value) {
    settings.push_back(
        {.name = listEntryName(BattleMenuList, index, field), .value = std::move(value)});
  };
  add(CommandField, static_cast<double>(entry.command));
  add(LabelField, entry.labelKey);
  add(AvailableField, entry.available);
  add(CurrentField, entry.current);
  add(IconField, entry.icon);
  add(MpCostField, static_cast<double>(entry.mpCost));
  add(ProblemField, entry.problemKey);
  add(AreaField, entry.area);
  add(FriendlyFireField, entry.friendlyFire);
}

std::string countName(std::string_view list) {
  return std::string{list} + std::string{CountSuffix};
}

} // namespace

std::optional<std::int32_t> reviveSecondsOf(const battle::Battle& battle, battle::FighterId own) {
  const battle::EndingRules& ending = battle.rules().ending;
  const std::int32_t deathFrames = battle.deathFramesOf(own);
  // Only the player's side revives, and a partner whose countdown ran out stays down.
  const battle::Fighter& fighter = battle.state().fighters.at(own);
  if (!battle.state().isPlayerSide(own) || !fighter.fainted || fighter.gone() ||
      deathFrames < ending.reviveCountdownShownAt) {
    return std::nullopt;
  }
  const std::int32_t framesLeft = std::max(0, ending.reviveFrames - deathFrames);
  return (framesLeft + battle::FramesPerSecond - 1) / battle::FramesPerSecond;
}

// A technique's or the finisher's entry shows its move's name; the others the original's command
// name.
std::string commandLabelOf(battle::Command command, const battle::Fighter& partner,
                           const NameKeys& names) {
  const auto number = static_cast<std::size_t>(command);
  constexpr auto FirstTechnique = static_cast<std::size_t>(battle::Command::Technique1);
  if (number < FirstTechnique) {
    return commandLabelKey(command);
  }
  const std::size_t slot =
      command == battle::Command::Finisher ? battle::FinisherSlot : number - FirstTechnique;
  const auto& move = partner.moves.at(slot);
  return move ? names.move(*move) : std::string{};
}

// battle_main.c: BTL_setCommandIconUV: a command's own icon, or for a technique the icon of its
// move's special effect.
std::string commandIconOf(battle::Command command, const battle::Fighter& partner,
                          const battle::MoveCatalog& moves) {
  const auto number = static_cast<std::int32_t>(command);
  constexpr auto FirstTechnique = static_cast<std::int32_t>(battle::Command::Technique1);
  if (command == battle::Command::Finisher || command == battle::Command::None) {
    return {};
  }
  if (number < FirstTechnique) {
    return std::format("command-{}", number);
  }
  const auto& move = partner.moves.at(static_cast<std::size_t>(number - FirstTechnique));
  if (!move) {
    return {};
  }
  return std::format("special-{}", std::clamp(moves.at(*move).special, 0, IconsPerRow - 1));
}

namespace {

// A technique's MP cost to the partner now; the other commands cost nothing.
std::int32_t techniqueCostOf(const battle::Battle& battle, battle::Command command,
                             battle::FighterId own) {
  const auto number = static_cast<std::int32_t>(command);
  constexpr auto FirstTechnique = static_cast<std::int32_t>(battle::Command::Technique1);
  constexpr auto LastTechnique = static_cast<std::int32_t>(battle::Command::Technique3);
  if (number < FirstTechnique || number > LastTechnique) {
    return 0;
  }
  return battle.partnerMoveCost(static_cast<std::size_t>(number - FirstTechnique), own).value_or(0);
}

std::string_view stanceName(battle::Stance stance) {
  switch (stance) {
  case battle::Stance::Aggressive:
    break;
  case battle::Stance::Hold:
    return "hold";
  case battle::Stance::Distance:
    return "distance";
  case battle::Stance::Guard:
    return "guard";
  }
  return "aggressive";
}

// The letter of a foe that shares its species with another foe, by their order in the battle.
std::string foeTagOf(const battle::BattleState& state, battle::FighterId own,
                     battle::FighterId id) {
  if (id >= state.fighters.size() || !state.foes(own, id)) {
    return {};
  }
  std::size_t same = 0;
  char letter = 'A';
  for (battle::FighterId other = 0; other < state.fighters.size(); ++other) {
    if (!state.foes(own, other) ||
        state.fighters[other].combatant.species != state.fighters[id].combatant.species) {
      continue;
    }
    ++same;
    if (other < id) {
      ++letter;
    }
  }
  return same > 1 ? std::string(1, letter) : std::string{};
}

std::string_view phaseName(battle::OrderPhase phase) {
  switch (phase) {
  case battle::OrderPhase::Idle:
    break;
  case battle::OrderPhase::Processing:
    return "processing";
  case battle::OrderPhase::Positioning:
    return "positioning";
  case battle::OrderPhase::Preparing:
    return "preparing";
  case battle::OrderPhase::Executing:
    return "executing";
  case battle::OrderPhase::Recovering:
    return "recovering";
  }
  return {};
}

std::string problemKeyOf(battle::OrderProblem problem) {
  switch (problem) {
  case battle::OrderProblem::None:
  case battle::OrderProblem::ByTrainer:
    break;
  case battle::OrderProblem::EmptySlot:
    return "battle.folder.problem.empty_slot";
  case battle::OrderProblem::NotEnoughMp:
    return "battle.folder.problem.not_enough_mp";
  case battle::OrderProblem::FinisherNotReady:
    return "battle.folder.problem.finisher_not_ready";
  case battle::OrderProblem::NoTarget:
    return "battle.folder.problem.no_target";
  case battle::OrderProblem::TargetGone:
    return "battle.folder.problem.target_gone";
  case battle::OrderProblem::Unreachable:
    return "battle.folder.problem.unreachable";
  }
  return {};
}

std::string endKeyOf(battle::OrderEnd end, battle::OrderProblem problem) {
  switch (end) {
  case battle::OrderEnd::Done:
    break;
  case battle::OrderEnd::Interrupted:
    return "battle.folder.end.interrupted";
  case battle::OrderEnd::Invalid:
    return "battle.folder.end.invalid";
  case battle::OrderEnd::Cancelled:
    return problem == battle::OrderProblem::ByTrainer ? "battle.folder.end.annulled"
                                                      : "battle.folder.end.cancelled";
  case battle::OrderEnd::Replaced:
    return "battle.folder.end.replaced";
  }
  return {};
}

std::string commandProblemOf(const battle::Battle& battle, battle::Command command,
                             battle::FighterId own) {
  const auto number = static_cast<std::int32_t>(command);
  constexpr auto FirstTechnique = static_cast<std::int32_t>(battle::Command::Technique1);
  constexpr auto LastTechnique = static_cast<std::int32_t>(battle::Command::Technique3);
  if (command == battle::Command::Finisher) {
    return problemKeyOf(battle.orderProblem(battle::FinisherSlot, own));
  }
  if (number < FirstTechnique || number > LastTechnique) {
    return {};
  }
  return problemKeyOf(battle.orderProblem(static_cast<std::size_t>(number - FirstTechnique), own));
}

std::string_view areaName(battle::AreaShape area) {
  switch (area) {
  case battle::AreaShape::Single:
    break;
  case battle::AreaShape::AroundSelf:
    return "around_self";
  case battle::AreaShape::AroundTarget:
    return "around_target";
  case battle::AreaShape::Global:
    return "global";
  }
  return {};
}

void addFolderTechnique(CommandEntry& entry, const battle::Battle& battle, battle::FighterId own) {
  const auto number = static_cast<std::int32_t>(entry.command);
  constexpr auto FirstTechnique = static_cast<std::int32_t>(battle::Command::Technique1);
  constexpr auto LastTechnique = static_cast<std::int32_t>(battle::Command::Technique3);
  if (number < FirstTechnique || number > LastTechnique) {
    return;
  }
  const std::optional<battle::folder::Technique> technique =
      battle.folderTechnique(static_cast<std::size_t>(number - FirstTechnique), own);
  if (technique) {
    entry.area = std::string{areaName(technique->area)};
    entry.friendlyFire = technique->friendlyFire;
  }
}

std::string orderMoveKey(const battle::Battle& battle, battle::FighterId own,
                         const battle::Order& order, const NameKeys& names) {
  const auto& move = battle.state().fighters[own].moves.at(order.slot);
  return move ? names.move(*move) : std::string{};
}

std::string allyOrderKeyOf(const battle::Battle& battle, battle::FighterId id,
                           const NameKeys& names) {
  if (const battle::FolderFighter* folder = battle.folderOf(id)) {
    return folder->active ? orderMoveKey(battle, id, *folder->active, names)
                          : std::format("battle.stance.{}", stanceName(folder->effectiveStance()));
  }
  const battle::Command current = battle.state().commandsOf(id).current;
  return current == battle::Command::None
             ? std::string{}
             : commandLabelOf(current, battle.state().fighters[id], names);
}

BattleView::Folder folderViewOf(const battle::Battle& battle, const battle::FolderFighter& folder,
                                battle::FighterId own, const NameKeys& names) {
  constexpr std::int32_t Hundred = 100;
  BattleView::Folder view;
  if (folder.active) {
    view.moveKey = orderMoveKey(battle, own, *folder.active, names);
    view.phase = std::string{phaseName(folder.phase)};
    if (folder.phaseLength > 0) {
      view.phasePercent = (folder.phaseLength - folder.phaseFrames) * Hundred / folder.phaseLength;
    }
  }
  const battle::BattleState& state = battle.state();
  const auto foeKey = [&](std::optional<battle::FighterId> target) {
    return target && *target < state.fighters.size() && state.foes(own, *target)
               ? names.species(state.fighters[*target].combatant.species)
               : std::string{};
  };
  const auto foeTag = [&](std::optional<battle::FighterId> target) {
    return target ? foeTagOf(state, own, *target) : std::string{};
  };
  if (folder.active) {
    view.activeTargetKey = foeKey(folder.active->target);
    view.activeTargetTag = foeTag(folder.active->target);
    view.committed = folder.committed;
  }
  for (const battle::Order& order : folder.queue) {
    view.queue.push_back(orderMoveKey(battle, own, order, names));
    view.queueTargets.push_back(foeKey(order.target));
    view.queueTargetTags.push_back(foeTag(order.target));
  }
  view.targetKey = foeKey(state.fighters[own].target);
  view.targetTag = foeTag(state.fighters[own].target);
  view.stance = std::string{stanceName(folder.effectiveStance())};
  view.bar = folder.defenseBar;
  view.barMax = battle.defenseBarCapacity(own);
  view.broken = folder.broken();
  view.brokenSeconds =
      (folder.brokenFrames + battle::FramesPerSecond - 1) / battle::FramesPerSecond;
  if (folder.notice) {
    view.noticeKey = endKeyOf(folder.notice->end, folder.notice->problem);
    view.noticeMoveKey = orderMoveKey(battle, own, folder.notice->order, names);
    view.noticeReasonKey = problemKeyOf(folder.notice->problem);
    if (folder.notice->by) {
      view.noticeByKey = orderMoveKey(battle, own, *folder.notice->by, names);
    }
  }
  return view;
}

} // namespace

BattleView battleViewOf(const battle::Battle& battle, const NameKeys& names,
                        battle::FighterId own) {
  const battle::BattleState& state = battle.state();
  BattleView view;
  if (own >= state.fighters.size() || !state.commanded(own)) {
    own = battle::PartnerId;
  }
  const auto fighterOf = [&](battle::FighterId id) {
    FighterView fighter = fighterViewOf(state.fighters[id], battle.hpBarShown(id), names);
    fighter.id = id;
    fighter.tag = foeTagOf(state, own, id);
    return fighter;
  };
  view.fighters.push_back(fighterOf(own));
  for (battle::FighterId id = 1; id < state.fighters.size(); ++id) {
    if (!state.isPlayerSide(id) || state.allies.empty()) {
      view.fighters.push_back(fighterOf(id));
    }
  }
  for (battle::FighterId id = 0; id < state.fighters.size(); ++id) {
    if (id != own && !state.allies.empty() && state.isPlayerSide(id)) {
      view.allies.push_back(fighterViewOf(state.fighters[id], true, names));
      const std::optional<std::int32_t> revive = reviveSecondsOf(battle, id);
      view.allies.back().reviveSeconds =
          revive && *revive > 0 ? revive : std::optional<std::int32_t>{};
      view.allies.back().orderKey = allyOrderKeyOf(battle, id, names);
    }
  }
  const battle::Fighter& partner = state.fighters[own];
  const battle::CommandState& commands = state.commandsOf(own);
  const bool folder = battle.system() == battle::BattleSystemId::Folder;
  for (const battle::Command command : commands.menu) {
    view.menu.push_back(
        {.command = command,
         .labelKey =
             folder && battle::folderCommandTextKey(command) != battle::commandTextKey(command)
                 ? std::string{battle::folderCommandTextKey(command)}
                 : commandLabelOf(command, partner, names),
         .available = command != battle::Command::Finisher || gaugeFull(partner),
         .current = command == commands.current,
         .icon = commandIconOf(command, partner, battle.moves()),
         .mpCost = techniqueCostOf(battle, command, own),
         .problemKey = folder ? commandProblemOf(battle, command, own) : std::string{},
         .area = {},
         .friendlyFire = false});
    if (folder) {
      addFolderTechnique(view.menu.back(), battle, own);
    }
  }
  const bool partnerCharging = state.finisher.fighter == own;
  view.finisher = {.progress = partner.finisherProgress,
                   .goal = partner.finisherGoal,
                   .ready = gaugeFull(partner),
                   .charging = partnerCharging && state.finisher.charging,
                   .charge = partnerCharging ? state.finisher.charge : 0};
  view.reviveSeconds = reviveSecondsOf(battle, own);
  if (state.matchClock) {
    view.matchSeconds = state.matchClock->seconds;
  }
  if (const battle::FolderFighter* orders = battle.folderOf(own)) {
    view.folder = folderViewOf(battle, *orders, own, names);
    const auto aims = [&](const battle::Order& order, battle::FighterId id) {
      return order.target == id;
    };
    for (FighterView& fighter : view.fighters) {
      fighter.targeted = state.fighters[own].target == fighter.id && fighter.id != own;
      fighter.queued =
          fighter.id != own && ((orders->active && aims(*orders->active, fighter.id)) ||
                                std::ranges::any_of(orders->queue, [&](const battle::Order& order) {
                                  return aims(order, fighter.id);
                                }));
    }
  }
  view.bagAvailable = battle.bagAvailableFor(own);
  view.bagOpen = battle.bagOpen(own);
  view.itemInFlight = battle.thrownItemOf(own).has_value();
  return view;
}

namespace {

// battle_hud.c: BTL_appendCommandLearnedText: the message of the commands a brain's menu opened,
// "Listens to !" after it.
std::optional<EndMessage> commandMessageOf(battle::MenuEntry entry) {
  switch (entry) {
  case battle::MenuEntry::Attack:
    return EndMessage::GoAllTheWay;
  case battle::MenuEntry::Moderate:
    return EndMessage::KeepItDown;
  case battle::MenuEntry::Distance:
  case battle::MenuEntry::Change:
    return EndMessage::MoveAwayChangeTarget;
  case battle::MenuEntry::Defend:
    return EndMessage::PutUpWithIt;
  case battle::MenuEntry::Techniques:
    return EndMessage::SetTechnique;
  case battle::MenuEntry::Finisher:
  case battle::MenuEntry::Flee:
  case battle::MenuEntry::YourCall:
    break;
  }
  return std::nullopt;
}

EndLine originalLine(std::vector<std::string> parts) {
  return {.parts = std::move(parts), .nameKey = {}, .amount = 0, .subjectKey = {}};
}

EndLine engineLine(std::string_view key, std::string nameKey, std::int32_t amount) {
  return {.parts = {std::string{key}},
          .nameKey = std::move(nameKey),
          .amount = amount,
          .subjectKey = {}};
}

} // namespace

std::string endMessageKey(EndMessage message) {
  return localization::gameTextKey(BattleText, BattleMessagesBlock,
                                   static_cast<std::size_t>(message));
}

namespace {

// battle_ui.c: handleBattleEndBox joins the lines in this order: the items each enemy dropped, the
// injury, the commands the brain opened, the MP bonus and the move learned (battleMoveLearning
// shows the one it rolled, not the moves learned with it).
std::vector<EndLine> originalLines(const battle::BattleResult& result,
                                   const session::BattleAftermath& aftermath,
                                   const NameKeys& names) {
  std::vector<EndLine> lines;
  const std::string yellow{Yellow};
  for (const battle::Drop& drop : result.drops) {
    lines.push_back(
        originalLine({yellow, names.species(drop.species), endMessageKey(EndMessage::Dropped),
                      names.item(drop.item), std::string{WhiteWait}}));
  }
  // battle_hud.c: BTL_appendInjuredText: the partner's name in yellow, then «was injured».
  if (result.injured) {
    lines.push_back(originalLine(
        {yellow, names.species(result.partnerSpecies), endMessageKey(EndMessage::WasInjured)}));
  }
  if (!result.commandsLearned.empty()) {
    std::vector<std::string> parts;
    for (const battle::MenuEntry entry : result.commandsLearned) {
      const std::optional<EndMessage> message = commandMessageOf(entry);
      if (message && (parts.empty() || parts.back() != endMessageKey(*message))) {
        parts.push_back(endMessageKey(*message));
      }
    }
    if (!parts.empty()) {
      parts.push_back(endMessageKey(EndMessage::ListensTo));
      lines.push_back(originalLine(std::move(parts)));
    }
  }
  if (result.mpDiscountLearned > 0) {
    // battle_hud.c: BTL_appendMPBonusText: the US text has no row break and four rows less than it
    // counts, so the original cuts the row at the box's edge and then types what lies past the
    // text until a press finds an end; here it takes two rows (a fix, see the module's doc).
    EndLine line =
        originalLine({endMessageKey(EndMessage::MpConsumptionBonus), std::string{NewRow},
                      endMessageKey(EndMessage::ReducedBy), std::string{MpBonusPercentTextKey}});
    line.amount = result.mpDiscountLearned;
    lines.push_back(std::move(line));
  }
  if (!aftermath.learnedMoves.empty()) {
    lines.push_back(originalLine(
        {yellow, names.move(aftermath.learnedMoves.front()), endMessageKey(EndMessage::Learned)}));
  }
  return lines;
}

} // namespace

BattleEndView battleEndViewOf(const battle::BattleResult& result,
                              const session::BattleAftermath& aftermath, const NameKeys& names) {
  BattleEndView view{.outcomeKey = std::string{battle::outcomeTextKey(result.outcome)},
                     .lines = originalLines(result, aftermath, names),
                     .statGains = {},
                     .bits = result.bits};
  if (result.livesLost > 0) {
    view.lines.push_back(engineLine(LivesLostTextKey, {}, result.livesLost));
  }
  for (const game::inventory::Stack& lost : aftermath.itemsLost) {
    view.lines.push_back(engineLine(ItemLostTextKey, names.item(lost.item), lost.count));
  }
  if (aftermath.recruitJoined) {
    view.lines.push_back(
        engineLine(RecruitJoinedTextKey, names.species(*aftermath.recruitJoined), 0));
  }
  for (const game::partner::Stat stat : game::partner::AllStats) {
    view.statGains.at(static_cast<std::size_t>(stat)) = aftermath.statGains[stat];
    view.stats.at(static_cast<std::size_t>(stat)) = aftermath.statsBefore[stat];
  }
  return view;
}

ScreenSettings settingsOf(const BattleView& view) {
  ScreenSettings settings;
  settings.push_back(
      {.name = countName(BattleFighterList), .value = static_cast<double>(view.fighters.size())});
  for (std::size_t index = 0; index < view.fighters.size(); ++index) {
    addFighter(settings, index, view.fighters[index]);
  }
  settings.push_back(
      {.name = countName(BattleAllyList), .value = static_cast<double>(view.allies.size())});
  for (std::size_t index = 0; index < view.allies.size(); ++index) {
    addFighter(settings, index, view.allies[index], BattleAllyList);
  }
  settings.push_back(
      {.name = countName(BattleMenuList), .value = static_cast<double>(view.menu.size())});
  for (std::size_t index = 0; index < view.menu.size(); ++index) {
    addCommand(settings, index, view.menu[index]);
  }
  settings.push_back({.name = std::string{FinisherProgressSetting},
                      .value = static_cast<double>(view.finisher.progress)});
  settings.push_back(
      {.name = std::string{FinisherGoalSetting}, .value = static_cast<double>(view.finisher.goal)});
  settings.push_back({.name = std::string{FinisherReadySetting}, .value = view.finisher.ready});
  settings.push_back(
      {.name = std::string{FinisherChargingSetting}, .value = view.finisher.charging});
  settings.push_back({.name = std::string{FinisherChargeSetting},
                      .value = static_cast<double>(view.finisher.charge)});
  settings.push_back(
      {.name = std::string{ReviveSecondsSetting},
       .value = view.reviveSeconds ? static_cast<double>(*view.reviveSeconds) : NoValue});
  settings.push_back(
      {.name = std::string{MatchSecondsSetting},
       .value = view.matchSeconds ? static_cast<double>(*view.matchSeconds) : NoValue});
  settings.push_back({.name = std::string{BagAvailableSetting}, .value = view.bagAvailable});
  settings.push_back({.name = std::string{BagOpenSetting}, .value = view.bagOpen});
  settings.push_back({.name = std::string{ItemInFlightSetting}, .value = view.itemInFlight});
  settings.push_back({.name = std::string{FolderActiveSetting}, .value = view.folder.has_value()});
  const BattleView::Folder folder = view.folder.value_or(BattleView::Folder{});
  settings.push_back({.name = std::string{FolderMoveSetting}, .value = folder.moveKey});
  settings.push_back({.name = std::string{FolderPhaseSetting}, .value = folder.phase});
  settings.push_back({.name = std::string{FolderPhasePercentSetting},
                      .value = static_cast<double>(folder.phasePercent)});
  settings.push_back(
      {.name = countName(FolderQueueList), .value = static_cast<double>(folder.queue.size())});
  for (std::size_t index = 0; index < folder.queue.size(); ++index) {
    settings.push_back(
        {.name = listEntryName(FolderQueueList, index, "move"), .value = folder.queue[index]});
    settings.push_back({.name = listEntryName(FolderQueueList, index, "target"),
                        .value = folder.queueTargets[index]});
    settings.push_back({.name = listEntryName(FolderQueueList, index, "tag"),
                        .value = folder.queueTargetTags[index]});
  }
  settings.push_back(
      {.name = std::string{FolderBarSetting}, .value = static_cast<double>(folder.bar)});
  settings.push_back(
      {.name = std::string{FolderBarMaxSetting}, .value = static_cast<double>(folder.barMax)});
  settings.push_back({.name = std::string{FolderBrokenSetting}, .value = folder.broken});
  settings.push_back({.name = std::string{FolderTargetSetting}, .value = folder.targetKey});
  settings.push_back({.name = std::string{FolderNoticeSetting}, .value = folder.noticeKey});
  settings.push_back({.name = std::string{FolderNoticeMoveSetting}, .value = folder.noticeMoveKey});
  settings.push_back(
      {.name = std::string{FolderNoticeReasonSetting}, .value = folder.noticeReasonKey});
  settings.push_back({.name = std::string{FolderNoticeBySetting}, .value = folder.noticeByKey});
  settings.push_back(
      {.name = std::string{FolderActiveTargetSetting}, .value = folder.activeTargetKey});
  settings.push_back(
      {.name = std::string{FolderActiveTagSetting}, .value = folder.activeTargetTag});
  settings.push_back({.name = std::string{FolderTargetTagSetting}, .value = folder.targetTag});
  settings.push_back({.name = std::string{FolderStanceSetting}, .value = folder.stance});
  settings.push_back({.name = std::string{FolderBrokenSecondsSetting},
                      .value = static_cast<double>(folder.brokenSeconds)});
  settings.push_back({.name = std::string{FolderCommittedSetting}, .value = folder.committed});
  return settings;
}

ScreenSettings settingsOf(const BattleEndView& view) {
  ScreenSettings settings;
  settings.push_back({.name = std::string{BattleOutcomeSetting}, .value = view.outcomeKey});
  settings.push_back(
      {.name = countName(BattleEndList), .value = static_cast<double>(view.lines.size())});
  for (std::size_t index = 0; index < view.lines.size(); ++index) {
    const EndLine& line = view.lines[index];
    std::string parts;
    for (const std::string& part : line.parts) {
      if (!parts.empty()) {
        parts += PartSeparator;
      }
      parts += part;
    }
    settings.push_back(
        {.name = listEntryName(BattleEndList, index, PartsField), .value = std::move(parts)});
    settings.push_back(
        {.name = listEntryName(BattleEndList, index, NameField), .value = line.nameKey});
    settings.push_back({.name = listEntryName(BattleEndList, index, AmountField),
                        .value = static_cast<double>(line.amount)});
    settings.push_back(
        {.name = listEntryName(BattleEndList, index, SubjectField), .value = line.subjectKey});
  }
  for (std::size_t stat = 0; stat < view.statGains.size(); ++stat) {
    settings.push_back({.name = listEntryName(BattleGainList, stat, ValueField),
                        .value = static_cast<double>(view.statGains[stat])});
    settings.push_back({.name = listEntryName(BattleStatList, stat, ValueField),
                        .value = static_cast<double>(view.stats.at(stat))});
    settings.push_back({.name = listEntryName(BattleStatList, stat, MaxField),
                        .value = static_cast<double>(view.statMaxima.at(stat))});
  }
  settings.push_back(
      {.name = std::string{BattleBitsSetting}, .value = static_cast<double>(view.bits)});
  settings.push_back(
      {.name = std::string{BattleMoneySetting}, .value = static_cast<double>(view.money)});
  settings.push_back(
      {.name = std::string{BattleFrameSecondsSetting}, .value = 1.0 / battle::FramesPerSecond});
  settings.push_back({.name = std::string{BattleCountDelaySetting},
                      .value = static_cast<double>(view.countDelayFrames)});
  settings.push_back(
      {.name = std::string{BattleWaitSetting}, .value = static_cast<double>(view.waitFrames)});
  return settings;
}

} // namespace dwe::screens
