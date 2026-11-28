#pragma once

#include <dwe/game/battle/battle.hpp>
#include <dwe/game/inventory/stacking.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/battle_adapter.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::screens {

// The settings of the battle screen (ui/battle/document), which refreshes on BattleRevisionSetting.
inline constexpr std::string_view BattleRevisionSetting = "battle.revision";
inline constexpr std::string_view BattleFighterList = "battle.fighter";
// A co-op battle's other partners (docs/architecture/coop-combat.md §6).
inline constexpr std::string_view BattleAllyList = "battle.ally";
inline constexpr std::string_view BattleMenuList = "battle.menu";
inline constexpr std::string_view BattleEndList = "battle.end";
inline constexpr std::string_view BattleGainList = "battle.end.gain";
inline constexpr std::string_view BattleStatList = "battle.end.stat";
inline constexpr std::string_view FinisherProgressSetting = "battle.finisher.progress";
inline constexpr std::string_view FinisherGoalSetting = "battle.finisher.goal";
inline constexpr std::string_view FinisherReadySetting = "battle.finisher.ready";
inline constexpr std::string_view FinisherChargingSetting = "battle.finisher.charging";
inline constexpr std::string_view FinisherChargeSetting = "battle.finisher.charge";
inline constexpr std::string_view ReviveSecondsSetting = "battle.revive_seconds";
inline constexpr std::string_view BattleOutcomeSetting = "battle.end.outcome";
inline constexpr std::string_view BattleBitsSetting = "battle.end.bits";
inline constexpr std::string_view BattleMoneySetting = "battle.end.money";
inline constexpr std::string_view BattleFrameSecondsSetting = "battle.frame_seconds";
inline constexpr std::string_view BattleCountDelaySetting = "battle.end.count_delay_frames";
inline constexpr std::string_view BattleWaitSetting = "battle.end.wait_frames";
inline constexpr std::string_view BagAvailableSetting = "battle.bag.available";
inline constexpr std::string_view BagOpenSetting = "battle.bag.open";
inline constexpr std::string_view ItemInFlightSetting = "battle.item_in_flight";
inline constexpr std::string_view MatchSecondsSetting = "battle.match_seconds";
// The Folder battle's HUD (docs/architecture/folder-battle.md).
inline constexpr std::string_view FolderActiveSetting = "battle.folder.active";
inline constexpr std::string_view FolderMoveSetting = "battle.folder.move";
inline constexpr std::string_view FolderPhaseSetting = "battle.folder.phase";
inline constexpr std::string_view FolderPhasePercentSetting = "battle.folder.phase_percent";
inline constexpr std::string_view FolderQueueList = "battle.folder.queue";
inline constexpr std::string_view FolderBarSetting = "battle.folder.bar";
inline constexpr std::string_view FolderBarMaxSetting = "battle.folder.bar_max";
inline constexpr std::string_view FolderBrokenSetting = "battle.folder.broken";
inline constexpr std::string_view FolderTargetSetting = "battle.folder.target";
inline constexpr std::string_view FolderNoticeSetting = "battle.folder.notice";
inline constexpr std::string_view FolderNoticeMoveSetting = "battle.folder.notice_move";
inline constexpr std::string_view FolderNoticeReasonSetting = "battle.folder.notice_reason";
inline constexpr std::string_view FolderNoticeBySetting = "battle.folder.notice_by";
inline constexpr std::string_view FolderActiveTargetSetting = "battle.folder.active_target";
inline constexpr std::string_view FolderActiveTagSetting = "battle.folder.active_tag";
inline constexpr std::string_view FolderTargetTagSetting = "battle.folder.target_tag";
inline constexpr std::string_view FolderStanceSetting = "battle.folder.stance";
inline constexpr std::string_view FolderBrokenSecondsSetting = "battle.folder.broken_seconds";
inline constexpr std::string_view FolderCommittedSetting = "battle.folder.committed";

// A command's label: a technique's or the finisher's move name, the others the original's command
// name (battle_main.c: BTL_COMMAND_NAMES; vs_main.c: VS_renderMoveName).
[[nodiscard]] std::string commandLabelOf(game::battle::Command command,
                                         const game::battle::Fighter& fighter,
                                         const NameKeys& names);
// A command's icon in the skins' sprites: its own ("command-<n>") or, for a technique, its move's
// special effect's ("special-<n>"); none for the finisher (battle_main.c: BTL_setCommandIconUV;
// vs_main.c: VS_setCommandIconUV).
[[nodiscard]] std::string commandIconOf(game::battle::Command command,
                                        const game::battle::Fighter& fighter,
                                        const game::battle::MoveCatalog& moves);
