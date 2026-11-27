#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/new_game_story.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/field/npcs.hpp>
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
#include <vector>

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

// The cell sections the story asks for at a frame of the original (tamer.c: checkMapInteraction,
// once a frame). The field ticks several times a frame and reads a confirm press on whichever tick
// it lands: a confirm cell asks only on that tick, so it is kept for the next frame, as the
// original reads the press at its next frame. A step cell asks every tick the player stands on
// it, so only the frame's own tick counts.
class FrameCells {
public:
  // The sections to ask for after a field tick: none between frames; at a frame, the tick's own
  // and a confirm cell pressed since the last frame.
  [[nodiscard]] std::vector<std::int32_t> take(std::span<const std::int32_t> sections,
                                               std::optional<std::int32_t> confirmed, bool frame);
  // Forgets a pressed confirm cell (a new map).
  void clear() { confirmed_.reset(); }

private:
  std::optional<std::int32_t> confirmed_;
};

// The ruleset section of the story (rules/<id>/story.yaml).
inline constexpr std::string_view StorySection = "story";

// The raised triggers of the species past the base range (RaisedSpeciesLimit): species
// RaisedSpeciesLimit + k is raised as trigger firstTrigger + k, for k below `species`. They live in
// the ruleset's extra triggers, so a mod with more species keeps them where scripts can test them.
struct ExtraRaisedRange {
  std::int32_t firstTrigger = game::story::BaseTriggerCount;
  std::int32_t species = 0;

  friend bool operator==(const ExtraRaisedRange&, const ExtraRaisedRange&) = default;
};

// A map entry's roll that places a roaming rare: the section's store_random into `variable` from
// 0 to `highest` places it on `hitsAtMost` or less.
struct RoamingRareRoll {
  game::story::SectionRef section;
  std::int64_t variable = 0;
  std::int64_t highest = 0;
  std::int64_t hitsAtMost = 0;

  friend bool operator==(const RoamingRareRoll&, const RoamingRareRoll&) = default;
};

struct StorySectionRules {
  // Engine ticks per frame of the original: the engine ticks at 60 Hz and the original's story ran
  // at 20 frames a second, so the original's is 3.
  std::uint32_t ticksPerFrame = 0;
  game::story::StoryRules rules;
  game::story::StoryFlowRules flow;
  // What the ruleset adds to the original's 800 triggers and 256 variables (`capacity`; none when
  // the section leaves it out).
  game::story::StoryCapacity capacity;
  ExtraRaisedRange extraRaised;
  std::vector<RoamingRareRoll> roamingRares;
  // Variables each co-op player keeps for itself (`player_own_variables`): a reward a script gives
  // once per player, as the meat farm's days since its last meat.
  std::vector<std::int32_t> playerOwnVariables;
  // Triggers each co-op player keeps for itself (`player_own_triggers`), as the shop job's
  // halfway line.
  std::vector<std::int32_t> playerOwnTriggers;
};

core::Result<StorySectionRules> readStoryRules(const ruleset::Section& section);

// The species another save section kept as raised (the evolution section), by number.
using RaisedSource = std::function<std::set<std::int32_t>()>;

struct StoryAdapterDesc {
  // The mounted packs, for data/new_game_story, field/warps and the digimon table; they must
  // outlive the adapter.
  const assets::VirtualFileSystem* files = nullptr;
};

