#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/assets/new_game_stats.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/game/partner/effects.hpp>
#include <dwe/game/partner/events.hpp>
#include <dwe/game/partner/partner.hpp>
#include <dwe/game/partner/species.hpp>
#include <dwe/game/save/autosave.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/ruleset/ruleset_store.hpp>
#include <dwe/session/partner_view.hpp>
#include <dwe/session/session_rules.hpp>
#include <dwe/session/session_system.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace dwe::audio {
class Audio;
}

namespace dwe::session {

class SessionMusic;
class SessionSound;

// The tamer's record the story reads and changes (script_value.c: getStatsPointer MERIT,
// BATTLES_STARTED, BATTLES_FLED, TOURNAMENTS_WON, TOURNAMENT_WINS, TOURNAMENTS_LOST).
struct TamerRecord {
  std::int32_t merit = 0;
  std::int32_t startedBattles = 0;
  std::int32_t fledBattles = 0;
  std::int32_t tournamentsWon = 0;
  std::int32_t tournamentWins = 0;
  std::int32_t tournamentsLost = 0;

  friend bool operator==(const TamerRecord&, const TamerRecord&) = default;
};

// The most partners the player counts as raised; the count stays there (partner_impl.c:804).
inline constexpr std::int32_t MaxRaisedCount = 99;

// The longest a save waits for a care scene to end (GameSession::savesWait), in real seconds of
// the scene: far more than any care scene lasts, so only a scene that never ends reaches it.
inline constexpr double SaveWaitLimitSeconds = 60.0;

// Which way a tamer level change the field announces went (tamer.c: TAMER_LEVELS_AWARDED).
enum class TamerLevelChange : std::uint8_t { Up, Down };

// The tamer as far as the session keeps it until a tamer system exists.
struct TamerState {
  std::int32_t level = 0;
  // The partners raised, the first one included (raisedCount): a new game starts it
  // at 1 (main_menu.c: initializeDefaultSavegame) and each reborn partner adds one, up to
  // MaxRaisedCount (partner_impl.c: initializeReincarnatedPartner).
  std::int32_t raised = 1;
  TamerRecord record{};

  friend bool operator==(const TamerState&, const TamerState&) = default;
};

// What the new game's opening asked (main.c: newGameScene): the partner the answers picked and the
// names typed. Empty texts keep the ruleset's partner and leave the names unset.
struct NewGameChoice {
  std::string partner;
  std::string tamerName;
  std::string partnerName;

  friend bool operator==(const NewGameChoice&, const NewGameChoice&) = default;
};

// The names the player gave, as the save's summary keeps them (SaveSummary::tamer and nickname);
// empty until given.
struct PlayerNames {
  std::string tamer;
  std::string partner;
};

// Where the player is: the map and the place on it, as a save keeps it.
struct FieldPlace {
  std::string map;
  assets::SpawnPoint spawn;
};

// An autosave the session wrote, and why: its reasons in the order they were first asked.
struct Autosave {
  game::save::SlotId slot;
  std::vector<game::save::AutosaveReason> reasons;

  friend bool operator==(const Autosave&, const Autosave&) = default;
};

// What a session is made from.
struct SessionDesc {
  // The mounted packs; they must outlive the session.
  const assets::VirtualFileSystem* files = nullptr;
  // The ruleset to load from the packs, unless `rules` gives one already loaded (dwe-sim, with its
  // --set values).
  std::string rulesetId{ruleset::OriginalRuleset};
  std::optional<ruleset::Ruleset> rules;
  // The partner species, when already read (dwe-sim shares one table between its runs, tests make
  // their own); otherwise read from the ruleset's digimon and raise tables.
  std::shared_ptr<const game::partner::SpeciesTable> species;
  // The seed of every random roll of the playthrough.
  std::uint64_t seed = 1;
  // Where the save slots are, or nullptr for a session that never saves (autosave requests are
  // dropped).
  game::save::SaveStorage* storage = nullptr;
  // The game version written in saves.
  std::string gameVersion;
  // The real date for the saves' created and updated times; the epoch when not given, so tests are
  // repeatable.
  std::function<std::chrono::sys_seconds()> wallClock;
  // Where the story's sounds and music play, or nullptr for a silent session (dwe-sim, tests); it
  // must outlive the session.
  audio::Audio* sound = nullptr;
  // The adapters of the systems besides the partner, in the order they tick.
  std::vector<std::unique_ptr<SessionSystem>> systems;
};

struct SessionState;
namespace sections {
struct SessionSerializers;
} // namespace sections

// One game session (docs/modulos/apps/session.md): the ruleset store, the game clock, the partner,
// the save and the systems that plug in, glued together, as the game and dwe-sim play it. Each tick
// it advances the clock (only while the field runs game time, at the clock's fast-forward speed),
// runs the partner's care and the systems, routes their events (autosaves, the tamer's level,
// rebirth, map changes) and writes a requested autosave. The field is the scene's: the session
// drives it through attachField.
class GameSession {
public:
  // Loads the ruleset with the checks of every system, the species and the save settings.
  static core::Result<std::unique_ptr<GameSession>> create(SessionDesc desc);

