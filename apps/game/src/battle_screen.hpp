#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/game/battle/player_retreat.hpp>
#include <dwe/screens/battle_intro.hpp>
#include <dwe/screens/battle_view.hpp>
#include <dwe/screens/command_menu.hpp>
#include <dwe/screens/command_shout.hpp>
#include <dwe/screens/damage_numbers.hpp>
#include <dwe/screens/death_countdown.hpp>
#include <dwe/screens/enemy_hp_bars.hpp>
#include <dwe/screens/partner_bars.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/battle_effects.hpp>
#include <dwe/session/field_items_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/session_music.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "battle_effect_bodies.hpp"
#include "battle_ground.hpp"
#include "fighter_figure.hpp"

namespace dwe::app {

// The battle screen's document and the commands it sends.
inline constexpr std::string_view BattleDocument = "battle";
inline constexpr std::string_view BattleCommandName = "battle_command";
inline constexpr std::string_view BattleChargeCommand = "battle_charge";
inline constexpr std::string_view BattleCloseCommand = "battle_close";
// The end box's count: a point or a bit counted, and the bits all in (the sounds of battle_ui.c).
inline constexpr std::string_view BattleEndCountCommand = "battle_end_count";
inline constexpr std::string_view BattleEndCountedCommand = "battle_end_counted";
// The tamer's bag (battle_tick.c: tamerTickBattle opens it with its own button): open, close, and
// throw the item of a slot (the command's number) to the partner. The bag is the field's own
// (inventory.c: addInventoryUI, the partner menu's items page); its Use throws the item.
inline constexpr std::string_view BattleBagOpenCommand = "battle_bag_open";
inline constexpr std::string_view BattleBagCloseCommand = "battle_bag_close";
inline constexpr std::string_view BattleThrowCommand = "battle_throw";
// What the battle screen shows: the fight or the end box.
inline constexpr std::string_view BattlePhaseSetting = "battle.phase";
// Whether a battle runs, from its opening to the end box's close: the HUD hides what the battle's
// panels would overlap (the minimap, which the original lacks).
inline constexpr std::string_view BattleActiveSetting = "battle.active";
inline constexpr std::string_view IntroPhase = "intro";
inline constexpr std::string_view FightPhase = "fight";
// A victory's poses before the end box (battle_main.c: BTL_handleVictorySequence): no box, no
// input.
inline constexpr std::string_view VictoryPhase = "victory";
inline constexpr std::string_view EndPhase = "end";
// After the end box, while the command strip goes back up (battle_main.c:777).
inline constexpr std::string_view ClosingPhase = "closing";

class FieldScene;

struct BattleScreenDesc {
  session::GameSession* session = nullptr;
  session::BattleAdapter* battle = nullptr;
  session::InventoryAdapter* inventory = nullptr;
  // Where the beaten enemies' drops land; it may be null (then they are lost).
  session::FieldItemsAdapter* fieldItems = nullptr;
  // What the fighters walk on: the field's camera and map; it may be null (then the ruleset's arena
  // radius holds them).
  const game::battle::BattleGround* ground = nullptr;
  // Where the player and the partner strike the victory poses; it may be null (then the end box
  // opens with the poses' sound).
  FieldScene* scene = nullptr;
  // For the fighters' names; it may be null.
  const assets::DigimonTable* species = nullptr;
  // The mounted packs, for the techniques' effect scripts; without them (or without sound) the
  // techniques play no sounds.
  const assets::VirtualFileSystem* files = nullptr;
  ui::Ui* ui = nullptr;
};

// Plays a battle on the battle document: starts it through the session's battle adapter, runs it on
// the game loop's ticks, passes the player's commands to it, publishes its view, and once it ends
// gives the result to the session and shows the end box until the player closes it. The rules are
// the battle's; this only shows and forwards.
// An NPC leaving the battle's ground, as the battle opens or after a victory: the field shows it on
// `figure`; it walks to its flee point (after a victory, once it has got up).
struct Leaver {
  std::int32_t npc = 0;
  FighterFigure figure;
  bool walking = false;
};

class BattleScreen {
public:
  explicit BattleScreen(BattleScreenDesc desc) : desc_(desc) {}

  // `enemyNpcs` are the slots of the map's NPCs the enemies are, in their order: each enemy is
  // shown on its NPC (none: the enemies are not shown). A fixed fight cannot be fled
  // (FLEE_DISABLED): its enemies stay on the map after it.
  core::Result<void> start(session::BattleRequest request, std::vector<std::int32_t> enemyNpcs,
                           bool fixedFight = false);