// Glues the story (packages/game/story) to the session: starts its state as a new game leaves it,
// saves it in the `story` save section, and runs the story's sections. Each game has its own script
// VM, in which StoryLuaHost runs the packs' story modules; one tick of the session is one engine
// tick of the runner. The field reads the triggers and writes the ones a script warp sets
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
  // Puts a map record's NPC on the attached field as the map-head's spawn_npc does, without holding
  // the world: co-op shows at once an NPC another player's story change brings
  // (docs/architecture/multiplayer.md). False when nothing spawned.
  bool spawnNpc(std::int64_t species, std::int64_t slot, bool autotalk);
  // Asks for the sections of the NPCs the player touched or talked to in the last field tick, as
  // events, at the next frame; a running script drops them (tamer.c: checkMapInteraction).
  void runNpcContacts(std::span<const game::field::NpcContact> contacts);
  // The script id of the NPC whose section last ran from a contact (TALKED_TO_ENTITY): a battle
  // with no predefined enemies fights it.
  [[nodiscard]] std::optional<std::int32_t> talkedTo() const { return talkedTo_; }
  // The entity of that NPC (TALKED_TO_ENTITY is an entity: FirstNpcInstance + its slot), which
  // tells it apart from NPCs with the same script id; nothing when it is no longer on the field.
  [[nodiscard]] std::optional<std::int64_t> talkedEntity() const;
  // The entity of the field's NPC with a script id, the first by slot (script_value.c:
  // scriptIdToEntityId walks ENTITY_TABLE in order); nothing without the field or such an NPC.
  [[nodiscard]] std::optional<std::int64_t> npcEntity(std::int32_t scriptId) const;
  // The NPC orders, for the other orders that hold an NPC (a textbox's speaker: holdOwner); null
  // before the rules are read.
  [[nodiscard]] game::story::NpcOrders* npcOrders() { return npcOrders_.get(); }
  // The story's scripted moves, for the moves its other orders start (start_battle's pan back onto
  // the player); null before the rules are read.
  [[nodiscard]] game::story::ScriptMotion* motion() { return motion_ ? &*motion_ : nullptr; }

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
  // a script's map change): the map-head section runs at once (StoryRunner::enterMap). A script's
  // warp_to the field took (game::field::MapChange::arrivalSection) runs its arrival section
  // after it, as the script's own change would (StoryRunner::arriveWith).
  void mapEntered(std::int32_t map, std::optional<std::int32_t> arrivalSection = std::nullopt);
  // Asks for the sections of the map's script that the player's cell asked for in the last field
  // tick (game::field::Field::cellSections), at the next frame; a running script drops them. Only
  // the tick before a frame of the original asks, as the original checks the cell once a frame;
  // a confirm cell pressed between frames (game::field::Field::confirmSection) waits for it
  // (FrameCells).
  void runCellSections(std::span<const std::int32_t> sections,
                       std::optional<std::int32_t> confirmed = std::nullopt);
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
  // does (script_common.c: setDigimonRaised), and the ruleset's extra range (ExtraRaisedRange) past
  // those. A number past both (the hidden content's partners) is kept apart and saved by the
  // evolution section; a negative one is never raised.
  [[nodiscard]] bool raised(std::int32_t species) const;
  void markRaised(std::int32_t species);
  [[nodiscard]] std::set<std::int32_t> raisedSpecies() const;
  // Whether the player holds this medal (script_common.c: hasMedal).
  [[nodiscard]] bool hasMedal(std::int32_t medal) const;
  // How many medals the player has, as the player page counts them.
  [[nodiscard]] std::int32_t medalCount() const;
  // Whether the player page's item box `slot` (0 to PlayerItemCount - 1) shows its item.
  [[nodiscard]] bool hasPlayerItem(std::int32_t slot) const;
  // Which triggers and variables belong to one player rather than to the world co-op players
  // share (multiplayer.md, "Shared world interactions"): the raised species, the medals and the
  // player page's items are the player's own, and so are the script flow's own (the freeze, the
  // talk's facing, the next map's name), which follow that player's scripts, and the ruleset's
  // player_own_variables (a reward each player gets once). The rest is the world.
  [[nodiscard]] bool playerOwnTrigger(std::int32_t trigger) const;
  [[nodiscard]] bool playerOwnVariable(std::int32_t variable) const;
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
  // The control channel's reload_map: the story modules and patches read again from the packs on
  // the next section (StoryLuaHost::reloadScripts), the state kept. Fails while a section runs.
  core::Result<void> reloadScripts();
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
  // Only holds while the script that set it runs: a save neither keeps it nor brings it back.
  void setFixedFightTrigger(std::int64_t trigger) { fixedFightTrigger_ = trigger; }

  [[nodiscard]] game::story::StoryState& state() { return state_; }
  [[nodiscard]] const game::story::StoryState& state() const { return state_; }

  // The shop's tally (script_interp.c: SHOP_VARIABLE): routines 30, 31 and 56 keep it and a page's
  // {shopvar} shows it (script_textbox.c: showTextbox, code 0x10). A new game clears it; saves do
  // not keep it, as the original's memory card does not.
  [[nodiscard]] std::int64_t shopTally() const { return shopTally_; }
  void setShopTally(std::int64_t tally) { shopTally_ = tally; }

  // The chance in 100 that a roaming rare's roll places it (the option roaming_rare_chance);
  // nothing rolls as the original.
  void setRoamingRareChance(std::optional<std::int32_t> percent) { roamingRareChance_ = percent; }

