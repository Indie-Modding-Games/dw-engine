#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/fixed_step.hpp>
#include <dwe/game/battle/combatant.hpp>
#include <dwe/game/battle/events.hpp>
#include <dwe/game/battle/flights.hpp>
#include <dwe/game/battle/ground.hpp>
#include <dwe/game/battle/hit_particles.hpp>
#include <dwe/game/battle/items.hpp>
#include <dwe/game/battle/moves.hpp>
#include <dwe/game/battle/rules.hpp>
#include <dwe/game/battle/state.hpp>
#include <dwe/game/battle/status_visuals.hpp>
#include <dwe/game/battle/strategies.hpp>
#include <dwe/ruleset/random.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace dwe::game::battle {

struct BattleContext;

// The battle runs at the original's 20 frames a second on its own fixed tick: the game clock stands
// still during a battle (tamer.c: startBattle), which then moves it on by a fixed amount
// (BattleResult::elapsedMinutes).
inline constexpr std::int32_t FramesPerSecond = 20;

// What the battle needs to know about the partner beyond its combatant: the care values the rules
// read.
struct PartnerCondition {
  std::int32_t discipline = 0;
  std::int32_t tiredness = 0;
  bool sick = false;
  bool injured = false;
  // The moves it already knows, so it does not learn them again.
  std::vector<std::string> knownMoves;
};

// What a battle is for the session: the tournaments and the arena have their own results and
// autosave; a versus match (two players' partners) has no consequence at all.
enum class BattleKind : std::uint8_t { Normal, Tournament, Arena, Versus };

// A versus match (vs_combat.c): the one enemy is the other player's partner, which that player
// commands as the partner is commanded (Battle::command with RivalId). It needs a time limit, as a
// tournament match, and the clock, the end and the shared cooldowns follow the tournament's
// (VS__checkEndCondition, VS_DISABLE_HITTING, P2_AOE_TIMER). A victory is the first player's win.
struct VersusSetup {
  // VS_DISCIPLINE[1], from the rival's registered record.
  std::int32_t rivalDiscipline = 0;
};

struct BattleSetup {
  Combatant partner;
  PartnerCondition condition;
  // One to limits.max_enemies.
  std::vector<Combatant> enemies;
  // A battle of the story: no fleeing, and enemies spend no MP (btl.c: FLEE_DISABLED).
  bool scripted = false;
  // A battle without the tamer: the partner takes no commands (battle_tick.c: handleCommands,
  // trigger 2).
  bool tamerless = false;
  // The map it happens on: some maps give no drops.
  std::string map;
  // The minute of the hour it starts at, for the hour the battle may cross.
  std::int32_t startMinute = 0;
  // Where the tamer stands; a fleeing partner runs there.
  Point tamer;
  // Set by the session and handed back in the result. A tournament match gives no rewards and costs
  // no care, lives or time (the STD overlay has no end box: std_main.c: STD_deinitializeCombat).
  BattleKind kind = BattleKind::Normal;
  // A tournament match's clock in seconds (std_main.c: STD_addFighterCounter(99)): it ends the
  // fight when it runs out, and a fallen partner loses at once, with no time to revive it
  // (STD_checkEndCondition).
  std::optional<std::int32_t> timeLimit;
  // The resident that joins the city when this battle is won: in the original the map's script
  // decides it after the battle (a recruitment fight), so the session names it here and reads it
  // back in the result.
  std::optional<std::string> recruitOnVictory;
  // Only for BattleKind::Versus: the rival is commanded and both menus are the versus menus.
  std::optional<VersusSetup> versus;
};

enum class Outcome : std::uint8_t { Victory, Fled, Defeat };

// The stats in the order the original lists them (HP, MP, offense, defense, speed, brain).
enum class StatIndex : std::uint8_t { Hp, Mp, Offense, Defense, Speed, Brain };
inline constexpr std::size_t StatCount = 6;
using StatValues = std::array<std::int32_t, StatCount>;

// The battle's results. The battle never touches the partner: the session applies these.
// An item a beaten enemy leaves (battle_ui.c: battleStatsGainsAndDrops), with the enemy's species
// and where it stood: item.c: spawnDroppedItems lays it on the ground there once the end box
// closes.
struct Drop {
  std::string item;
  std::string species;
  Point position;

  friend bool operator==(const Drop&, const Drop&) = default;
};