// The Folder battle's words on its HUD (docs/architecture/folder-battle.md): a stance's name
// ("aggressive"...), an order phase's ("processing"..., empty while idle), the text key of why an
// order cannot go on and of how one ended other than done (empty when that says nothing).
[[nodiscard]] std::string_view stanceName(game::battle::Stance stance);
[[nodiscard]] std::string_view orderPhaseName(game::battle::OrderPhase phase);
[[nodiscard]] std::string orderProblemKey(game::battle::OrderProblem problem);
[[nodiscard]] std::string orderEndKey(game::battle::OrderEnd end,
                                      game::battle::OrderProblem problem);
// The whole seconds a fainted partner's revive countdown shows (the ending rules' revive frames,
// shown from reviveCountdownShownAt); none while it stands, before the countdown shows or for an
// enemy.
[[nodiscard]] std::optional<std::int32_t> reviveSecondsOf(const game::battle::Battle& battle,
                                                          game::battle::FighterId fighter);

// A fighter's bars and name (battle_hud.c: BTL_renderPartnerStatusBars; battle_main.c:
// BTL_renderEnemyHPBars).
struct FighterView {
  std::string nameKey;
  std::int32_t hp = 0;
  std::int32_t maxHp = 0;
  std::int32_t mp = 0;
  std::int32_t maxMp = 0;
  bool fainted = false;
  // Whether the HUD shows its HP bar now: the partner's always, an enemy's for a while after
  // damage.
  bool hpBarShown = true;
  // The key of its status effect (battle.status.<name>), or empty without one.
  std::string statusKey;
  // A co-op ally's revive countdown in whole seconds while it shows (as the partner's
  // BattleView::reviveSeconds); none otherwise.
  std::optional<std::int32_t> reviveSeconds;
  // Its number in the battle, which the enemies' HP bars name it by.
  std::size_t id = 0;
  // A foe of the same species as another foe: its letter ("A", "B"...) in the battle's order, the
  // HUD's mark for it; empty otherwise.
  std::string tag;
  // In the Folder battle, the foe the next orders aim at and a foe an order aims at.
  bool targeted = false;
  bool queued = false;
  // A co-op ally's current order: the text key of its command, its Folder order's move or its
  // stance; empty for the others.
  std::string orderKey;
  // A co-op ally a guest's next thrown item flies to (the guest picks it in its bag).
  bool throwTarget = false;

  friend bool operator==(const FighterView&, const FighterView&) = default;
};

// An entry of the command menu (command_menu.c: BTL_renderCommandMenu): the command, its label and
// whether the partner takes it now.
struct CommandEntry {
  game::battle::Command command = game::battle::Command::None;
  std::string labelKey;
  bool available = true;
  // The command the partner follows now.
  bool current = false;
  // The icon the original draws for it (battle_main.c: BTL_setCommandIconUV): "command-<n>" by the
  // command's number, "special-<n>" by a technique's special effect, or empty for the finisher,
  // which has none.
  std::string icon;
  // The MP a technique costs the partner now (Battle::partnerMoveCost); 0 for the other commands.
  std::int32_t mpCost = 0;
  // In the Folder battle, the text key of why a technique or the finisher cannot be ordered now
  // (Battle::orderProblem); empty when it can, and in the Classic battle.
  std::string problemKey;

  friend bool operator==(const CommandEntry&, const CommandEntry&) = default;
};

// The partner's finisher gauge (battle_hud.c: BTL_renderFinisherGauge) and its charge during the
// aura (command_menu.c: BTL_tickFinisherChargeup).
struct FinisherGauge {
  std::int32_t progress = 0;
  std::int32_t goal = 0;
  bool ready = false;
  bool charging = false;
  std::int32_t charge = 0;

  friend bool operator==(const FinisherGauge&, const FinisherGauge&) = default;
};

