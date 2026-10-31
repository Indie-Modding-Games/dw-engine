#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/story/partner_routines.hpp>
#include <dwe/game/story/screen_history.hpp>
#include <dwe/game/story/story_battle.hpp>
#include <dwe/session/session_system.hpp>
#include <dwe/session/story_props.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace dwe::session {

class BattleAdapter;
class CardShop;
class EvolutionAdapter;
class InventoryAdapter;
class StoryAdapter;

// The ruleset section of the story's battles, scenes and routines (rules/<id>/story_scenes.yaml).
inline constexpr std::string_view StoryScenesSection = "story_scenes";

// How a new game's story starts.
enum class NewGameChain : std::uint8_t {
  // As the original: the story's state clear but for the partner's raised trigger, and the field
  // starts on the first map at its exit (map.c: initializeMap), whose map-head section and step
  // cell run the opening (main.c: main, runMapHeadScript(0xCC)).
  Original,
  // The state data/new_game_story leaves and field/warps' start, without the opening.
  Seeded,
};

// A sound of the story's scenes: the bank slot and the sound in it.
struct StorySceneSound {
  std::int64_t slot = 0;
  std::int64_t sound = 0;
};

// The egg box's sounds (doo2.c: DOO2_openEggBox, DOO2_tickEggInput): when it opens, when the egg
// moves and when the player takes one.
struct StoryEggBoxRules {
  StorySceneSound open;
  StorySceneSound move;
  StorySceneSound choose;
};

// The death the story plays (partner_impl.c: tickDeathCondition): a lost life or a death runs
// `section` of the map-head script with `clearedVariable` at 0, and the rebirth waits for its
// scene.
struct StoryDeathRules {
  std::uint16_t section = 0;
  std::int64_t clearedVariable = 0;
  StoryEggBoxRules eggBox;
};

// The numbers of the story_scenes section.
// Where the field puts the player at a new game's start: a map by its number in the original.
struct NewGameStart {
  std::int32_t map = 0;
  std::int32_t exit = 0;
};

struct StoryScenesRules {
  game::story::StoryBattleRules battle;
  game::story::PartnerRoutineRules partner;
  game::story::ScreenHistoryRules screens;
  // The variable most routines read their operand from (pstat 254), and the one the shop tally's
  // high and low bytes go to and come from (pstats 243 and 244).
  std::int64_t operandVariable = 0;
  std::int64_t tallyHighVariable = 0;
  std::int64_t tallyLowVariable = 0;
  // The operand that names no item, and how many of the item routines 12 and 13 take.
  std::int64_t noItem = 0;
  std::int32_t removeOneCount = 0;
  std::int32_t removeAllCount = 0;
  // The trigger that turns the shop into the recycle shop and the card shop into its sale; the one
  // that turns the card shop from a pack into its buy menu.
  std::int64_t sellingTrigger = 0;
  std::int64_t buyingCardsTrigger = 0;
  // The recruits: trigger first + species, for `count` species.
  std::int64_t firstRecruitTrigger = 0;
  std::int64_t recruitCount = 0;
  NewGameChain newGameChain = NewGameChain::Seeded;
  std::int32_t newGameMap = 0;
  std::int32_t newGameExit = 0;
  // Without it, the session gives the partner its new form at once and no script runs.
  std::optional<StoryDeathRules> death;
};

core::Result<StoryScenesRules> readStoryScenesRules(const ruleset::Section& section);

// The menus and the scenes the story's routines open on the app's screens (script_common.c and
// the SHOP, DGET and DOO2 overlays). Neutral names; the comments give the original's.
enum class StoryScreen : std::uint8_t {
  DiscardItem,        // routine 0, openDiscardItem
  MeritShop,          // routine 1, openMeritShop
  TradeShop,          // routine 2, the item trader's shop (openMojyamonShop)
  Tournament,         // routine 6, scriptStartTournament
  Shop,               // routine 8, openShop
  RecycleShop,        // routine 8 while the selling trigger is set, openRecycleShop
  Jukebox,            // routine 9, openJukebox
  ItemKeeper,         // routine 11, tickItemKeeper
  CardBuy,            // routine 14 while the card-buying trigger is set, openBuyCardMenu
  CardSell,           // routine 14 while the selling trigger is set, openSellCardMenu
  TournamentSchedule, // routine 18, initTournamentSchedule
  TournamentMedal,    // routine 22, scriptCheckTournamentMedal
  EggBox,             // routine 29, DOO2_openEggBox and DOO2_tickEggInput
  MoneyBox,           // routine 15, createShopBitsBox(1): shown, not waited for
  SmokeEffect,        // routine 38, createNinjamonEffect: shown, not waited for
  CloseTextBox,       // routine 16, triggerBoxCloseFlag(2): the second text box closes
  EndAnimationLoop,   // routine 55, setLoopCountToOne: the entity's animation plays once more
  ObjectAnimation,    // routine 21, resetMapObjectAnimation(pstat 247, pstat 248)
  NewGameMenu,        // routine 32, newGameStateMachine: the opening's questions and the names
  ForgetMoves,        // routine 47, lostAllLives: the reborn partner forgets moves, with its box
  SaveMenu,           // routine 48, tickSaveMachine
  GymLibrary,         // routine 39, loadTrainingLibrary: the next course is not complete yet
  GymCourse,          // routine 7, the gym course pstat 254 picks, waited for to its end
};