  GameSession(const GameSession&) = delete;
  GameSession& operator=(const GameSession&) = delete;
  GameSession(GameSession&&) = delete;
  GameSession& operator=(GameSession&&) = delete;
  ~GameSession();

  // Starts a new game from the ruleset's new_game section: the clock at its start, the partner, the
  // tamer; the opening's choice, when it ran, gives the partner and the names.
  core::Result<void> startNewGame(const NewGameChoice& choice = {});

  // Loads a slot (or its backup, for a damaged save), or a save already read, switching to its
  // ruleset if it is another one. The place it was saved at is in savedPlace() for whoever enters
  // the map.
  core::Result<void> load(game::save::SlotId slot, bool fromBackup = false);
  core::Result<void> loadFile(const game::save::SaveFile& file);

  // The playthrough as a save file: the loaded file's sections with every system's written over
  // them.
  [[nodiscard]] core::Result<game::save::SaveFile> saveFile() const;

  // Writes the playthrough to a slot at once. A manual slot becomes the playthrough's slot. Only
  // the sleep's own save writes during its care scene; every other save asks savesWait() first.
  core::Result<void> save(game::save::SlotId slot);

  // Whether a save must wait (a dwe safety rule, docs/modulos/apps/session.md): the field holds
  // the player for a care scene (a pile, the toilet, a praise or scold, feeding, the sleep), whose
  // state no save keeps, so a save written now would load with the scene gone and the partner's
  // care running at once. The original never saves there: its save machine is a talk, which only
  // the free player starts (tamer.c: checkMapInteraction in tamerTickWalkingState), and it has no
  // autosave. A requested autosave, the save command and the save machine wait for the scene's
  // end; after SaveWaitLimitSeconds of one scene they save anyway, so no save waits forever.
  [[nodiscard]] bool savesWait() const;

  // The manual slot the playthrough saves to (docs/architecture/guardar-y-cargar.md): the one it
  // was loaded from or last saved to; loaded from an autosave, the one the autosave names; nothing
  // for a new game until it is saved. Each save's summary keeps it, so an autosave knows its
  // playthrough's slot.
  [[nodiscard]] const std::optional<game::save::SlotId>& homeSlot() const { return homeSlot_; }
  // When the playthrough started, which its every save keeps: it tells this playthrough's saves
  // from another's.
  [[nodiscard]] std::chrono::sys_seconds playthroughStarted() const { return file_.header.created; }
  // The save slots, to check one before writing it; nothing for a session that never saves.
  [[nodiscard]] const game::save::SaveSlots* slots() const { return slots_ ? &*slots_ : nullptr; }

  // Whether a game was started or loaded.
  [[nodiscard]] bool playing() const;

  // One tick of the game loop, of real seconds (the menu, which pauses the game, does not tick).
  void tick(double realSeconds);

  // The field of the scene: its game time drives the clock and its map changes route. It must stay
  // attached only while it lives.
  void attachField(const game::field::Field& field);
  void detachField();

  // Where the loaded save left the player, for entering its map; nothing for a new game.
  [[nodiscard]] const std::optional<FieldPlace>& savedPlace() const;

  // Where the player is now: on the attached field's map, else where the loaded save left them.
  [[nodiscard]] std::optional<FieldPlace> currentPlace() const;

  // Whether the game was finished (the original's MAIN_STATE 2): the ending's save marks it and a
  // save made then keeps it, so loading it plays the after-ending movie (main.c: main) before the
  // field.
  [[nodiscard]] bool gameCleared() const;
  // gameClearSave: the game is finished, and saves continue on `continueMap` (the File City screen
  // of getFileCityTopMap) at the player's position, or on the current map without one.
  void markGameCleared(std::optional<std::string> continueMap);
  // The save machine and the sleep's save play on as a story game (openSaveMachine and
  // partnerTickSleep set MAIN_STATE 1).
  void resumeStory();

  // What the care rules and the systems see of the rest of the game now (the tamer busy in a map
  // change, how the partner feels about the map).
  [[nodiscard]] game::partner::CareContext careContext() const;

