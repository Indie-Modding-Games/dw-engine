#include <dwe/game/battle/text_keys.hpp>
#include <dwe/screens/battle_view.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace dwe::screens {
namespace {

namespace battle = game::battle;

// The end box's lines the battle module has no key for: what a defeat takes and who joins the city.
constexpr std::string_view ItemLostTextKey = "battle.result.item_lost";
constexpr std::string_view LivesLostTextKey = "battle.result.lives_lost";
constexpr std::string_view RecruitJoinedTextKey = "battle.result.recruit_joined";
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
constexpr std::string_view CommandField = "command";
constexpr std::string_view LabelField = "label";
constexpr std::string_view AvailableField = "available";
constexpr std::string_view CurrentField = "current";
constexpr std::string_view KeyField = "key";
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
                           : std::string{battle::statusTextKey(status)}};
}

// A technique's or the finisher's entry shows its move's name; the others the original's command
// name.
std::string labelOf(battle::Command command, const battle::Fighter& partner,
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

bool gaugeFull(const battle::Fighter& fighter) {
  return fighter.hasFinisher() && fighter.finisherGoal > 0 &&
         fighter.finisherProgress >= fighter.finisherGoal;
}

std::optional<std::int32_t> reviveSecondsOf(const battle::Battle& battle) {
  const battle::BattleState& state = battle.state();
  const battle::EndingRules& ending = battle.rules().ending;
  if (!state.partner().fainted || state.deathFrames < ending.reviveCountdownShownAt) {
    return std::nullopt;
  }
  const std::int32_t framesLeft = std::max(0, ending.reviveFrames - state.deathFrames);
  return (framesLeft + battle::FramesPerSecond - 1) / battle::FramesPerSecond;
}

void addFighter(ScreenSettings& settings, std::size_t index, const FighterView& fighter) {
  const auto add = [&](std::string_view field, ui::UiValue value) {
    settings.push_back(
        {.name = listEntryName(BattleFighterList, index, field), .value = std::move(value)});
  };
  add(NameField, fighter.nameKey);
  add(HpField, static_cast<double>(fighter.hp));
  add(MaxHpField, static_cast<double>(fighter.maxHp));
  add(MpField, static_cast<double>(fighter.mp));
  add(MaxMpField, static_cast<double>(fighter.maxMp));
  add(FaintedField, fighter.fainted);
  add(HpBarField, fighter.hpBarShown);
  add(StatusField, fighter.statusKey);
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
}

std::string countName(std::string_view list) {
  return std::string{list} + std::string{CountSuffix};
}

} // namespace

BattleView battleViewOf(const battle::Battle& battle, const NameKeys& names) {
  const battle::BattleState& state = battle.state();
  BattleView view;
  for (battle::FighterId id = 0; id < state.fighters.size(); ++id) {
    view.fighters.push_back(fighterViewOf(state.fighters[id], battle.hpBarShown(id), names));
  }
  const battle::Fighter& partner = state.partner();
  for (const battle::Command command : battle.menu()) {
    view.menu.push_back({.command = command,
                         .labelKey = labelOf(command, partner, names),
                         .available = command != battle::Command::Finisher || gaugeFull(partner),
                         .current = command == state.commands.current});
  }
  const bool partnerCharging = state.finisher.fighter == battle::PartnerId;
  view.finisher = {.progress = partner.finisherProgress,
                   .goal = partner.finisherGoal,
                   .ready = gaugeFull(partner),
                   .charging = partnerCharging && state.finisher.charging,
                   .charge = partnerCharging ? state.finisher.charge : 0};
  view.reviveSeconds = reviveSecondsOf(battle);
  view.bagAvailable = battle.bagAvailable();
  view.bagOpen = battle.bagOpen();
  view.itemInFlight = battle.itemInFlight();
  return view;
}

BattleEndView battleEndViewOf(const battle::BattleResult& result,
                              const session::BattleAftermath& aftermath, const NameKeys& names) {
  BattleEndView view{.outcomeKey = std::string{battle::outcomeTextKey(result.outcome)},
                     .lines = {},
                     .statGains = {},
                     .bits = result.bits};
  for (const battle::Drop& drop : result.drops) {
    view.lines.push_back({.key = std::string{battle::ItemDroppedTextKey},
                          .nameKey = names.item(drop.item),
                          .amount = 1,
                          .subjectKey = names.species(drop.species)});
  }
  if (result.injured) {
    view.lines.push_back(
        {.key = std::string{battle::InjuredTextKey}, .nameKey = {}, .amount = 0, .subjectKey = {}});
  }
  for (const std::string& move : aftermath.learnedMoves) {
    view.lines.push_back({.key = std::string{battle::MoveLearnedTextKey},
                          .nameKey = names.move(move),
                          .amount = 0,
                          .subjectKey = {}});
  }
  if (result.livesLost > 0) {
    view.lines.push_back({.key = std::string{LivesLostTextKey},
                          .nameKey = {},
                          .amount = result.livesLost,
                          .subjectKey = {}});
  }
  for (const game::inventory::Stack& lost : aftermath.itemsLost) {
    view.lines.push_back({.key = std::string{ItemLostTextKey},
                          .nameKey = names.item(lost.item),
                          .amount = lost.count,
                          .subjectKey = {}});
  }
  if (aftermath.recruitJoined) {
    view.lines.push_back({.key = std::string{RecruitJoinedTextKey},
                          .nameKey = names.species(*aftermath.recruitJoined),
                          .amount = 0,
                          .subjectKey = {}});
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
  settings.push_back({.name = std::string{BagAvailableSetting}, .value = view.bagAvailable});
  settings.push_back({.name = std::string{BagOpenSetting}, .value = view.bagOpen});
  settings.push_back({.name = std::string{ItemInFlightSetting}, .value = view.itemInFlight});
  return settings;
}

ScreenSettings settingsOf(const BattleEndView& view) {
  ScreenSettings settings;
  settings.push_back({.name = std::string{BattleOutcomeSetting}, .value = view.outcomeKey});
  settings.push_back(
      {.name = countName(BattleEndList), .value = static_cast<double>(view.lines.size())});
  for (std::size_t index = 0; index < view.lines.size(); ++index) {
    const EndLine& line = view.lines[index];
    settings.push_back({.name = listEntryName(BattleEndList, index, KeyField), .value = line.key});
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