struct BattleResult {
  Outcome outcome = Outcome::Defeat;
  std::int64_t frames = 0;
  // The partner's HP and MP at the end, a defeat's too: the death map's script gives those back
  // (dg 212, section 0x48).
  std::int32_t partnerHp = 0;
  std::int32_t partnerMp = 0;
  // On a victory: what each stat goes up by (battle_ui.c: battleStatsGainsAndDrops), the bits and
  // items won and the moves learned.
  StatValues statGains{};
  std::int32_t bits = 0;
  std::vector<Drop> drops;
  std::vector<std::string> learnedMoves;
  // battle_ui.c: handleBattleInjury: the partner got injured.
  bool injured = false;
  // battle_hud.c: BTL_appendCommandLearnedText: the menu entries the brain's raise opened, when it
  // reached a menu it was below (only the highest menu reached counts).
  std::vector<MenuEntry> commandsLearned;
  // battle_hud.c: BTL_appendMPBonusText: the MP discount, in percent, the brain's raise reached
  // when it was below it (0 for none).
  std::int32_t mpDiscountLearned = 0;
  // The partner's species, which the end box names in the injury line.
  std::string partnerSpecies;
  // tamer.c: startBattle and partner_impl.c: handlePostBattleTiredness: what the outcome does to
  // the care values.
  CareChange care;
  std::int32_t foodChange = 0;
  // tamer.c: startBattle (a defeat): lives lost. The items lost are the inventory section's
  // (defeat.lost_percent).
  std::int32_t livesLost = 0;
  // tamer.c: advanceBattleTime: the minutes the clock moves on, and whether that ends an hour (an
  // hour of lifetime and the hourly care counters of the partner).
  std::int32_t elapsedMinutes = 0;
  bool hourCrossed = false;
  std::int32_t lifetimeHoursLost = 0;
  // What the partner did, for the session and the simulator.
  BattleCounters counters;
  // The setup's kind, and the resident that joins the city now (only after a victory).
  BattleKind kind = BattleKind::Normal;
  std::optional<std::string> recruitJoined;
  // A tournament match: the clock ran out (the less damaged side won), or both sides fell or took
  // the same damage; a draw is a defeat (std_main.c: STD_deinitializeCombat(1, 1)).
  bool timeUp = false;
  bool draw = false;
};

// One battle, frame by frame. A UI calls the inputs as the player acts and step() once per frame
// (or advance() with the real time); a simulator calls step() until finished(). The same setup,
// rules and seed play the same battle.
class Battle {
public:
  // Prepares the battle (battle_setup.c: BTL_initializeCombat). The rules and the catalog must
  // outlive it.
  static core::Result<Battle> start(BattleSetup setup, const BattleRules& rules,
                                    const MoveCatalog& moves, BattleStrategies strategies,
                                    std::uint64_t seed);

  // The player picks a command of the menu (battle_tick.c: handleCommands).
  core::Result<void> command(Command command);
  // The same for a commanded side: PartnerId, or RivalId in a versus match (vs_combat.c:
  // VS___tickVSInput: the command takes no delay; the finisher only once its gauge is full).
  core::Result<void> command(Command command, FighterId side);
  // A press of the finisher charge buttons during the partner's finisher aura (command_menu.c:
  // BTL_tickFinisherChargeup); at most one counts per frame.
  void pressFinisherCharge();
  // Where the player stands now: it runs back as the battle opens (PlayerRetreat), and a fleeing
  // partner runs there.
  void movePlayer(Point point) { state_.tamer = point; }
  // A debug shortcut to the end of the fight: every enemy takes damage enough to fall, which then
  // runs as a beating blow would (fainting, the victory, the drops, the stat gains).
  void defeatEnemies();
  // Its opposite: the partner takes damage enough to fall (a route that checks a lost fight).
  void defeatPartner();
  // A cup match's clock runs out now (a route that checks the time-up notice): the side that should
  // win (none: a draw) loses HP until its damage is less than the other's (or the same); nothing
  // outside a match.
  void runOutClock(std::optional<bool> partnerWins);
  // The tamer's bag (battle_tick.c: tamerTickBattle, inventory.c: addInventoryUI): it opens in a
  // battle with the tamer while the partner is not running away and no thrown item is on its way.
  // While it is open the partner takes no command; the battle goes on.
  // No bag in a tournament match (std_hud.c: STD_tickTamerTournament only gives orders).
  [[nodiscard]] bool bagAvailable() const;
  [[nodiscard]] bool bagOpen() const { return state_.bagOpen; }
  core::Result<void> openBag();
  void closeBag();
  // Throws an item at the partner (main.c: startThrowingItem): the bag closes, the item leaves the
  // player's hand on frame releaseFrame of the throw and lands on the last of its flightFrames,
  // always on the partner (tickThrownItem). The defeat countdown waits while it flies.
  core::Result<void> throwItem(ThrownItem item);
  [[nodiscard]] bool itemInFlight() const { return state_.thrownItem.has_value(); }
  // The frame of the thrown item's flight (itemThrowHeight, stepItemFlight), or none while no item
  // flies or it is still in the player's hand.
  [[nodiscard]] std::optional<std::int32_t> thrownItemFlightFrame() const;
  // Whether the HUD shows a fighter's HP bar: always the partner's, an enemy's for a while after
  // damage (battle_main.c: BTL_renderEnemyHPBars).
  [[nodiscard]] bool hpBarShown(FighterId fighter) const;

