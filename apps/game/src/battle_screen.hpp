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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "battle_effect_bodies.hpp"
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
// An NPC leaving the battle's ground after a victory: the field shows it on `figure`; it walks to
// its flee point once it has got up.
struct Leaver {
  std::int32_t npc = 0;
  FighterFigure figure;
  bool walking = false;
};

class BattleScreen {
public:
  explicit BattleScreen(BattleScreenDesc desc) : desc_(desc) {}

  // `enemyNpcs` are the script ids of the map's NPCs the enemies are, in their order: each enemy is
  // shown on its NPC (none: the enemies are not shown). A fixed fight cannot be fled
  // (FLEE_DISABLED): its enemies stay on the map after it.
  core::Result<void> start(session::BattleRequest request, std::vector<std::int32_t> enemyNpcs,
                           bool fixedFight = false);

  // Whether the battle, its end box, the beaten enemies' exit or the command strip going back up
  // is on screen: the field and the game clock wait meanwhile.
  [[nodiscard]] bool active() const {
    return battle_.has_value() || ending_ || exit_.has_value() || closing_;
  }
  // The outcome of the last battle, once, after its end box is closed: what a story battle reads.
  [[nodiscard]] std::optional<game::battle::Outcome> takeOutcome();
  // The battle_win debug command: the enemies fall at once; false when no fight is running.
  bool defeatEnemies();
  // The battle on screen until its end box closes, for a bug report and --trace; nothing otherwise.
  [[nodiscard]] const game::battle::Battle* battle() const { return battle_ ? &*battle_ : nullptr; }
  // The map NPCs the enemies are, in the battle's order.
  [[nodiscard]] std::span<const std::int32_t> enemyNpcs() const { return enemyNpcs_; }

  // One logic tick of the battle and the view of it. The battle runs once its opening is over.
  void tick(double seconds);

  // Where an enemy's anger mark hangs on the screen, `lift` world units over its feet; the field
  // scene knows (play_session.cpp).
  using EnemyAnchor = std::function<screens::AngerAnchor(const game::battle::Combatant&, float)>;
  // Places the anger marks of the opening over the enemies, while they show.
  void placeIntro(const EnemyAnchor& anchor);

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
  void placePlayer() const;
  void facePlayerToPartner(game::battle::Point partner) const;
  void tickVictory(double seconds);
  void startPoses();
  void tickPoses(double seconds);
  void releasePoses();
  void openEndBox() const;
  void playSound(std::int64_t sound) const;
  void publishIntro() const;
  void tickBattle(double seconds);
  void takeFigures();
  void showEffects() const;
  // The numbers the frame's hits, blocks and poison pop out of the fighters, and one frame of them.
  void stepNumbers(const game::battle::BattleEvents& frame);
  void tickNumbers(double seconds);
  void publishNumbers() const;
  // The enemies' HP bars beside them (battle_main.c: BTL_renderEnemyHPBars); none without a battle.
  void publishHpBars() const;
  void showFighters(double seconds);
  void standPartner(std::uint32_t clip, std::string_view origin);
  void openBag();
  // Keeps the bag's document and the battle's bag together: the bag closes when its document goes,
  // and the document goes when the battle closed the bag or ended.
  void syncBag();
  void throwItem(double slot);
  void finish();
  void close();
  // battle_screen_exit.cpp: what the battle leaves on the map.
  [[nodiscard]] bool offScreen(std::int32_t npc, float x, float z) const;
  [[nodiscard]] std::vector<std::int32_t> npcsOnScreen() const;
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
  // What the effects read of the fighters; it outlives effects_.
  std::optional<BattleEffectBodies> bodies_;
  std::optional<session::BattleEffects> effects_;
  // When the techniques' effects reach their targets (game::battle::ScriptedFlights), from the
  // effect scripts and the bodies.
  std::optional<game::battle::ScriptedFlights> flights_;
  bool ending_ = false;
  // The field's bag document is open over the battle.
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
  double introSeconds_ = 0.0;
  // The player running back while the banner shows (BTL_handleBattleIntro).
  std::optional<game::battle::PlayerRetreat> retreat_;
  // Where the partner stood as the fight's last frame began: the player looks there
  // (tamerTickBattle runs before BTL_battleMain moves the fighters).
  std::optional<game::battle::Point> partnerBeforeFrame_;
  std::uint32_t battlesStarted_ = 0;
  std::optional<game::battle::Outcome> outcome_;
  // Whether the battle on the end box was won: the field's music comes back when it closes.
  bool won_ = false;
  // The time left before the victory poses' sound, once a battle is won.
  std::optional<double> victoryPoseSeconds_;
  // The time the victory poses have played, while they do, and which of the two still plays.
  std::optional<double> posesSeconds_;
  bool playerPosing_ = false;
  bool partnerPosing_ = false;
  // The battle's map and the items its beaten enemies left: they land on the ground when the end
  // box closes (battle_ui.c: handleBattleEndBox calls spawnDroppedItems last).
  std::string map_;
  std::vector<game::battle::Drop> drops_;
  // The fighters as the field shows them, the partner first, from the battle's state while it runs
  // and as it left them until the end box closes; the NPC each enemy is.
  std::vector<FighterFigure> figures_;
  std::vector<std::int32_t> enemyNpcs_;
  std::uint32_t partnerStance_ = 0;
  // FLEE_DISABLED, and the NPCs on screen when the battle started (overworld.c:1352 keeps
  // isOnScreen from the last field tick): the battle's end takes those off the map.
  bool fixedFight_ = false;
  std::vector<std::int32_t> onScreenAtStart_;
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