  // How the partner feels about the attached field's map, worked out when the map is entered, as
  // the original does on loading a map (map.c: loadMapSetup); neutral without a field.
  [[nodiscard]] game::partner::AreaResponse areaResponse() const { return area_; }

  [[nodiscard]] ruleset::RulesetStore& rules() { return store_; }
  [[nodiscard]] ruleset::GameClock& clock();
  [[nodiscard]] const ruleset::GameClock& clock() const;
  [[nodiscard]] const SessionRules& sessionRules() const;
  [[nodiscard]] const game::partner::PartnerRules& partnerRules() const;

  // The partner; only while playing().
  [[nodiscard]] game::partner::Partner& partner();
  [[nodiscard]] const game::partner::Partner& partner() const;
  // What the UI shows of the partner; only while playing().
  [[nodiscard]] PartnerView partnerView() const;
  [[nodiscard]] const TamerState& tamer() const;
  // The tamer's and the partner's names; a script renames them (call_routine 32).
  [[nodiscard]] const PlayerNames& names() const { return names_; }
  void setNames(PlayerNames names) { names_ = std::move(names); }

  // The random numbers of the systems besides the partner (an evolution's special roll, a chip's
  // tamer level, the items lost in a defeat), saved with the game. A new game starts them from the
  // seed.
  [[nodiscard]] ruleset::Random& random();

  // A tamer level change with its chance (tamer.c: addTamerLevel), kept within the tamer section's
  // limits.
  void addTamerLevel(std::int32_t chancePercent, std::int32_t amount);
  // The change addTamerLevel made that the field has not announced yet (tamer.c:
  // TAMER_LEVEL_AWARD_PENDING): only one that left the level within the limits before they were
  // applied. Not saved, as in the original.
  [[nodiscard]] const std::optional<TamerLevelChange>& tamerLevelAward() const {
    return tamerLevelAward_;
  }
  void clearTamerLevelAward() { tamerLevelAward_.reset(); }
  // A story order sets the tamer level (script_interp.c: scriptInstruction28to3F), kept within the
  // tamer section's limits.
  void setTamerLevel(std::int32_t level);
  // A story order changes the tamer's record (script_interp.c: scriptInstruction28to3F).
  void setTamerRecord(const TamerRecord& record);

  // The partner's events of the last tick and of the commands since, for the UI.
  [[nodiscard]] std::vector<game::partner::PartnerEvent> takePartnerEvents();

  // The player's commands to the partner, which route the events they cause.
  [[nodiscard]] bool refuses(const game::partner::ItemOffer& offer);
  void feed(const game::partner::FoodEffect& food);
  bool cure(const game::partner::CureEffect& cure);
  void scold();
  void praise();
  void useToilet();
  // The partner eats a pile of this size from the ground (partner_impl.c: handleEatingPoop).
  void eatPoop(std::int32_t pileSize);
  // The field's pile scene (partner.c: tickWildPoop): the pile with its care effects at the
  // squat, the tamer level at the end.
  void leaveWildPile();
  void finishWildPoop();
  // Puts the partner to bed; the clock jumps to its wake hour. Fails if it is not sleepy. sleep is
  // fallAsleep and then wakeUp; the sleep screen calls them apart, because the original saves
  // between them and runs the wake-up evolutions only once the screen faded back in (partner.c:
  // partnerTickSleep).
  core::Result<void> sleep(const game::partner::SleepContext& context);
  core::Result<void> fallAsleep(const game::partner::SleepContext& context);
  void wakeUp();
  core::Result<game::partner::TrainingResult> train(const game::partner::TrainingRequest& request);

  // After a system's command changed the partner (an item, an evolution): routes the partner's
  // events and writes a requested autosave, as the session's own commands do.
  void afterSystemCommand();

  // Whether the player walks the field freely (tamer.c: tamerGetState() == 0): no map change, no
  // script or care scene holding them, and the field ran game time last tick. Always true without
  // a field.
  [[nodiscard]] bool playerFree() const;
  // Whether a story script ran at the end of the last tick (IS_SCRIPT_PAUSED == 0): the story
  // tells it each tick. The partner's death and sickness wait for the player free and no script.
  void setScriptRunning(bool running) { scriptRunning_ = running; }

  // Whether the story plays the death and asks for the rebirth itself (StoryScenes); otherwise a
  // dead partner is reborn at once.
  void setRebirthByStory(bool byStory) { rebirthByStory_ = byStory; }
  // Gives the dead partner its new form; `egg` is the egg box's (Partner::rebirth). The events are
  // routed with the session's next ones.
  void rebirthPartner(std::optional<std::size_t> egg = std::nullopt);

