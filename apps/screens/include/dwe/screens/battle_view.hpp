#pragma once

#include <dwe/game/battle/battle.hpp>
#include <dwe/game/inventory/stacking.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/battle_adapter.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::screens {

// The settings of the battle screen (ui/battle/document), which refreshes on BattleRevisionSetting.
inline constexpr std::string_view BattleRevisionSetting = "battle.revision";
inline constexpr std::string_view BattleFighterList = "battle.fighter";
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
  // The partner first, then the enemies.
  std::vector<FighterView> fighters;
  std::vector<CommandEntry> menu;
  FinisherGauge finisher;
  // The seconds left to revive the fallen partner, once the countdown shows (command_menu.c:
  // BTL_renderDeathCountdown).
  std::optional<std::int32_t> reviveSeconds;
  // The tamer's bag: whether it can open, whether it is open and whether a thrown item is on its
  // way.
  bool bagAvailable = false;
  bool bagOpen = false;
  bool itemInFlight = false;
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

// The battle screen's view of a battle under way.
[[nodiscard]] BattleView battleViewOf(const game::battle::Battle& battle, const NameKeys& names);

// The end box of a finished battle and what the session did with it.
[[nodiscard]] BattleEndView battleEndViewOf(const game::battle::BattleResult& result,
                                            const session::BattleAftermath& aftermath,
                                            const NameKeys& names);

// The views as the UI settings the battle document reads.
[[nodiscard]] ScreenSettings settingsOf(const BattleView& view);
[[nodiscard]] ScreenSettings settingsOf(const BattleEndView& view);

} // namespace dwe::screens
