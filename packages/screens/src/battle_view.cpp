#include <dwe/game/battle/text_keys.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/screens/battle_view.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
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
constexpr std::string_view CommandField = "command";
constexpr std::string_view LabelField = "label";
constexpr std::string_view AvailableField = "available";
constexpr std::string_view CurrentField = "current";
constexpr std::string_view IconField = "icon";
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
                           : std::string{battle::statusTextKey(status)}};
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
  add(IconField, entry.icon);
}

std::string countName(std::string_view list) {
  return std::string{list} + std::string{CountSuffix};
}

} // namespace

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

BattleView battleViewOf(const battle::Battle& battle, const NameKeys& names) {
  const battle::BattleState& state = battle.state();
  BattleView view;
  for (battle::FighterId id = 0; id < state.fighters.size(); ++id) {
    view.fighters.push_back(fighterViewOf(state.fighters[id], battle.hpBarShown(id), names));
  }
  const battle::Fighter& partner = state.partner();
  for (const battle::Command command : battle.menu()) {
    view.menu.push_back({.command = command,
                         .labelKey = commandLabelOf(command, partner, names),
                         .available = command != battle::Command::Finisher || gaugeFull(partner),
                         .current = command == state.commands.current,
                         .icon = commandIconOf(command, partner, battle.moves())});
  }
  const bool partnerCharging = state.finisher.fighter == battle::PartnerId;
  view.finisher = {.progress = partner.finisherProgress,
                   .goal = partner.finisherGoal,
                   .ready = gaugeFull(partner),
                   .charging = partnerCharging && state.finisher.charging,
                   .charge = partnerCharging ? state.finisher.charge : 0};
  view.reviveSeconds = reviveSecondsOf(battle);
  if (state.matchClock) {
    view.matchSeconds = state.matchClock->seconds;
  }
  view.bagAvailable = battle.bagAvailable();
  view.bagOpen = battle.bagOpen();
  view.itemInFlight = battle.itemInFlight();
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
