#pragma once

#include <dwe/core/error.hpp>
#include <dwe/platform/events.hpp>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "control_json.hpp"
#include "story_set_command.hpp"

namespace dwe::app {

// The game's debug control channel (`dwe-game --control <socket>`, docs/guides/control-remoto.md):
// a tool outside the game (an MCP server, a test) sends a JSON request per line and gets a JSON
// reply per line. It reads the state as the state trace writes it, presses keys tick by tick,
// pauses the logic and advances it a number of ticks, places characters, starts battles, loads a
// save or a map and takes screenshots. For a map editor it also lists the packs' files again,
// reloads the current map from them, dumps the story's state and shows the map's debug overlay,
// polls where everyone stands (live), previews a map patch without saving it, writes the story's
// triggers and variables and reads the log's warnings. Off unless asked for; the game then does
// nothing for it.
//
// A request is {"id": <any>, "cmd": "<command>", ...}; the reply echoes the id with "ok": true and
// what was asked, or "ok": false and an "error". Replies to press, advance and screenshot come
// once the ticks ran or the picture was written. Several clients may be connected at once
// (ControlSocket): each gets the replies to its own requests only, while the held keys, the
// presses and the pause are the game's, shared by all of them.

struct ControlRequest {
  enum class Kind : std::uint8_t {
    Status,       // status: tick, paused, mode, map, held keys
    State,        // state {path?}: the state trace's line of now, or the part the path names
    Press,        // press {keys, ticks = 6}: the keys down for exactly that many ticks
    Hold,         // hold {keys}: the keys down until released
    Release,      // release {keys?}: those keys, or all of them, up
    Pause,        // pause: no logic tick runs until resume or advance
    Resume,       // resume: the logic runs at its pace again
    Advance,      // advance {ticks}: that many ticks run; paused, exactly them and no more
    Screenshot,   // screenshot {path}: the next frame drawn, as a PNG
    Script,       // script {code}: a line of the game's scripts, as the console runs it
    Place,        // place {text}: place("<who> <x> <z> <rotation>") (place_command.hpp)
    Battle,       // battle {target}: battle("<target>"), such as story:2
    Load,         // load {save | scene}: a save file, or a new game on a map
    Trace,        // trace {path?}: writes the state trace to the file; without one it stops
    Quit,         // quit: the game closes
    Rescan,       // rescan: the packs' files are listed again (new files are seen)
    ReloadMap,    // reload_map {exit?}: rescan, then the current map read again and re-entered
    StoryState,   // story_state {path?}: the story's triggers, variables, script and the camera
    Overlay,      // overlay {on?}: the map's debug overlay on, off, or toggled without `on`
    Live,         // live: where the characters stand, the view and the newest warning, compactly
    PreviewMap,   // preview_map {map, patch, pack?}: the map entered again with the patch on top
    PreviewClear, // preview_clear: the preview dropped, the map as the packs give it
    LogTail,      // log_tail {since?}: the warnings and errors logged after `since`
    StorySet,     // story_set {kind, number, value}: a story trigger or variable written
  };
  Kind kind = Kind::Status;
  // The keys of press, hold and release, by name (keyNamed), in the order given.
  std::vector<platform::KeyCode> keys;
  std::vector<std::string> keyNames;
  // press: how long the keys stay down; advance: how many ticks run.
  std::uint64_t ticks = 0;
  // state: the path; script: the line; place: its argument; battle: the target; load: the map.
  std::string text;
  // screenshot: the picture; load: the save; trace: the file, none to stop it.
  std::optional<std::filesystem::path> path;
  // reload_map: the exit to enter by; none keeps the player where they stand.
  std::optional<std::int32_t> exit;
  // overlay: on or off; none toggles it.
  std::optional<bool> on;
  // preview_map: the patch's YAML text (the map is `text`) and the pack whose rights it has.
  std::string patch;
  std::optional<std::string> pack;
  // log_tail: the newest sequence number the client has; none for every entry kept.
  std::optional<std::uint64_t> since;
  // story_set: the trigger or variable written.
  StoryWrite story;
};

// A request line read: its id, echoed by the reply even when the rest is wrong, and the request.
struct ParsedControlRequest {
  JsonValue id;
  core::Result<ControlRequest> request;
};

ParsedControlRequest parseControlRequest(std::string_view line);

// Where the game is, for status: title, movie, interview (the new game's questions), field or
// battle, and the map of the game being played.
struct ControlStatus {
  std::string mode;
  std::string map;
  // For soak runs: the bindless table's slots in use and the GPU memory the allocator holds, when
  // the game has a renderer.
  std::optional<std::uint32_t> textures;
  std::optional<std::uint64_t> gpuBytes;
  // Whether the map debug overlay is on, when the game has one (for an editor that mirrors it).
  std::optional<bool> overlay;
  // The control channel's clients connected now, when the game counts them.
  std::optional<std::size_t> clients;
  // The map a preview_map patch is shown on; none without a preview.
  std::optional<std::string> preview;
};

// Where a character stands, in world space (docs/architecture/world-space.md), and its yaw in
// radians: it faces (sin yaw, 0, cos yaw), world +Z at yaw 0.
struct ControlPose {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
  float yaw = 0.0F;
};

struct ControlNpcPose {
  // The NPC's id in the map's script.
  std::int32_t id = 0;
  ControlPose pose;
};

// The background pixels the window shows: the top-left one and the size.
struct ControlView {
  float x = 0.0F;
  float y = 0.0F;
  float width = 0.0F;
  float height = 0.0F;
};

// What live answers, beside the tick and the pause the dispatcher knows.
struct ControlLive {
  std::string mode;
  std::string map;
  // A story script or section runs (or is about to).
  bool busy = false;
  std::optional<std::string> preview;
  // The sequence number of the newest warning or error log_tail keeps; 0 for none yet.
  std::uint64_t log = 0;
  // None without a game; kept during a battle (it is fought on the field).
  std::optional<ControlPose> player;
  std::optional<ControlPose> partner;
  std::vector<ControlNpcPose> npcs;
  // None while no map camera is up.
  std::optional<ControlView> view;
};

// What preview_map did: the collision swapped in place, or the map entered again.
struct ControlPreview {
  std::string map;
  bool reentered = false;
};

// What preview_clear did: the map the preview was dropped from (empty without a game), and
// whether there was a preview to drop.
struct ControlPreviewCleared {
  std::string map;
  bool cleared = false;
};

// A warning or an error the log wrote, numbered from 1 in the order written.
struct ControlLogEntry {
  std::uint64_t seq = 0;
  bool error = false;
  std::string text;
};

struct ControlLogTail {
  // The newest sequence number written; 0 for none yet.
  std::uint64_t next = 0;
  std::vector<ControlLogEntry> entries;
};

// Called once a screenshot was written (its path) or could not be.
using ControlShotDone = std::function<void(core::Result<std::string>)>;

// What reload_map did: the map entered again and the exit it was entered by, none when the player
// kept their place.
struct ControlReload {
  std::string map;
  std::optional<std::int32_t> exit;
};

// What the dispatcher asks of the game. The game builds it from its own parts; the tests fake it.
struct ControlHost {
  std::function<ControlStatus()> status;
  // The state trace's line of the tick that ran last; nothing when no game is being played.
  std::function<std::optional<std::string>()> state;
  std::function<core::Result<void>(std::string_view)> runScript;
  // Loads request.path (a save) or starts a new game on request.text (a map).
  std::function<core::Result<void>(const ControlRequest&)> load;
  std::function<void(const std::filesystem::path&, ControlShotDone)> screenshot;
  std::function<core::Result<void>(const std::optional<std::filesystem::path>&)> trace;
  std::function<void()> quit;
  // The map editor's commands. A host without one answers that the game cannot do it.
  std::function<core::Result<void>()> rescan;
  std::function<core::Result<ControlReload>(std::optional<std::int32_t> exit)> reloadMap;
  // The story's state as a JSON object (story_state_json.hpp).
  std::function<core::Result<std::string>()> storyState;
  // Turns the overlay on or off (none toggles it) and says whether it is on now.
  std::function<bool(std::optional<bool> on)> overlay;
  std::function<ControlLive()> live;
  // preview_map's request (map in text, patch, pack) applied, or why not.
  std::function<core::Result<ControlPreview>(const ControlRequest&)> previewMap;
  std::function<core::Result<ControlPreviewCleared>()> previewClear;
  // The warnings and errors kept, those after `since` (all without it).
  std::function<ControlLogTail(std::optional<std::uint64_t> since)> logTail;
  std::function<core::Result<void>(const StoryWrite&)> storySet;
};

// A reply line and the client it goes to.
struct ControlReply {
  std::uint64_t client = 0;
  std::string line;
};

class RemoteControl {
public:
  // Ticks an advance runs per frame at most, so the game keeps drawing while it advances.
  static constexpr int AdvanceTicksPerFrame = 4;