  // Item effects on the partner (item.c): restore HP and MP, cure its status effects, keep new ones
  // off, revive it after it fell (before the countdown ends).
  void restorePartner(std::int32_t hp, std::int32_t mp);
  void curePartnerStatus();
  void protectPartner();
  core::Result<void> revivePartner(std::int32_t hp);

  // Rolls from here on with the original's generator from its state (ruleset::Random::original),
  // for an oracle run: the original's state as its fight began gives the same first decisions.
  // The original's hit sparks (HitParticles) then roll from it too, and the effect scripts take
  // theirs from originalRolls().
  void useOriginalRolls(std::uint32_t state) {
    random_ = ruleset::Random::original(state);
    originalRolls_ = true;
  }
  // The battle's rolls when they are the original's (useOriginalRolls), for the effect scripts
  // that run with the battle (EffectRunner::useRolls); nothing otherwise.
  [[nodiscard]] ruleset::Random* originalRolls() { return originalRolls_ ? &random_ : nullptr; }
  // The hit sparks, the particles they leave on the fighters and their clouds, which the effect
  // scripts' attacker particles share (EffectRunner::useParticles), and the rolls they take: the
  // battle's with the original's, else their own.
  [[nodiscard]] HitParticles& hitParticles() { return hitParticles_; }
  [[nodiscard]] const HitParticles& hitParticles() const { return hitParticles_; }
  [[nodiscard]] ruleset::Random& particleRolls() {
    return originalRolls_ ? random_ : hitParticles_.ownRolls();
  }
  // Where the battle's rolls are (ruleset::Random::state), for a state trace to compare.
  [[nodiscard]] std::uint64_t rollState() const { return random_.state(); }

  // The ground the fighters walk on, which must outlive the battle; none keeps them within the
  // ruleset's arena radius.
  void setGround(const BattleGround* ground) { ground_ = ground; }
  // The clips of a fighter's model (motion.hpp): from then on its clips move it and time its
  // attacks, hit reactions, blocks and faint. A fighter without them walks at the ruleset's walk
  // speed and keeps the ruleset's animation lengths.
  void setClips(FighterId fighter, FighterClips clips);
  // When the techniques' attacks reach their targets (flights.hpp), which must outlive the battle;
  // none flies them at the ruleset's projectile speed.
  void setFlights(const AttackFlights* flights) { flights_ = flights; }

  // Runs one frame and returns what happened in it, with what the inputs since the last frame did
  // first.
  const BattleEvents& step();
  // Runs the frames that `seconds` of real time hold and returns their events.
  BattleEvents advance(double seconds);
  // The same, also handing each frame's events to `onFrame` right after that frame, for what runs
  // in step with the battle's frames (the effect scripts).
  BattleEvents advance(double seconds, const std::function<void(const BattleEvents&)>& onFrame);

  [[nodiscard]] bool finished() const { return result_.has_value(); }
  [[nodiscard]] const std::optional<BattleResult>& result() const { return result_; }
  [[nodiscard]] const BattleState& state() const { return state_; }
  // The commands the player can choose now.
  [[nodiscard]] const std::vector<Command>& menu() const { return state_.commands.menu; }
  // A commanded side's (PartnerId, or RivalId in a versus match).
  [[nodiscard]] const std::vector<Command>& menuOf(FighterId side) const {
    return state_.commandsOf(side).menu;
  }
  // A hash of everything the next frames depend on (the fighters, the commands, the attacks in
  // flight, the finisher, the clock and the rolls): two battles that agree on it play on alike. A
  // versus match compares it each frame between the two machines.
  [[nodiscard]] std::uint64_t checksum() const;
  [[nodiscard]] const BattleRules& rules() const { return *rules_; }
  [[nodiscard]] const MoveCatalog& moves() const { return *moves_; }
  // What the fighters' statuses show over them now.
  [[nodiscard]] const StatusVisuals& statusVisuals() const { return statusVisuals_; }

private:
  Battle(const BattleRules& rules, const MoveCatalog& moves, BattleStrategies strategies,
         std::uint64_t seed);

  void finish(Outcome outcome, bool draw = false);
  // std_main.c: STD_checkEndCondition, for a tournament match: whether it ended this frame.
  bool endMatch();
  // The frame's context, with its events going to `events`.
  BattleContext contextOf(BattleEvents& events);
  // The events not yet returned by step(): those of the last frame are dropped once returned.
  BattleEvents& pending();

  const BattleRules* rules_;
  const MoveCatalog* moves_;
  BattleStrategies strategies_;
  ruleset::Random random_;
  // Whether random_ is the original's generator (useOriginalRolls).
  bool originalRolls_ = false;
  HitParticles hitParticles_;
  StatusVisuals statusVisuals_;
  BattleSetup setup_;
  BattleState state_;
  BattleEvents events_;
  std::optional<BattleResult> result_;
  core::FixedStep clock_;
  const BattleGround* ground_ = nullptr;
  const AttackFlights* flights_ = nullptr;
  bool chargePressed_ = false;
  bool delivered_ = false;
};

} // namespace dwe::game::battle