  // Asks for an autosave; the save module keeps one request and the ruleset decides which reasons
  // count.
  void requestAutosave(game::save::AutosaveReason reason);
  // The autosave written since the last call, for the HUD and the autotests.
  [[nodiscard]] std::optional<Autosave> takeAutosaved();

  [[nodiscard]] double playSeconds() const { return playSeconds_; }
  [[nodiscard]] std::uint64_t seed() const { return seed_; }
  // The story's sound slots, or nullptr for a silent session.
  [[nodiscard]] game::story::SoundPort* sound();
  // The music: the story's, the clock's and the battles', or nullptr for a silent session.
  [[nodiscard]] SessionMusic* music();
  // A scene's sound bank in slot 8 instead of the map's, until nothing gives it back
  // (SessionSound::useSceneBank); nothing happens in a silent session.
  void useSceneSounds(std::optional<std::int32_t> soundId);
  // The battle bank in slot 8 from a battle's start; at its end a won one gives slot 8 back to the
  // map's bank and a fled or lost one keeps the battle's (SessionSound::startBattleBank,
  // endBattleBank); nothing happens in a silent session.
  void startBattleSounds();
  void endBattleSounds(bool won);
  // The vball entry of the original's species number `type` in one of slots 5 to 7
  // (SessionSound::useSpeciesBank); nothing happens in a silent session.
  void useSpeciesSounds(std::int64_t slot, std::int32_t type);
  // The enemies' species banks in slots 5 to 7, in the battle's order
  // (SessionSound::useEnemyBanks); nothing happens in a silent session.
  void useEnemySounds(std::span<const std::string> species);

private:
  GameSession(SessionDesc desc, ruleset::RulesetStore store);

  core::Result<void> setUp();
  core::Result<void> readRules();
  // The partner's stats at the start: the extracted ones, or the ruleset's when the packs have
  // none.
  [[nodiscard]] game::partner::PartnerStats startingStats(const NewGameRules& start) const;
  void followRules();
  core::Result<void> registerSaveSections();
  void advanceGameTime(double realSeconds);
  void followMap();
  // How the partner feels about the attached field's map now.
  [[nodiscard]] game::partner::AreaResponse areaOfFieldMap() const;
  void routePartnerEvents();
  void routePartnerEvent(const game::partner::PartnerEvent& event);
  void changeTamerLevel(std::int32_t change);
  void advanceAutosaveWindow(double realSeconds);
  // Counts how long the field has held the player for the current care scene.
  void followCareHold(double realSeconds);
  void writeRequestedAutosave();
  // What every command of the player does after it: route the partner's events and write a
  // requested autosave.
  void afterCommand();
  // The praise or scold about to happen, with the partner's values before it.
  [[nodiscard]] PraiseOrScold praiseOrScold(bool scolded) const;
  void afterPraiseOrScold(const PraiseOrScold& event);
  [[nodiscard]] game::save::SaveHeader headerNow() const;
  [[nodiscard]] std::chrono::sys_seconds now() const;

  SessionDesc desc_;
  ruleset::RulesetStore store_;
  std::uint64_t rulesRevision_ = 0;
  std::uint64_t seed_;
  std::unique_ptr<SessionState> state_;
  std::shared_ptr<const game::partner::SpeciesTable> species_;
  // The stats the new-game script sets (data/new_game_stats), when the packs have them;
  // new_game.stats otherwise.
  std::optional<assets::NewGameStatsTable> newGameStats_;
  std::optional<game::save::SaveSlots> slots_;
  std::optional<game::save::AutosaveRequests> autosave_;
  std::optional<Autosave> autosaved_;
  std::unique_ptr<sections::SessionSerializers> serializers_;
  game::save::SaveSections sections_;
  game::save::SaveFile file_;
  std::optional<game::save::SlotId> homeSlot_;
  std::optional<TamerLevelChange> tamerLevelAward_;
  std::vector<game::partner::PartnerEvent> partnerEvents_;
  bool rebirthByStory_ = false;
  bool scriptRunning_ = false;
  std::string lastMap_;
  std::unique_ptr<SessionSound> sound_;
  std::unique_ptr<SessionMusic> music_;
  game::partner::AreaResponse area_ = game::partner::AreaResponse::Neutral;
  double playSeconds_ = 0.0;
  // Real seconds of the care scene the field holds the player in now; 0 when it holds none.
  double careHeldSeconds_ = 0.0;
  // Whether the pending autosave already logged that it waits for the care scene.
  bool autosaveWaitLogged_ = false;
  PlayerNames names_;
};

} // namespace dwe::session
