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
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/ruleset/ruleset_store.hpp>
#include <dwe/session/partner_view.hpp>
#include <dwe/session/session_rules.hpp>
#include <dwe/session/session_system.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dwe::session {

// The tamer as far as the session keeps it until a tamer system exists.
struct TamerState {
    std::int32_t level = 0;

    friend bool operator==(const TamerState&, const TamerState&) = default;
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
    // The ruleset to load from the packs, unless `rules` gives one already loaded (dwe-sim, with its --set values).
    std::string rulesetId{ruleset::OriginalRuleset};
    std::optional<ruleset::Ruleset> rules;
    // The partner species, when already read (dwe-sim shares one table between its runs, tests make their own);
    // otherwise read from the ruleset's digimon and raise tables.
    std::shared_ptr<const game::partner::SpeciesTable> species;
    // The seed of every random roll of the playthrough.
    std::uint64_t seed = 1;
    // Where the save slots are, or nullptr for a session that never saves (autosave requests are dropped).
    game::save::SaveStorage* storage = nullptr;
    // The game version written in saves.
    std::string gameVersion;
    // The real date for the saves' created and updated times; the epoch when not given, so tests are repeatable.
    std::function<std::chrono::sys_seconds()> wallClock;
    // The adapters of the systems besides the partner, in the order they tick.
    std::vector<std::unique_ptr<SessionSystem>> systems;
};

struct SessionState;
namespace sections {
struct SessionSerializers;
} // namespace sections

// One game session (docs: apps/session/README.md): the ruleset store, the game clock, the partner, the save and the
// systems that plug in, glued together, as the game and dwe-sim play it. Each tick it advances the clock (only while
// the field runs game time, at the clock's fast-forward speed), runs the partner's care and the systems, routes their
// events (autosaves, the tamer's level, rebirth, map changes) and writes a requested autosave. The field is the
// scene's: the session drives it through attachField.
class GameSession {
public:
    // Loads the ruleset with the checks of every system, the species and the save settings.
    static core::Result<std::unique_ptr<GameSession>> create(SessionDesc desc);

    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;
    GameSession(GameSession&&) = delete;
    GameSession& operator=(GameSession&&) = delete;
    ~GameSession();

    // Starts a new game from the ruleset's new_game section: the clock at its start, the partner, the tamer.
    core::Result<void> startNewGame();

    // Loads a slot, or a save already read, switching to its ruleset if it is another one. The place it was saved at
    // is in savedPlace() for whoever enters the map.
    core::Result<void> load(game::save::SlotId slot);
    core::Result<void> loadFile(const game::save::SaveFile& file);

    // The playthrough as a save file: the loaded file's sections with every system's written over them.
    [[nodiscard]] core::Result<game::save::SaveFile> saveFile() const;

    // Writes the playthrough to a slot.
    core::Result<void> save(game::save::SlotId slot);

    // Whether a game was started or loaded.
    [[nodiscard]] bool playing() const;

    // One tick of the game loop, of real seconds (the menu, which pauses the game, does not tick).
    void tick(double realSeconds);

    // The field of the scene: its game time drives the clock and its map changes route. It must stay attached only
    // while it lives.
    void attachField(const game::field::Field& field);
    void detachField();

    // Where the loaded save left the player, for entering its map; nothing for a new game.
    [[nodiscard]] const std::optional<FieldPlace>& savedPlace() const;

    // Where the player is now: on the attached field's map, else where the loaded save left them.
    [[nodiscard]] std::optional<FieldPlace> currentPlace() const;

    // What the care rules and the systems see of the rest of the game now (the tamer busy in a map change).
    [[nodiscard]] game::partner::CareContext careContext() const;

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

    // The random numbers of the systems besides the partner (an evolution's special roll, a chip's tamer level, the
    // items lost in a defeat), saved with the game. A new game starts them from the seed.
    [[nodiscard]] ruleset::Random& random();

    // A tamer level change with its chance (tamer.c: addTamerLevel), kept within the tamer section's limits.
    void addTamerLevel(std::int32_t chancePercent, std::int32_t amount);

    // The partner's events of the last tick and of the commands since, for the UI.
    [[nodiscard]] std::vector<game::partner::PartnerEvent> takePartnerEvents();

    // The player's commands to the partner, which route the events they cause.
    [[nodiscard]] bool refuses(const game::partner::ItemOffer& offer);
    void feed(const game::partner::FoodEffect& food);
    bool cure(const game::partner::CureEffect& cure);
    void scold();
    void praise();
    void useToilet();
    // Puts the partner to bed; the clock jumps to its wake hour. Fails if it is not sleepy.
    core::Result<void> sleep(const game::partner::SleepContext& context);
    core::Result<game::partner::TrainingResult> train(const game::partner::TrainingRequest& request);

    // After a system's command changed the partner (an item, an evolution): routes the partner's events and writes a
    // requested autosave, as the session's own commands do.
    void afterSystemCommand();

    // Asks for an autosave; the save module keeps one request and the ruleset decides which reasons count.
    void requestAutosave(game::save::AutosaveReason reason);
    // The autosave written since the last call, for the HUD and the autotests.
    [[nodiscard]] std::optional<Autosave> takeAutosaved();

    [[nodiscard]] double playSeconds() const { return playSeconds_; }
    [[nodiscard]] std::uint64_t seed() const { return seed_; }

private:
    GameSession(SessionDesc desc, ruleset::RulesetStore store);

    core::Result<void> setUp();
    core::Result<void> readRules();
    // The partner's stats at the start: the extracted ones, or the ruleset's when the packs have none.
    [[nodiscard]] game::partner::PartnerStats startingStats(const NewGameRules& start) const;
    void followRules();
    core::Result<void> registerSaveSections();
    void advanceGameTime(double realSeconds);
    void followMap();
    void routePartnerEvents();
    void routePartnerEvent(const game::partner::PartnerEvent& event);
    void changeTamerLevel(std::int32_t change);
    void rebirthPartner();
    void advanceAutosaveWindow(double realSeconds);
    void writeRequestedAutosave();
    // What every command of the player does after it: route the partner's events and write a requested autosave.
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
    // The stats the new-game script sets (data/new_game_stats), when the packs have them; new_game.stats otherwise.
    std::optional<assets::NewGameStatsTable> newGameStats_;
    std::optional<game::save::SaveSlots> slots_;
    std::optional<game::save::AutosaveRequests> autosave_;
    std::optional<Autosave> autosaved_;
    std::unique_ptr<sections::SessionSerializers> serializers_;
    game::save::SaveSections sections_;
    game::save::SaveFile file_;
    std::vector<game::partner::PartnerEvent> partnerEvents_;
    std::string lastMap_;
    double playSeconds_ = 0.0;
};

} // namespace dwe::session
