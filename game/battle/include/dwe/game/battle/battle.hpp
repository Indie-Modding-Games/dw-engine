#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/fixed_step.hpp>
#include <dwe/game/battle/combatant.hpp>
#include <dwe/game/battle/events.hpp>
#include <dwe/game/battle/moves.hpp>
#include <dwe/game/battle/rules.hpp>
#include <dwe/game/battle/state.hpp>
#include <dwe/game/battle/strategies.hpp>
#include <dwe/ruleset/random.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dwe::game::battle {

// The battle runs at the original's 20 frames a second on its own fixed tick: the game clock stands still during a
// battle (tamer.c: startBattle), which then moves it on by a fixed amount (BattleResult::elapsedMinutes).
inline constexpr std::int32_t FramesPerSecond = 20;

// What the battle needs to know about the partner beyond its combatant: the care values the rules read.
struct PartnerCondition {
    std::int32_t discipline = 0;
    std::int32_t tiredness = 0;
    bool sick = false;
    bool injured = false;
    // The moves it already knows, so it does not learn them again.
    std::vector<std::string> knownMoves;
};

// What a battle is for the session: the tournaments and the arena have their own results and autosave.
enum class BattleKind : std::uint8_t { Normal, Tournament, Arena };

struct BattleSetup {
    Combatant partner;
    PartnerCondition condition;
    // One to limits.max_enemies.
    std::vector<Combatant> enemies;
    // A battle of the story: no fleeing, and enemies spend no MP (btl.c: FLEE_DISABLED).
    bool scripted = false;
    // A battle without the tamer: the partner takes no commands (battle_tick.c: handleCommands, trigger 2).
    bool tamerless = false;
    // The map it happens on: some maps give no drops.
    std::string map;
    // The minute of the hour it starts at, for the hour the battle may cross.
    std::int32_t startMinute = 0;
    // Where the tamer stands; a fleeing partner runs there.
    Point tamer;
    // Set by the session and handed back in the result.
    BattleKind kind = BattleKind::Normal;
    // The resident that joins the city when this battle is won: in the original the map's script decides it after
    // the battle (a recruitment fight), so the session names it here and reads it back in the result.
    std::optional<std::string> recruitOnVictory;
};

enum class Outcome : std::uint8_t { Victory, Fled, Defeat };

// The stats in the order the original lists them (HP, MP, offense, defense, speed, brain).
enum class StatIndex : std::uint8_t { Hp, Mp, Offense, Defense, Speed, Brain };
inline constexpr std::size_t StatCount = 6;
using StatValues = std::array<std::int32_t, StatCount>;

// The battle's results. The battle never touches the partner: the session applies these.
struct BattleResult {
    Outcome outcome = Outcome::Defeat;
    std::int64_t frames = 0;
    // The partner's HP and MP at the end.
    std::int32_t partnerHp = 0;
    std::int32_t partnerMp = 0;
    // On a victory: what each stat goes up by (battle_ui.c: battleStatsGainsAndDrops), the bits and items won and the
    // moves learned.
    StatValues statGains{};
    std::int32_t bits = 0;
    std::vector<std::string> drops;
    std::vector<std::string> learnedMoves;
    // battle_ui.c: handleBattleInjury: the partner got injured.
    bool injured = false;
    // tamer.c: startBattle and partner_impl.c: handlePostBattleTiredness: what the outcome does to the care values.
    CareChange care;
    std::int32_t foodChange = 0;
    // tamer.c: startBattle (a defeat): lives lost and the share of the items lost.
    std::int32_t livesLost = 0;
    std::int32_t itemLossPercent = 0;
    // tamer.c: advanceBattleTime: the minutes the clock moves on, and whether that ends an hour (an hour of lifetime
    // and the hourly care counters of the partner).
    std::int32_t elapsedMinutes = 0;
    bool hourCrossed = false;
    std::int32_t lifetimeHoursLost = 0;
    // What the partner did, for the session and the simulator.
    BattleCounters counters;
    // The setup's kind, and the resident that joins the city now (only after a victory).
    BattleKind kind = BattleKind::Normal;
    std::optional<std::string> recruitJoined;
};

// One battle, frame by frame. A UI calls the inputs as the player acts and step() once per frame (or advance() with
// the real time); a simulator calls step() until finished(). The same setup, rules and seed play the same battle.
class Battle {
public:
    // Prepares the battle (battle_setup.c: BTL_initializeCombat). The rules and the catalog must outlive it.
    static core::Result<Battle> start(BattleSetup setup, const BattleRules& rules, const MoveCatalog& moves,
                                      BattleStrategies strategies, std::uint64_t seed);

    // The player picks a command of the menu (battle_tick.c: handleCommands).
    core::Result<void> command(Command command);
    // A press of the finisher charge buttons during the partner's finisher aura (command_menu.c:
    // BTL_tickFinisherChargeup); at most one counts per frame.
    void pressFinisherCharge();
    // Item effects on the partner (item.c): restore HP and MP, cure its status effects, keep new ones off, revive it
    // after it fell (before the countdown ends).
    void restorePartner(std::int32_t hp, std::int32_t mp);
    void curePartnerStatus();
    void protectPartner();
    core::Result<void> revivePartner(std::int32_t hp);

    // Runs one frame and returns what happened in it, with what the inputs since the last frame did first.
    const BattleEvents& step();
    // Runs the frames that `seconds` of real time hold and returns their events.
    BattleEvents advance(double seconds);

    [[nodiscard]] bool finished() const { return result_.has_value(); }
    [[nodiscard]] const std::optional<BattleResult>& result() const { return result_; }
    [[nodiscard]] const BattleState& state() const { return state_; }
    // The commands the player can choose now.
    [[nodiscard]] const std::vector<Command>& menu() const { return state_.commands.menu; }
    [[nodiscard]] const BattleRules& rules() const { return *rules_; }
    [[nodiscard]] const MoveCatalog& moves() const { return *moves_; }

private:
    Battle(const BattleRules& rules, const MoveCatalog& moves, BattleStrategies strategies, std::uint64_t seed);

    void finish(Outcome outcome);
    // The events not yet returned by step(): those of the last frame are dropped once returned.
    BattleEvents& pending();

    const BattleRules* rules_;
    const MoveCatalog* moves_;
    BattleStrategies strategies_;
    ruleset::Random random_;
    BattleSetup setup_;
    BattleState state_;
    BattleEvents events_;
    std::optional<BattleResult> result_;
    core::FixedStep clock_;
    bool chargePressed_ = false;
    bool delivered_ = false;
};

} // namespace dwe::game::battle
