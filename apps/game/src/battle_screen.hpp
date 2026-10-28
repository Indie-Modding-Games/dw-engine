#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/screens/battle_intro.hpp>
#include <dwe/screens/battle_view.hpp>
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
#include <string>
#include <string_view>
#include <vector>

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
// throw the item of a slot (the command's number) to the partner.
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

  // Whether the battle, its end box or the beaten enemies' exit is on screen: the field and the
  // game clock wait meanwhile.
  [[nodiscard]] bool active() const { return battle_.has_value() || ending_ || exit_.has_value(); }
  // The outcome of the last battle, once, after its end box is closed: what a story battle reads.
  [[nodiscard]] std::optional<game::battle::Outcome> takeOutcome();
  // The battle_win debug command: the enemies fall at once; false when no fight is running.
  bool defeatEnemies();

  // One logic tick of the battle and the view of it. The battle runs once its opening is over.
  void tick(double seconds);

  // Where an enemy's anger mark hangs on the screen, `lift` world units over its feet; the field
  // scene knows (play_session.cpp).
  using EnemyAnchor = std::function<screens::AngerAnchor(const game::battle::Combatant&, float)>;
  // Places the anger marks of the opening over the enemies, while they show.
  void placeIntro(const EnemyAnchor& anchor);

  // Takes the battle document's commands; false for any other command.
  bool handle(const ui::UiCommand& command);

private:
  [[nodiscard]] screens::NameKeys names() const;
  void publish() const;
  void tickIntro(double seconds);
  void tickVictory(double seconds);
  void startPoses();
  void tickPoses(double seconds);
  void releasePoses();
  void openEndBox() const;
  void playSound(std::int64_t sound) const;
  void publishIntro() const;
  void tickBattle(double seconds);
  void takeFigures();
  void showFighters(double seconds);
  void standPartner(std::uint32_t clip, std::string_view origin);
  void openBag();
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
  [[nodiscard]] session::BattleMusicFacts musicFactsOf(const session::BattleRequest& request) const;

  BattleScreenDesc desc_;
  std::optional<game::battle::Battle> battle_;
  std::optional<session::BattleEffects> effects_;
  bool ending_ = false;
  // The opening before the fight (battle_setup.c: BTL_handleBattleIntro), the enemies it marks and
  // where, and the time it has not run yet. Each battle seeds its own from their count.
  std::optional<screens::BattleIntro> intro_;
  std::vector<game::battle::Combatant> introEnemies_;
  std::vector<screens::AngerAnchor> angerAnchors_;
  double introSeconds_ = 0.0;
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
};

} // namespace dwe::app