  // `paused`: the logic starts paused, before its first tick (--control-paused).
  explicit RemoteControl(ControlHost host, bool paused = false);

  // Handles a request line of a client; its reply, now or later, comes out of takeReplies for
  // that client.
  void receive(std::uint64_t client, std::string_view line);

  // A client left: its pending replies (press, advance and screenshot) are dropped. The ticks of
  // its presses and advances still run.
  void forget(std::uint64_t client);

  // The logic ticks to run this frame: the fixed step's when running, the advance's when paused.
  int ticksThisFrame(int stepTicks);

  // The key changes for the tick `tick` about to run: held keys and the keys of a press during its
  // ticks are down, every other key the channel pressed is up.
  void beforeTick(std::uint64_t tick, std::vector<platform::KeyChanged>& changes);

  // After a frame's ticks, with the ticks run so far: replies to the press and advance requests
  // that are done and to the screenshots written.
  void afterFrame(std::uint64_t ticksRun);

  [[nodiscard]] bool paused() const { return paused_; }
  // Paused with nothing to advance or press: the game may rest between frames.
  [[nodiscard]] bool idle() const;

  std::vector<ControlReply> takeReplies();

private:
  // A press's keys and the ticks they are down for, [from, to).
  struct Press {
    std::uint64_t from = 0;
    std::uint64_t to = 0;
    std::vector<platform::KeyCode> keys;
  };
  // A reply that waits for the logic to reach a tick.
  struct TickWait {
    std::uint64_t client = 0;
    std::string id;
    std::uint64_t until = 0;
    std::string fields;
  };
  // A screenshot being taken; the renderer fills `result`.
  struct Shot {
    std::uint64_t client = 0;
    std::string id;
    std::optional<core::Result<std::string>> result;
  };

  void reply(std::uint64_t client, std::string_view id, std::string_view fields);
  void fail(std::uint64_t client, std::string_view id, std::string_view problem);
  void handle(std::uint64_t client, const std::string& id, const ControlRequest& request);
  [[nodiscard]] std::string liveFields() const;
  [[nodiscard]] std::string statusFields() const;
  [[nodiscard]] std::string heldNames() const;
  // Ticks that run before the logic stands still again: those of the advance when paused.
  void advanceBy(std::uint64_t ticks);

  ControlHost host_;
  bool paused_ = false;
  // The ticks the logic has run, as afterFrame last heard.
  std::uint64_t ticks_ = 0;
  // Ticks still to run while paused.
  std::uint64_t pending_ = 0;
  std::vector<platform::KeyCode> held_;
  std::vector<std::string> heldNames_;
  std::deque<Press> presses_;
  // The keys the channel has down in the input now.
  std::vector<platform::KeyCode> down_;
  std::vector<TickWait> waits_;
  std::vector<std::shared_ptr<Shot>> shots_;
  std::vector<ControlReply> replies_;
};

} // namespace dwe::app
