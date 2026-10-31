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

namespace dwe::app {

// The game's debug control channel (`dwe-game --control <socket>`, docs/guides/control-remoto.md):
// a tool outside the game (an MCP server, a test) sends a JSON request per line and gets a JSON
// reply per line. It reads the state as the state trace writes it, presses keys tick by tick,
// pauses the logic and advances it a number of ticks, places characters, starts battles, loads a
// save or a map and takes screenshots. Off unless asked for; the game then does nothing for it.
//
// A request is {"id": <any>, "cmd": "<command>", ...}; the reply echoes the id with "ok": true and
// what was asked, or "ok": false and an "error". Replies to press, advance and screenshot come
// once the ticks ran or the picture was written.

struct ControlRequest {
  enum class Kind : std::uint8_t {
    Status,     // status: tick, paused, mode, map, held keys
    State,      // state {path?}: the state trace's line of now, or the part the path names
    Press,      // press {keys, ticks = 6}: the keys down for exactly that many ticks
    Hold,       // hold {keys}: the keys down until released
    Release,    // release {keys?}: those keys, or all of them, up
    Pause,      // pause: no logic tick runs until resume or advance
    Resume,     // resume: the logic runs at its pace again
    Advance,    // advance {ticks}: that many ticks run; paused, exactly them and no more
    Screenshot, // screenshot {path}: the next frame drawn, as a PNG
    Script,     // script {code}: a line of the game's scripts, as the console runs it
    Place,      // place {text}: place("<who> <x> <z> <rotation>") (place_command.hpp)
    Battle,     // battle {target}: battle("<target>"), such as story:2
    Load,       // load {save | scene}: a save file, or a new game on a map
    Trace,      // trace {path?}: writes the state trace to the file; without one it stops
    Quit,       // quit: the game closes
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
};

// Called once a screenshot was written (its path) or could not be.
using ControlShotDone = std::function<void(core::Result<std::string>)>;

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
};

class RemoteControl {
public:
  // Ticks an advance runs per frame at most, so the game keeps drawing while it advances.
  static constexpr int AdvanceTicksPerFrame = 4;

  // `paused`: the logic starts paused, before its first tick (--control-paused).
  explicit RemoteControl(ControlHost host, bool paused = false);

  // Handles a request line; its reply, now or later, comes out of takeReplies.
  void receive(std::string_view line);

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

  std::vector<std::string> takeReplies();

private:
  // A press's keys and the ticks they are down for, [from, to).
  struct Press {
    std::uint64_t from = 0;
    std::uint64_t to = 0;
    std::vector<platform::KeyCode> keys;
  };
  // A reply that waits for the logic to reach a tick.
  struct TickWait {
    std::string id;
    std::uint64_t until = 0;
    std::string fields;
  };
  // A screenshot being taken; the renderer fills `result`.
  struct Shot {
    std::string id;
    std::optional<core::Result<std::string>> result;
  };

  void reply(std::string_view id, std::string_view fields);
  void fail(std::string_view id, std::string_view problem);
  void handle(const std::string& id, const ControlRequest& request);
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
  std::vector<std::string> replies_;
};

} // namespace dwe::app