private:
  class Serializer;
  class StatePort;
  class RandomPort;
  class RollPort;
  class SoundRelay;
  class MusicRelay;

  core::Result<void> wire(const StorySectionRules& story,
                          const game::story::DialogueRules& dialogue);
  core::Result<void> wireNpcs();
  core::Result<void> clearFixedFight(game::story::StoryState& state) const;
  void followNpcHolds();
  core::Result<void> wireFlow();
  core::Result<void> publishMotion();
  [[nodiscard]] game::story::StoryFlowPorts flowPorts();
  void playUnplayedChange();
  // Runs a map's head section between the music's enterMap and mapHeadDone.
  void runMapHead(std::int32_t map);
  void followRules(const StorySectionRules& story, const game::story::DialogueRules& dialogue);
  // Sizes the state to the rules' capacity and takes their extra raised range.
  void followCapacity(const StorySectionRules& story);
  // The trigger that marks a species raised, if it has one.
  [[nodiscard]] std::optional<std::int32_t> raisedTrigger(std::int32_t species) const;
  core::Result<void> startRunner(const StorySectionRules& story);

  StoryAdapterDesc desc_;
  std::string storyStage_;
  StoryLock lock_;
  std::optional<assets::NewGameStory> newGame_;
  std::string fieldStartMap_;
  std::int32_t fieldStartExit_ = 0;
  std::optional<assets::DigimonTable> digimon_;
  RaisedSource savedRaised_;
  // The raised species with no trigger of their own (numbers from RaisedSpeciesLimit past the
  // extra range: the hidden content's partners); the evolution section saves them with the rest.
  std::set<std::int32_t> extraRaised_;
  // The capacity and the extra raised range of the rules last read.
  game::story::StoryCapacity capacity_;
  ExtraRaisedRange extraRaisedRange_;
  game::story::StoryState state_;
  std::int64_t shopTally_ = 0;
  std::unique_ptr<Serializer> serializer_;
  // Declared in the order they are built: the runner holds the host, the host the VM's coroutines,
  // and the VM's published functions the ports and the orders.
  std::unique_ptr<script::ScriptVm> vm_;
  std::unique_ptr<StatePort> statePort_;
  std::unique_ptr<RandomPort> randomPort_;
  std::unique_ptr<RollPort> rollPort_;
  std::vector<RoamingRareRoll> roamingRares_;
  std::vector<std::int32_t> playerOwnVariables_;
  std::vector<std::int32_t> playerOwnTriggers_;
  std::optional<std::int32_t> roamingRareChance_;
  std::unique_ptr<SoundRelay> sound_;
  std::unique_ptr<MusicRelay> music_;
  std::unique_ptr<game::story::StoryOrders> orders_;
  std::unique_ptr<StoryNpcs> npcs_;
  std::unique_ptr<game::story::NpcOrders> npcOrders_;
  // Whether a script ran at the end of the last tick.
  bool scriptRan_ = false;
  std::optional<std::int32_t> talkedTo_;
  std::optional<scene::Entity> talkedNpc_;
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
  std::optional<std::int64_t> fixedFightTrigger_;
  // The cell sections asked on the last frame: a step cell asks every frame, logged once.
  std::vector<std::int32_t> askedCells_;
  FrameCells frameCells_;
};

} // namespace dwe::session
