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
#include <dwe/session/story_lock.hpp>
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
#include <utility>

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
// The medals are triggers 0x16c + number (script_common.c: hasMedal); the player page counts 18
// (game_menu.c: renderPlayerInfoView).
inline constexpr std::int32_t FirstMedalTrigger = 0x16c;
inline constexpr std::int32_t MedalCount = 18;
// The medals chart shows the first 15 (game_menu.c: renderMedalView).
inline constexpr std::int32_t ChartedMedalCount = 15;
// The player page's four item boxes show triggers 45 to 48: the old rod, the amazing rod and two
// more (game_menu.c: renderPlayerInfoView; fish.c: getBestFishingRod).
inline constexpr std::int32_t FirstPlayerItemTrigger = 45;
inline constexpr std::int32_t PlayerItemCount = 4;

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
  // The script id of the NPC whose section last ran from a contact (TALKED_TO_ENTITY): a battle
  // with no predefined enemies fights it.
  [[nodiscard]] std::optional<std::int32_t> talkedTo() const { return talkedTo_; }
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
  // The partner's sync wait_for_entity waits on (partner.c: getScriptSyncBit): the partner scenes'
  // owner lowers it when a scene starts and raises it when it is over. Until the first call, the
  // waits find it raised.
  void setPartnerSync(bool raised);
  // The player is on the map with this number of the original (walked there, loaded a save or ended
  // a script's map change): the map-head section runs at once (StoryRunner::enterMap).
  void mapEntered(std::int32_t map);
  // Asks for the sections of the map's script that the player's cell asked for in the last field
  // tick (game::field::Field::cellSections), at the next frame; a running script drops them.
  void runCellSections(std::span<const std::int32_t> sections);
  // Whether the story set up the script of the map with this number (StoryRunner::scriptsMap).
  [[nodiscard]] bool scriptsMap(std::int32_t map) const;

  // The stage of the story lock to play instead of the ruleset's active_stage (--story-stage; empty
  // keeps the ruleset's, StoryStageAll opens everything). Set before the rules are read.
  void setStoryStage(std::string stage) { storyStage_ = std::move(stage); }
  // What the stage keeps open, from the story_lock section when the rules were read.
  [[nodiscard]] const StoryLock& lock() const { return lock_; }

  // The story as a new game with this first partner leaves it: the triggers data/new_game_story
  // sets, the ones its opening sets when field/warps starts after it, and the partner's raised
  // trigger. A trigger the document does not know at the start (its paths disagree, or game code
  // may set it) starts clear, and the log says so.
  [[nodiscard]] game::story::StoryState newGameState(std::string_view firstPartner) const;

  // The raised species, by number: the story owns them as triggers 0x200 + number, as the original
  // does (script_common.c: setDigimonRaised). A number past the triggers' (the hidden content's
  // partners) is kept apart and saved by the evolution section; a negative one is never raised.
  [[nodiscard]] bool raised(std::int32_t species) const;
  void markRaised(std::int32_t species);
  [[nodiscard]] std::set<std::int32_t> raisedSpecies() const;
  // Whether the player holds this medal (script_common.c: hasMedal).
  [[nodiscard]] bool hasMedal(std::int32_t medal) const;
  // How many medals the player has, as the player page counts them.
  [[nodiscard]] std::int32_t medalCount() const;
  // Whether the player page's item box `slot` (0 to PlayerItemCount - 1) shows its item.
  [[nodiscard]] bool hasPlayerItem(std::int32_t slot) const;
  // The ruleset's digimon table, when the rules named one the packs have.
  [[nodiscard]] const assets::DigimonTable* digimonTable() const {
    return digimon_ ? &*digimon_ : nullptr;
  }
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
  // What the story's scenes (story_scenes.hpp) build on: the state's port, the runner, the host
  // and the packs. Null until readRules built them.
  [[nodiscard]] game::story::StoryStatePort* statePort();
  [[nodiscard]] game::story::StoryRunner* runner() { return runner_ ? &*runner_ : nullptr; }
  [[nodiscard]] StoryLuaHost* host() { return host_.get(); }
  [[nodiscard]] const assets::VirtualFileSystem& files() const { return *desc_.files; }
  // Whether a new game's state is the one data/new_game_story leaves (the default), or clear but
  // for the partner's raised trigger, for the original's opening to set it.
  void seedNewGame(bool seed) { seedNewGame_ = seed; }

  [[nodiscard]] game::story::StoryState& state() { return state_; }
  [[nodiscard]] const game::story::StoryState& state() const { return state_; }

  // The shop's tally (script_interp.c: SHOP_VARIABLE): routines 30, 31 and 56 keep it and a page's
  // {shopvar} shows it (script_textbox.c: showTextbox, code 0x10). A new game clears it; saves do
  // not keep it, as the original's memory card does not.
  [[nodiscard]] std::int64_t shopTally() const { return shopTally_; }
  void setShopTally(std::int64_t tally) { shopTally_ = tally; }

private:
  class Serializer;
  class StatePort;
  class RandomPort;
  class SoundRelay;
  class MusicRelay;

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
  std::string storyStage_;
  StoryLock lock_;
  std::optional<assets::NewGameStory> newGame_;
  std::string fieldStartMap_;
  std::int32_t fieldStartExit_ = 0;
  std::optional<assets::DigimonTable> digimon_;
  RaisedSource savedRaised_;
  // The raised species with no trigger of their own (numbers from RaisedSpeciesLimit: the hidden
  // content's partners); the evolution section saves them with the rest.
  std::set<std::int32_t> extraRaised_;
  game::story::StoryState state_;
  std::int64_t shopTally_ = 0;
  std::unique_ptr<Serializer> serializer_;
  // Declared in the order they are built: the runner holds the host, the host the VM's coroutines,
  // and the VM's published functions the ports and the orders.
  std::unique_ptr<script::ScriptVm> vm_;
  std::unique_ptr<StatePort> statePort_;
  std::unique_ptr<RandomPort> randomPort_;
  std::unique_ptr<SoundRelay> sound_;
  std::unique_ptr<MusicRelay> music_;
  std::unique_ptr<game::story::StoryOrders> orders_;
  std::unique_ptr<StoryNpcs> npcs_;
  std::unique_ptr<game::story::NpcOrders> npcOrders_;
  // Whether a script ran at the end of the last tick.
  bool scriptRan_ = false;
  std::optional<std::int32_t> talkedTo_;
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
  bool seedNewGame_ = true;
};

} // namespace dwe::session