  // The slots of the NPCs on screen that join a battle with no predefined enemies, started by
  // touching the NPC in slot `touched` (btl.c: handleBattleStart); rolls the session's random
  // numbers. The NPCs with the touched one's script id roll as any other.
  [[nodiscard]] std::vector<std::int32_t> joiningNpcs(std::int32_t touched, bool fixedFight);

  // Whether the battle, its end box, the beaten enemies' exit or the command strip going back up
  // is on screen: the field and the game clock wait meanwhile.
  [[nodiscard]] bool active() const {
    return battle_.has_value() || ending_ || exit_.has_value() || closing_;
  }
  // Enemy life bars (an engine improvement, play_session.hpp: EnemyHpBarsSetting): a bar over every
  // enemy still standing for the whole fight, cups too. Off, the original's: a wild enemy's bar a
  // while after it is hit (Battle::hpBarShown) and none in a cup.
  void setEnemyHpBars(bool always);
  // Whether a fight runs (from its start to its end box), the time a quick battle plays at x2.
  [[nodiscard]] bool fighting() const { return battle_.has_value(); }
  // Whether the player has to react in time: the finisher's charge (command_menu.c:
  // BTL_tickFinisherChargeup), the fallen partner's countdown (BTL_tickDeathCountdown), the bag
  // open over the fight or an item in the hand or on its way. A quick battle waits at the
  // original's speed meanwhile (battle_speed.hpp).
  [[nodiscard]] bool reacting() const;
  // The outcome of the last battle, once, after its end box is closed: what a story battle reads.
  [[nodiscard]] std::optional<game::battle::Outcome> takeOutcome();
  // The battle_win debug command: the enemies fall at once; false when no fight is running.
  bool defeatEnemies();
  // The battle_lose debug command: the partner falls at once; false when no fight is running.
  bool defeatPartner();
  // The battle_time_up debug command: a cup match's clock runs out, `side` ("win", "lose" or ""
  // for a draw) having dealt more damage; false when no fight is running.
  bool runOutClock(std::string_view side);
  // The battle on screen until its end box closes, for a bug report and --trace; nothing otherwise.
  [[nodiscard]] const game::battle::Battle* battle() const { return battle_ ? &*battle_ : nullptr; }
  // The slots of the map NPCs the enemies are, in the battle's order.
  [[nodiscard]] std::span<const std::int32_t> enemyNpcs() const { return enemyNpcs_; }
  // The opening's banner is on screen (a cup's camera pulls out meanwhile: std_main.c:
  // STD_combatSetup).
  [[nodiscard]] bool introShowing() const { return intro_.has_value(); }
  // A cup match's end waits while the cup shows what follows the time running out (std_main.c:
  // STD_tickBattleResultScreen runs before STD_deinitializeCombat).
  void holdMatchEnd(bool hold) { matchEndHeld_ = hold; }
  // A cup match is over and its end (the winner's pose) plays.
  [[nodiscard]] bool matchEnding() const { return matchEndSeconds_.has_value(); }

  // One logic tick of the battle and the view of it. The battle runs once its opening is over.
  void tick(double seconds);

  // Where an enemy's anger mark hangs on the screen, `lift` world units over its feet; the field
  // scene knows (play_session.cpp).
  using EnemyAnchor = std::function<screens::AngerAnchor(const game::battle::Combatant&, float)>;
  // Once a drawn frame: places the anger marks of the opening over the enemies, while they show,
  // and hands the opening's frame to its document.
  void showIntro(const EnemyAnchor& anchor);

