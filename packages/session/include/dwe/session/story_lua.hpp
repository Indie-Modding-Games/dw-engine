#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/story/npc_orders.hpp>
#include <dwe/game/story/partner_routines.hpp>
#include <dwe/game/story/script_motion.hpp>
#include <dwe/game/story/section_host.hpp>
#include <dwe/game/story/story_battle.hpp>
#include <dwe/game/story/story_orders.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/game/story/story_routines.hpp>
#include <dwe/game/story/story_runner.hpp>
#include <dwe/game/story/story_task.hpp>
#include <dwe/script/api_registry.hpp>
#include <dwe/script/script_coroutine.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::assets {
class AssetId;
struct MountedPack;
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::game::story {
class StoryRunner;
} // namespace dwe::game::story

namespace dwe::script {
class ScriptVm;
} // namespace dwe::script

namespace dwe::game::story {
class Dialogue;
} // namespace dwe::game::story

namespace dwe::session {

inline constexpr std::string_view StoryNamespace = "story";

// The folder of the story patches (docs/architecture/interprete-historia.md#moddabilidad): each
// story/patches/<name>.lua returns `{ script = N, sections = { [S] = function(original) ... end }
// }` and its sections replace those of story/NNN without copying the module.
inline constexpr std::string_view StoryPatchFolder = "story/patches/";

// Spelled here because the runtime never depends on formats; a session test checks them against
// the emitter's names (story_api_names.hpp).
namespace story_api {
inline constexpr std::string_view Ended = "ENDED";
inline constexpr std::string_view Trigger = "trigger";
inline constexpr std::string_view Variable = "var";
inline constexpr std::string_view SetTrigger = "set_trigger";
inline constexpr std::string_view UnsetTrigger = "unset_trigger";
inline constexpr std::string_view SetVariable = "set_var";
inline constexpr std::string_view AddToVariable = "add_to_var";
inline constexpr std::string_view ReduceVariable = "reduce_var";
inline constexpr std::string_view CopyVariable = "copy_var";
inline constexpr std::string_view StoreRandom = "store_random";
inline constexpr std::string_view Delay = "delay";
inline constexpr std::string_view JumpToFile = "jump_to_file";
inline constexpr std::string_view PlaySound = "play_sound";

inline constexpr std::array Published{
    Ended,          Trigger,      Variable,    SetTrigger, UnsetTrigger, SetVariable, AddToVariable,
    ReduceVariable, CopyVariable, StoreRandom, Delay,      JumpToFile,   PlaySound};

// The scenes' orders (story_scenes.hpp), published apart once their system binds.
inline constexpr std::string_view CallRoutine = "call_routine";
inline constexpr std::string_view StartBattle = "start_battle";
inline constexpr std::string_view CallPartnerRoutine = "call_partner_routine";

// The props' orders (story_props.hpp), published apart (StoryLuaHost::publishPropApi).
inline constexpr std::string_view SpawnBoulder = "spawn_boulder";
inline constexpr std::string_view MoveBoulder = "move_boulder";
inline constexpr std::string_view DespawnBoulder = "despawn_boulder";
inline constexpr std::string_view SpawnSpriteAtEntity = "spawn_sprite_at_entity";

inline constexpr std::array ScenePublished{CallRoutine, StartBattle, CallPartnerRoutine};
inline constexpr std::array PropPublished{SpawnBoulder, MoveBoulder, DespawnBoulder,
                                          SpawnSpriteAtEntity};
} // namespace story_api

// What the props' orders change (story_props.hpp): the story's state of the boulder and the map
// sprites it moves to a character.
class StoryPropOrders {
public:
  StoryPropOrders() = default;
  StoryPropOrders(const StoryPropOrders&) = delete;
  StoryPropOrders& operator=(const StoryPropOrders&) = delete;
  StoryPropOrders(StoryPropOrders&&) = delete;
  StoryPropOrders& operator=(StoryPropOrders&&) = delete;
  virtual ~StoryPropOrders() = default;

  // spawn_boulder (map_object.c: spawnBoulder): the boulder at its start, unturned.
  virtual void spawnBoulder() = 0;
  // move_boulder (map_object.c: moveBoulder): the boulder goes `down` lower (the original's y)
  // and `forward` along z, and turns a step about its x axis; the section waits a frame.
  virtual void moveBoulder(std::int64_t down, std::int64_t forward) = 0;
  // despawn_boulder: the boulder goes away.
  virtual void despawnBoulder() = 0;
  // spawn_sprite_at_entity (overworld.c: spawnSpriteAtEntity).
  virtual void spriteAtCharacter(std::int64_t character, std::int64_t node,
                                 std::int64_t object) = 0;
};

// What the scenes' orders run: the routine table, the scripted battles and the partner's scenes,
// and how the world freezes for a scene. They must outlive every script call of the VM.
struct StorySceneOrders {
  game::story::RoutineTable* routines = nullptr;
  game::story::StoryBattles* battles = nullptr;
  game::story::PartnerRoutines* partner = nullptr;
  std::function<void()> freeze;
};

// Runs the story's sections as coroutines of the script VM: script N is the module story/NNN of
// the pack that provides it, and section S is its function sections[S], unless a story patch
// (StoryPatchFolder) has that section: then the patch's function runs, in its pack's environment,
// with the module's function as its argument `original` (nil when the module has none). Of two
// patches of one section the one higher in the pack stack wins (the later id within a pack), and
// the log says which lost.
class StoryLuaHost final : public game::story::SectionHost {
public:
  // The VM and the file system must outlive the host.
  StoryLuaHost(script::ScriptVm& vm, const assets::VirtualFileSystem& files);
  StoryLuaHost(const StoryLuaHost&) = delete;
  StoryLuaHost& operator=(const StoryLuaHost&) = delete;
  StoryLuaHost(StoryLuaHost&&) = delete;
  StoryLuaHost& operator=(StoryLuaHost&&) = delete;
  ~StoryLuaHost() override = default;