// A screen a routine asks the app for. `waits`: the script waits until the app closes it.
struct StoryScreenRequest {
  StoryScreen screen = StoryScreen::DiscardItem;
  std::int64_t routine = 0;
  // The routine's operands (the operand variable, or two for the object animation).
  std::int64_t first = 0;
  std::int64_t second = 0;
  bool waits = false;
};

// A scene of call_partner_routine the app plays: the evolutions the session runs itself.
struct PartnerSceneRequest {
  game::story::PartnerScene scene = game::story::PartnerScene::Toilet;
};

struct StoryScenesDesc {
  StoryAdapter* story = nullptr;
  EvolutionAdapter* evolution = nullptr;
  InventoryAdapter* inventory = nullptr;
  BattleAdapter* battle = nullptr;
  // The card shop whose pack routine 14 draws (rollCardPack); without it the pack is over at once.
  CardShop* cardShop = nullptr;
};

struct StoryScenesContext;

// The story's battles, partner scenes and routines (S6): it publishes start_battle,
// call_partner_routine and call_routine in the story's script VM and registers the routines, and
// it is the app's side of them: the battles, screens and scenes the story asks for wait in queues
// the app takes, and the app reports when each is over. It ticks before the story, so what the app
// reported is seen by the story the same tick.
class StoryScenes final : public SessionSystem {
public:
  explicit StoryScenes(StoryScenesDesc desc);
  ~StoryScenes() override;
  StoryScenes(const StoryScenes&) = delete;
  StoryScenes& operator=(const StoryScenes&) = delete;
  StoryScenes(StoryScenes&&) = delete;
  StoryScenes& operator=(StoryScenes&&) = delete;

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  // How a new game starts instead of the section's chain (--new-game-chain, an autotest route's
  // new_game); call it before the session reads its rules.
  void setNewGameChain(std::optional<NewGameChain> chain);
  // Reads the section and tells the story how a new game starts.
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;
  core::Result<void> startNewGame(GameSession& session) override;
  void tick(GameSession& session, const ruleset::ClockAdvance& advance) override;
  // A lost life or a death waits for the death section (rules.death).
  void routePartnerEvent(GameSession& session, const game::partner::PartnerEvent& event) override;

  // The battle the story asks for, once.
  [[nodiscard]] std::optional<game::story::ScriptedBattle> takeBattleRequest();
  // The battle and its end box are over.
  void battleFinished(game::story::BattleOutcome outcome);
  [[nodiscard]] std::optional<StoryScreenRequest> takeScreenRequest();
  // What the story puts on the field (story_props.hpp), in order, each once.
  [[nodiscard]] std::optional<StoryPropRequest> takePropRequest();
  // The screen the story waits for is closed.
  void screenClosed();
  [[nodiscard]] std::optional<PartnerSceneRequest> takePartnerScene();
  // Whether routine 53 ended the game since the last call: the app closes it, plays the ending
  // movie and goes back to the title (main.c: main, MAIN_STATE 3).
  [[nodiscard]] bool takeGameEnded();
  void partnerSceneFinished();
  // Whether the story plays the lost lives, the deaths and the rebirths as partner scenes; the app
  // then plays them when the story asks, not when the partner's events come.
  [[nodiscard]] bool playsDeath() const;
  // The egg the player took in the egg box, by its place in the rebirth's babies; the rebirth's
  // scene gives it to the partner.
  void chooseEgg(std::size_t egg);

  // What the original keeps of the screens and the battles (map.c, script_interp.c), and where a
  // new game starts.
  [[nodiscard]] game::story::ScreenHistory& screens();
  [[nodiscard]] const game::story::BattleCounters& battleCounters() const;
  [[nodiscard]] game::story::SceneSync& sceneSync();
  [[nodiscard]] const StoryScenesRules& rules() const;
  // Where the field starts a new game when the original chain runs, nothing when the field's own
  // start does (seeded, or no story_scenes section).
  [[nodiscard]] std::optional<NewGameStart> newGameStart() const;

private:
  void bind(GameSession& session);
  void watchRecruits(GameSession& session);
  void askForDeath(const GameSession& session);

  std::unique_ptr<StoryScenesContext> context_;
};

} // namespace dwe::session
