#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/new_game_story.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/save/sections.hpp>
#include <dwe/game/story/script_motion.hpp>
#include <dwe/game/story/story_orders.hpp>
#include <dwe/game/story/story_runner.hpp>
#include <dwe/game/story/story_state.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/session/session_system.hpp>
#include <dwe/session/story_dialogue.hpp>
#include <dwe/session/story_flow.hpp>
#include <dwe/session/story_lua.hpp>
#include <dwe/session/story_npcs.hpp>
#include <dwe/session/story_stage_link.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::script {
class ScriptVm;
} // namespace dwe::script

namespace dwe::session {

// The save section of the story's triggers and variables.
inline constexpr std::string_view StorySaveName = "story";

// The first partner's raised trigger is 0x200 + its number, for numbers below 0x3f (main.c: main
// raises it before the new game's section; script_common.c: setDigimonRaised).
inline constexpr std::int32_t FirstRaisedTrigger = 0x200;
inline constexpr std::int32_t RaisedSpeciesLimit = 0x3f;

// The ruleset section of the story (rules/<id>/story.yaml).
inline constexpr std::string_view StorySection = "story";

struct StorySectionRules {
  // Engine ticks per frame of the original: the engine ticks at 60 Hz and the original's story ran
  // at 20 frames a second, so the original's is 3.
  std::uint32_t ticksPerFrame = 0;
  game::story::StoryRules rules;
  game::story::StoryFlowRules flow;
};

core::Result<StorySectionRules> readStoryRules(const ruleset::Section& section);

// The species another save section kept as raised (the evolution section), by number.
using RaisedSource = std::function<std::set<std::int32_t>()>;

struct StoryAdapterDesc {
  // The mounted packs, for data/new_game_story, field/warps and the digimon table; they must
  // outlive the adapter.
  const assets::VirtualFileSystem* files = nullptr;
};

// Glues the story (game/story) to the session: starts its state as a new game leaves it, saves it
// in the `story` save section, and runs the story's sections. Each game has its own script VM, in
// which StoryLuaHost runs the packs' story modules; one tick of the session is one engine tick of
// the runner. The field reads the triggers and writes the ones a script warp sets
// (field_facts.hpp).
class StoryAdapter final : public SessionSystem {
public:
  explicit StoryAdapter(StoryAdapterDesc desc);
  ~StoryAdapter() override;
  StoryAdapter(const StoryAdapter&) = delete;
  StoryAdapter& operator=(const StoryAdapter&) = delete;
  StoryAdapter(StoryAdapter&&) = delete;
  StoryAdapter& operator=(StoryAdapter&&) = delete;

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  // Reads data/new_game_story, field/warps' start and the ruleset's digimon table, any of which may
  // be missing, and the story section. The first time, it builds the game's script VM and publishes
  // the story API in it; a failure there, or story modules from another dwx, stops the game.
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;
  core::Result<void> addSaveSections(game::save::SaveSections& sections) override;
  // The story as a new game leaves it, with the session's partner as the first one.
  core::Result<void> startNewGame(GameSession& session) override;
  void tick(GameSession& session, const ruleset::ClockAdvance& advance) override;

  // Asks the runner for a section (StoryRunner::request). A section that starts at once draws its
  // random numbers from the session the last tick or new game bound.
  game::story::RequestOutcome request(game::story::SectionRef section,
                                      game::story::StartTiming timing);

  // The field's NPCs the story's NPC orders change (story_npcs.hpp); they must outlive the
  // attachment, and nullptr detaches.
  void attachNpcs(game::field::FieldNpcs* npcs);
  // Asks for the sections of the NPCs the player touched or talked to in the last field tick, as
  // events, at the next frame; a running script drops them (tamer.c: checkMapInteraction).
  void runNpcContacts(std::span<const game::field::NpcContact> contacts);
  // The NPC orders, for the other orders that hold an NPC (a textbox's speaker: holdOwner); null
  // before the rules are read.
  [[nodiscard]] game::story::NpcOrders* npcOrders() { return npcOrders_.get(); }

