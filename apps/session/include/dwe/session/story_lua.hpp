#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/story/npc_orders.hpp>
#include <dwe/game/story/script_motion.hpp>
#include <dwe/game/story/section_host.hpp>
#include <dwe/game/story/story_orders.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/game/story/story_runner.hpp>
#include <dwe/script/script_coroutine.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string_view>

namespace dwe::assets {
class AssetId;
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

inline constexpr std::array Published{Ended,        Trigger,     Variable,      SetTrigger,
                                      UnsetTrigger, SetVariable, AddToVariable, ReduceVariable,
                                      CopyVariable, StoreRandom, Delay,         JumpToFile};
} // namespace story_api

// Runs the story's sections as coroutines of the script VM: script N is the module story/NNN of
// the pack that provides it, and section S is its function sections[S].
class StoryLuaHost final : public game::story::SectionHost {
public:
  // The VM and the file system must outlive the host.
  StoryLuaHost(script::ScriptVm& vm, const assets::VirtualFileSystem& files);
  StoryLuaHost(const StoryLuaHost&) = delete;
  StoryLuaHost& operator=(const StoryLuaHost&) = delete;
  StoryLuaHost(StoryLuaHost&&) = delete;
  StoryLuaHost& operator=(StoryLuaHost&&) = delete;
  ~StoryLuaHost() override = default;

  // Once per VM. The published functions point to the host, the state and the orders, which must
  // outlive every script call of the VM.
  core::Result<void> publishApi(game::story::StoryStatePort& state,
                                game::story::StoryOrders& orders);
  // The NPC orders (story_npcs.hpp: story_api::NpcPublished), once per VM; the orders must outlive
  // every script call of the VM.
  core::Result<void> publishNpcApi(game::story::NpcOrders& orders);
  // The script flow orders (story_flow.hpp: story_api::FlowPublished), once per VM: set_script and
  // section_on_exit go to the runner `runner` gives (none yet: the order fails), warp_to and
  // teleport end the section with a map change. The orders must outlive every script call.
  core::Result<void> publishFlowApi(game::story::StoryOrders& orders,
                                    std::function<game::story::StoryRunner*()> runner);

  // Once per VM, after publishApi: the textbox orders (story_dialogue.cpp), which point to the
  // dialogue; it must outlive every script call of the VM.
  core::Result<void> publishDialogue(game::story::Dialogue& dialogue);

  // Whether the packs' story modules come from this engine's dwx: the first one (script 0, which
  // the base pack always has) must carry its header, so outdated content stops the game when it
  // starts. Packs without story modules pass, and the log says no section can run.
  [[nodiscard]] core::Result<void> checkModules() const;

  // Once per VM: the scripted moves' orders (story_motion_api.hpp); the freezing ones freeze
  // through the runner. Both must outlive every script call of the VM.
  core::Result<void> publishMotionApi(game::story::StoryRunner& runner,
                                      game::story::ScriptMotion& motion);

  core::Result<game::story::SectionHandle> start(game::story::SectionRef section) override;
  game::story::SectionStep resume(game::story::SectionHandle handle,
                                  game::story::ResumeValue value) override;
  void close(game::story::SectionHandle handle) override;

private:
  core::Result<const script::ScriptModule*> module(std::uint16_t script);
  [[nodiscard]] core::Result<void> checkHeader(const assets::AssetId& id) const;

  script::ScriptVm* vm_;
  const assets::VirtualFileSystem* files_;
  std::map<std::uint16_t, script::ScriptModule> modules_;
  std::map<game::story::SectionHandle, script::ScriptCoroutine> sections_;
  game::story::SectionHandle nextHandle_ = 1;
  std::optional<game::story::Wait> pendingWait_;
};

} // namespace dwe::session