  // Takes the battle document's commands; false for any other command.
  bool handle(const ui::UiCommand& command);
  // The original's square button (battle_tick.c: handleCommands): calls the finisher once its
  // gauge is full, whatever command the strip has.
  void pressFinisher();

private:
  [[nodiscard]] screens::NameKeys names() const;
  void publish() const;
  void tickIntro(double seconds);
  // battle_screen_retreat.cpp: the player's run back during the banner.
  void startRetreat();
  void stepRetreat();
  void endRetreat();
  void placePlayer(game::battle::Point at, game::battle::Facing facing) const;
  void standPlayer() const;
  // battle_screen_retreat.cpp: the player's run out when the partner is called back, and the fade
  // to black over it.
  void stepEscape();
  void startEscape();
  void fadeEscape();
  void endEscape();
  void facePlayerToPartner(game::battle::Point partner) const;
  void tickVictory(double seconds);
  void tickMatchEnd(double seconds);
  void startPoses();
  void tickPoses(double seconds);
  void releasePoses();
  void openEndBox() const;
  void playSound(std::int64_t sound) const;
  void publishIntro() const;
  void tickBattle(double seconds);
  void takeFigures();
  void showEffects() const;
  void showStatusVisuals() const;
  void stepFieldEffects(const game::battle::BattleEvents& frame) const;
  // The numbers the frame's hits, blocks and poison pop out of the fighters, and one frame of them.
  void stepNumbers(const game::battle::BattleEvents& frame);
  void tickNumbers(double seconds);
  void publishNumbers() const;
  // The enemies' HP bars beside them (battle_main.c: BTL_renderEnemyHPBars); none without a battle.
  void publishHpBars();
  void showFighters(double seconds);
  void standPartner(std::uint32_t clip, std::string_view origin);
  void openBag();
  // Keeps the bag's document and the battle's bag together: the bag closes when its document goes,
  // and the document goes when the battle closed the bag or ended.
  void syncBag();
  void throwItem(double slot);
  // battle_screen_throw.cpp: the player's throw and the item on its way to the partner (main.c:
  // startThrowingItem, tickThrownItem, renderThrownItem), a battle frame at a time.
  void startThrow(assets::ItemHandle item);
  void stepThrow();
  void holdThrown();
  void showThrown() const;
  void endThrow();
  void finish();
  void close();
  // battle_screen_exit.cpp: what the battle leaves on the map.
  [[nodiscard]] bool offScreen(std::int32_t npc, float x, float z) const;
  [[nodiscard]] std::vector<std::int32_t> npcsOnScreen() const;
  // The fighting partner on the field's ground, in world axes.
  [[nodiscard]] glm::vec3 partnerWorld() const;
  // The NPCs not fighting that run off as the battle opens (battle_setup.c: BTL_handleBattleIntro).
  void startFlight();
  // Where the run-off marks hang, `lift` over each runner's species height, as the anger marks.
  [[nodiscard]] std::vector<screens::AngerAnchor> runOffAnchors(float lift) const;
  [[nodiscard]] screens::AngerAnchor keptOnScreen(screens::AngerAnchor anchor) const;
  [[nodiscard]] bool stepFlight(double seconds);
  void endFlight();
  void startExit();
  [[nodiscard]] float walkSpeed(std::int32_t npc) const;
  void startWalk(Leaver& leaver) const;
  void tickExit(double seconds);
  void endExit();
  // command_menu.c: the command strip's motion, a frame at the battle's rate outside the fight and
  // the opening, and its slide back up once the battle is over.
  void stepMenu(bool fighting);
  // The strip's and the partner's bars' settings, under the strip's revision.
  void publishMenu() const;
  // command_shout.c: the player's shout with a command the partner takes, and its frames.
  void shout(game::battle::Command command);
  void stepShout();
  void publishShout() const;
  void tickMenu(double seconds);
  void closeMenu();
  [[nodiscard]] session::BattleMusicFacts musicFactsOf(const session::BattleRequest& request) const;