  // Where the story's map changes play (story_flow.hpp); an empty target drops them, and the story
  // goes on as if the player stayed.
  void setMapChangeTarget(StoryMapChangeTarget target);
  // Where the story's hold on the player goes while a script runs frozen (story_flow.hpp); an
  // empty target holds nothing.
  void setFieldHoldTarget(StoryHoldTarget target);
  // The characters and the camera the scripted moves drive, and their rules from the field rules
  // (story_stage_link.hpp); null detaches it. The stage must outlive its attachment.
  void attachStage(game::story::ScriptActorsPort* stage, const game::story::MotionRules& rules);
  // The player is on the map with this number of the original (walked there, loaded a save or ended
  // a script's map change): the map-head section runs at once (StoryRunner::enterMap).
  void mapEntered(std::int32_t map);
  // Asks for the sections of the map's script that the player's cell asked for in the last field
  // tick (game::field::Field::cellSections), at the next frame; a running script drops them.
  void runCellSections(std::span<const std::int32_t> sections);
  // Whether the story set up the script of the map with this number (StoryRunner::scriptsMap).
  [[nodiscard]] bool scriptsMap(std::int32_t map) const;

  // The story as a new game with this first partner leaves it: the triggers data/new_game_story
  // sets, the ones its opening sets when field/warps starts after it, and the partner's raised
  // trigger. A trigger the document does not know at the start (its paths disagree, or game code
  // may set it) starts clear, and the log says so.
  [[nodiscard]] game::story::StoryState newGameState(std::string_view firstPartner) const;

  // The raised species, by number: the story owns them as triggers 0x200 + number, as the original
  // does (script_common.c: setDigimonRaised). A number with no trigger is never raised, and marking
  // it is logged.
  [[nodiscard]] bool raised(std::int32_t species) const;
  void markRaised(std::int32_t species);
  [[nodiscard]] std::set<std::int32_t> raisedSpecies() const;
  // Where a save from before the story section takes its raised species from (createGameSystems:
  // the evolution section's); without it such a save starts with none raised.
  void setSavedRaised(RaisedSource source) { savedRaised_ = std::move(source); }

  // The story's textboxes: what the UI shows and where it sends the player's keys. Only once the
  // rules were read.
  [[nodiscard]] game::story::Dialogue& dialogue();
  // Whether a script runs or is about to start (the original's IS_SCRIPT_PAUSED is 0).
  [[nodiscard]] bool scriptRunning() const { return runner_.has_value() && runner_->busy(); }
  // The game's script VM once the rules were read, where other systems publish their story orders.
  [[nodiscard]] script::ScriptVm* vm() { return vm_.get(); }

  [[nodiscard]] game::story::StoryState& state() { return state_; }
  [[nodiscard]] const game::story::StoryState& state() const { return state_; }

private:
  class Serializer;
  class StatePort;
  class RandomPort;

  core::Result<void> wire(const StorySectionRules& story,
                          const game::story::DialogueRules& dialogue);
  core::Result<void> wireNpcs();
  void followNpcHolds();
  core::Result<void> wireFlow();
  core::Result<void> publishMotion();
  [[nodiscard]] game::story::StoryFlowPorts flowPorts();
  void playUnplayedChange();
  void followRules(const StorySectionRules& story, const game::story::DialogueRules& dialogue);
  core::Result<void> startRunner(const StorySectionRules& story);

  StoryAdapterDesc desc_;
  std::optional<assets::NewGameStory> newGame_;
  std::string fieldStartMap_;
  std::int32_t fieldStartExit_ = 0;
  std::optional<assets::DigimonTable> digimon_;
  RaisedSource savedRaised_;
  game::story::StoryState state_;
  std::unique_ptr<Serializer> serializer_;
  // Declared in the order they are built: the runner holds the host, the host the VM's coroutines,
  // and the VM's published functions the ports and the orders.
  std::unique_ptr<script::ScriptVm> vm_;
  std::unique_ptr<StatePort> statePort_;
  std::unique_ptr<RandomPort> randomPort_;
  std::unique_ptr<game::story::StoryOrders> orders_;
  std::unique_ptr<StoryNpcs> npcs_;
  std::unique_ptr<game::story::NpcOrders> npcOrders_;
  // Whether a script ran at the end of the last tick.
  bool scriptRan_ = false;
  std::unique_ptr<StoryMapChanges> mapChanges_;
  std::unique_ptr<StoryFieldControl> fieldControl_;
  std::unique_ptr<StoryActorsLink> actors_;
  // In place, so the runner's frame systems and the move orders keep pointing at it.
  std::optional<game::story::ScriptMotion> motion_;
  std::optional<std::int32_t> enteredMap_;
  std::unique_ptr<StoryLuaHost> host_;
  // Before the runner, which points to it.
  std::unique_ptr<StoryDialogue> dialogue_;
  std::optional<game::story::StoryRunner> runner_;
  std::uint32_t ticksPerFrame_ = 0;
  game::story::StoryFlowRules flow_;
};

} // namespace dwe::session