  // Once per VM. The published functions point to the host, the state, the orders and the sound,
  // which must outlive every script call of the VM.
  core::Result<void> publishApi(game::story::StoryStatePort& state,
                                game::story::StoryOrders& orders, game::story::SoundPort& sound);
  // The NPC orders (story_npcs.hpp: story_api::NpcPublished), once per VM; the orders must outlive
  // every script call of the VM.
  core::Result<void> publishNpcApi(game::story::NpcOrders& orders);
  // The script flow orders (story_flow.hpp: story_api::FlowPublished), once per VM: set_script and
  // section_on_exit go to the runner `runner` gives (none yet: the order fails), warp_to and
  // teleport end the section with a map change. The orders must outlive every script call.
  core::Result<void> publishFlowApi(game::story::StoryOrders& orders,
                                    const std::function<game::story::StoryRunner*()>& runner);

  // Once per VM, after publishApi: the textbox orders (story_dialogue.cpp), which point to the
  // dialogue; it must outlive every script call of the VM.
  core::Result<void> publishDialogue(game::story::Dialogue& dialogue);

  // Once per VM, after publishApi: call_routine, start_battle and call_partner_routine. A section
  // that waits on one of them waits on its task (story_task.hpp), which the host ticks once a
  // frame.
  core::Result<void> publishSceneApi(StorySceneOrders orders);
  // Once per VM, after publishApi: the props' orders (story_api::SpawnBoulder and the next three),
  // which point to `props`; it must outlive every script call of the VM.
  core::Result<void> publishPropApi(StoryPropOrders& props);

  // Whether the packs' story modules come from this engine's dwx: the first one (script 0, which
  // the base pack always has) must carry its header, so outdated content stops the game when it
  // starts. Packs without story modules pass, and the log says no section can run.
  [[nodiscard]] core::Result<void> checkModules() const;

  // Once per VM: the scripted moves' orders (story_motion_api.hpp); the freezing ones freeze
  // through the runner. Both must outlive every script call of the VM.
  core::Result<void> publishMotionApi(game::story::StoryRunner& runner,
                                      game::story::ScriptMotion& motion);

  // The patch that runs a section, as `pack:asset id`, or nothing when the module's own runs.
  [[nodiscard]] std::optional<std::string> patchOf(game::story::SectionRef section);

  core::Result<game::story::SectionHandle> start(game::story::SectionRef section) override;
  game::story::SectionStep resume(game::story::SectionHandle handle,
                                  game::story::ResumeValue value) override;
  void close(game::story::SectionHandle handle) override;

private:
  // A loaded story patch.
  struct Patch {
    script::ScriptModule module;
    std::uint16_t script = 0;
  };
  using SectionKey = std::pair<std::uint16_t, std::uint16_t>;

  // A section that waits on a task, and the subroutine calls it waits in.
  struct WaitingTask {
    std::shared_ptr<game::story::StoryTask> task;
    std::uint32_t linkDepth = 0;
  };

  core::Result<const script::ScriptModule*> module(std::uint16_t script);
  // Ticks the section's task: its step, or nothing once it is done and the section resumes.
  std::optional<game::story::SectionStep> driveTask(game::story::SectionHandle handle,
                                                    game::story::ResumeValue& value);
  [[nodiscard]] core::Result<void> checkHeader(const assets::AssetId& id) const;
  // Loads the packs' story patches once; a patch that does not load is left out and logged.
  void loadPatches();
  void addPatch(const assets::AssetId& id, const assets::MountedPack& pack);

  script::ScriptVm* vm_;
  const assets::VirtualFileSystem* files_;
  std::map<std::uint16_t, script::ScriptModule> modules_;
  std::map<game::story::SectionHandle, script::ScriptCoroutine> sections_;
  game::story::SectionHandle nextHandle_ = 1;
  std::optional<game::story::Wait> pendingWait_;
  std::shared_ptr<game::story::StoryTask> pendingTask_;
  std::map<game::story::SectionHandle, WaitingTask> tasks_;
  bool patchesLoaded_ = false;
  std::vector<Patch> patches_;
  // The winning patch of each patched section, an index in patches_.
  std::map<SectionKey, std::size_t> patchedSections_;
};

} // namespace dwe::session