// What the battle screen shows while the battle runs.
struct BattleView {
  // The partner first, then the enemies. In a co-op battle the partner is this player's own.
  std::vector<FighterView> fighters;
  // A co-op battle's other partners, fighting beside it (none in the original's battles).
  std::vector<FighterView> allies;
  std::vector<CommandEntry> menu;
  FinisherGauge finisher;
  // The seconds left to revive the fallen partner, once the countdown shows (command_menu.c:
  // BTL_renderDeathCountdown).
  std::optional<std::int32_t> reviveSeconds;
  // A tournament match's clock, the seconds left, from the start of the match to its end
  // (std_main.c: STD_addFighterCounter, STD_renderFighterCounter); no clock in other battles.
  std::optional<std::int32_t> matchSeconds;
  // The tamer's bag: whether it can open, whether it is open and whether a thrown item is on its
  // way.
  bool bagAvailable = false;
  bool bagOpen = false;
  bool itemInFlight = false;
  // The Folder battle's HUD for the own partner; nothing in the Classic battle.
  struct Folder {
    // The active order's move name key and phase ("processing"... "recovering"), with the timed
    // phase's progress in percent; empty while it has none.
    std::string moveKey;
    std::string phase;
    std::int32_t phasePercent = 0;
    // The waiting orders' move name keys, the next first, and the name key of each one's foe
    // (empty when it aims at none).
    std::vector<std::string> queue;
    std::vector<std::string> queueTargets;
    std::vector<std::string> queueTargetTags;
    // The active order's foe, by name key and letter (FighterView::tag).
    std::string activeTargetKey;
    std::string activeTargetTag;
    // The name key of the foe the next orders aim at (Change picks it); empty without one.
    std::string targetKey;
    std::string targetTag;
    // The stance the partner moves by now ("aggressive", "hold", "distance", "guard"); the break
    // holds it, for brokenSeconds more seconds.
    std::string stance;
    std::int32_t brokenSeconds = 0;
    // The active order's technique has thrown what lands (#425): a hit no longer stops it.
    bool committed = false;
    std::int32_t bar = 0;
    std::int32_t barMax = 0;
    bool broken = false;
    // For a few seconds after an order ends other than done (cancelled, interrupted, invalid,
    // replaced): the text key of how, its move's name key and the key of why (empty when the
    // ending says it all).
    std::string noticeKey;
    std::string noticeMoveKey;
    std::string noticeReasonKey;
    // A replaced order's notice: the move of the order that took its place.
    std::string noticeByKey;

    friend bool operator==(const Folder&, const Folder&) = default;
  };
  std::optional<Folder> folder;
};

// A line of the end box as the original builds it in BTL_END_BOX_TEXTBUFFER (battle_hud.c:
// BTL_appendItemDroppedText and the others): the pieces it joins, in order. A piece is a text key
// (the original's message, a name, or an engine line), or, when it starts with a brace, a mark of
// its own: {color:7} the names' yellow, {color:1} the white, {br} a new row and {wait} the row that
// ends the line and waits. The texts carry the same marks; {name}, {subject} and {amount} take the
// values below.
struct EndLine {
  std::vector<std::string> parts;
  // {name}: the key of a name (an item, a move) the line shows; {amount}: a number.
  std::string nameKey;
  std::int32_t amount = 0;
  // {subject}: the key of who the line is about.
  std::string subjectKey;

  friend bool operator==(const EndLine&, const EndLine&) = default;
};

// The original's messages of the end box (btl.battle_effect_messages, battle_hud.c), by slot.
enum class EndMessage : std::uint8_t {
  ListensTo = 0,
  Dropped = 1,
  WasInjured = 2,
  SetTechnique = 3,
  PutUpWithIt = 4,
  MoveAwayChangeTarget = 5,
  KeepItDown = 6,
  GoAllTheWay = 7,
  MpConsumptionBonus = 8,
  ReducedBy = 9,
  Learned = 10,
};

// The text key of one of those messages.
[[nodiscard]] std::string endMessageKey(EndMessage message);

// What the end box shows (battle_ui.c: handleBattleEndBox and the boxes after it): the outcome, the
// lines of the drops, the injury, the commands and the MP bonus the brain opened and the move
// learned, in the original's order, then the engine's own (lives and items a defeat took, who joins
// the city); the stat gains, the bits.
struct BattleEndView {
  std::string outcomeKey;
  std::vector<EndLine> lines;
  // HP, MP, offense, defense, speed and brain, as the partner took them, and their values before.
  std::array<std::int32_t, game::battle::StatCount> statGains{};
  std::array<std::int32_t, game::battle::StatCount> stats{};
  // Each stat's limit: the end box's bars are drawn against it (battle_ui.c:
  // renderPostBattleStatsBox).
  std::array<std::int32_t, game::battle::StatCount> statMaxima{};
  std::int32_t bits = 0;
  // The money with the bits already in: the final balance box counts up to it.
  std::int32_t money = 0;
  // The pacing of the boxes, in battle frames: the wait before the stats count up and after each
  // line.
  std::int32_t countDelayFrames = 0;
  std::int32_t waitFrames = 0;
};

// The battle screen's view of a battle under way, for the player who commands partner `own` (in a
// co-op battle an ally can be this player's; the others show as its allies).
[[nodiscard]] BattleView battleViewOf(const game::battle::Battle& battle, const NameKeys& names,
                                      game::battle::FighterId own = game::battle::PartnerId);

// The end box of a finished battle and what the session did with it.
[[nodiscard]] BattleEndView battleEndViewOf(const game::battle::BattleResult& result,
                                            const session::BattleAftermath& aftermath,
                                            const NameKeys& names);

// The views as the UI settings the battle document reads.
[[nodiscard]] ScreenSettings settingsOf(const BattleView& view);
[[nodiscard]] ScreenSettings settingsOf(const BattleEndView& view);

} // namespace dwe::screens