  BattleScreenDesc desc_;
  std::optional<game::battle::Battle> battle_;
  // data/battle's flattened_shots: each original type's shot sprite column, read on the first
  // battle; empty when the packs have none (an extraction made before dwx read it).
  std::optional<std::map<std::string, std::int32_t, std::less<>>> shotColumns_;
  // What the effects read of the fighters; it outlives effects_.
  std::optional<BattleEffectBodies> bodies_;
  std::optional<FighterGround> ground_;
  std::optional<session::BattleEffects> effects_;
  // The effects' frames so far, for the scene to tell a new frame from the same one shown again
  // (FieldScene::setEffects).
  std::uint64_t effectFrames_ = 0;
  // When the techniques' effects reach their targets (game::battle::ScriptedFlights), from the
  // effect scripts and the bodies.
  std::optional<game::battle::ScriptedFlights> flights_;
  bool ending_ = false;
  // The field's bag document is open over the battle.
  // Enemy life bars over every enemy still standing (setEnemyHpBars).
  bool enemyHpBarsAlways_ = false;
  bool bagShown_ = false;
  // The numbers over the fighters (entity_text.c), and the time they have not run yet once the
  // battle is over: they go on through the victory until the last one goes.
  screens::DamageNumbers numbers_;
  double numbersSeconds_ = 0.0;
  // The opening before the fight (battle_setup.c: BTL_handleBattleIntro), the enemies it marks and
  // where, and the time it has not run yet. Each battle seeds its own from their count.
  std::optional<screens::BattleIntro> intro_;
  std::vector<game::battle::Combatant> introEnemies_;
  std::vector<screens::AngerAnchor> angerAnchors_;
  // The run-off marks over the NPCs on screen that are not fighting (btl.c: setFleeBubble(id, 1)).
  std::vector<screens::AngerAnchor> fleeAnchors_;
  double introSeconds_ = 0.0;
  // The player running back while the banner shows (BTL_handleBattleIntro).
  std::optional<game::battle::PlayerRetreat> retreat_;
  // The player running out once the partner is called back (handleFleeing), and the shade the
  // screen held as its fade to black started.
  std::optional<game::battle::PlayerFlight> escape_;
  std::optional<std::int32_t> escapeShadeFrom_;
  // Where the partner stood as the fight's last frame began: the player looks there
  // (tamerTickBattle runs before BTL_battleMain moves the fighters).
  std::optional<game::battle::Point> partnerBeforeFrame_;
  std::uint32_t battlesStarted_ = 0;
  std::optional<game::battle::Outcome> outcome_;
  // Whether the battle on the end box was won: the field's music comes back when it closes.
  bool won_ = false;
  // A tournament match's songs (session::BattleRequest::matchMusic).
  std::optional<session::MatchMusic> matchMusic_;
  // The won or lost match's song, played once the match's end is no longer held.
  std::optional<session::MusicSong> matchEndSong_;
  // The time left before the victory poses' sound, once a battle is won.
  std::optional<double> victoryPoseSeconds_;
  // A tournament match's end: the time left of the winner's pose and the frames after it, while
  // they run (the match has no end box).
  std::optional<double> matchEndSeconds_;
  bool matchEndHeld_ = false;
  // The time the victory poses have played, while they do, and which of the two still plays.
  std::optional<double> posesSeconds_;
  bool playerPosing_ = false;
  // The item the player threw, while it is in the hand or on its way, and whether the throw's clip
  // still plays.
  struct ThrownSprite {
    std::int32_t icon = 0;
    std::uint8_t palette = 0;
    std::optional<game::battle::ItemFlight> flight;
  };
  std::optional<ThrownSprite> thrown_;
  bool playerThrowing_ = false;
  bool partnerPosing_ = false;
  // The battle's map and the items its beaten enemies left: they land on the ground when the end
  // box closes (battle_ui.c: handleBattleEndBox calls spawnDroppedItems last).
  std::string map_;
  std::vector<game::battle::Drop> drops_;
  // The fighters as the field shows them, the partner first, from the battle's state while it runs
  // and as it left them until the end box closes; the NPC each enemy is.
  std::vector<FighterFigure> figures_;
  std::vector<std::int32_t> enemyNpcs_;
  // Where the fallen partner's countdown stands, taken when it shows (command_menu.c:
  // BTL_addDeathCountdown reads the partner's node once).
  std::optional<screens::WindowFraction> deathCountdownAnchor_;
  std::uint32_t partnerStance_ = 0;
  // FLEE_DISABLED, and the NPCs on screen when the battle started (overworld.c:1352 keeps
  // isOnScreen from the last field tick): the battle's end takes those off the map.
  bool fixedFight_ = false;
  std::vector<std::int32_t> onScreenAtStart_;
  // The NPCs running off during the opening, and how long they have run.
  std::vector<Leaver> fleers_;
  double fleeSeconds_ = 0.0;
  // The NPCs leaving after a victory (BTL_handleVictorySequence), each on its own figure, and how
  // long they have walked.
  struct Exit {
    std::vector<Leaver> leavers;
    double seconds = 0.0;
  };
  std::optional<Exit> exit_;
  // The player's command strip as it slides (BTL_tickCommandMenu), the time it has not run yet
  // after the fight, and whether it is going back up before the battle ends.
  screens::CommandMenuMotion menu_;
  screens::PartnerBarsMotion bars_;
  screens::CommandShout shout_;
  double menuSeconds_ = 0.0;
  bool closing_ = false;
};

} // namespace dwe::app
