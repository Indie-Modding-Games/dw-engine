#include <dwe/input/keys.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "control_json.hpp"
#include "remote_control.hpp"

namespace {

using dwe::app::ControlHost;
using dwe::app::ControlLive;
using dwe::app::ControlLogTail;
using dwe::app::ControlPose;
using dwe::app::ControlRequest;
using dwe::app::ControlShotDone;
using dwe::app::ControlStatus;
using dwe::app::ControlView;
using dwe::app::JsonValue;
using dwe::app::parseControlRequest;
using dwe::app::parseJson;
using dwe::app::RemoteControl;
using dwe::app::selectJson;
using dwe::app::StoryWrite;
using dwe::app::writeJson;
using dwe::platform::KeyChanged;
namespace keys = dwe::input::keys;

constexpr const char* TraceLine =
    R"({"src":"dwe","f":120,"time":2.0000,"in":4096,"map":"twna01","mode":"field",)"
    R"("player":{"x":256,"y":0,"z":-1285,"yaw":1024},)"
    R"("npcs":[{"x":-839,"y":0,"z":2210,"yaw":2048,"id":11},{"x":5,"y":0,"z":6,"yaw":0,"id":6}]})";

// A game that records what the channel asks of it.
struct FakeGame {
  std::optional<std::string> state = std::string{TraceLine};
  std::vector<std::string> scripts;
  std::vector<std::string> loads;
  std::optional<std::string> trace;
  ControlShotDone shot;
  bool quit = false;
  int rescans = 0;
  std::vector<std::optional<std::int32_t>> reloads;
  bool overlayOn = false;
  std::vector<std::string> previews;
  std::vector<StoryWrite> writes;
  bool storyBusy = false;

  ControlHost host() {
    return {
        .status =
            [] {
              return ControlStatus{.mode = "field",
                                   .map = "twna01",
                                   .textures = std::nullopt,
                                   .gpuBytes = std::nullopt,
                                   .overlay = std::nullopt,
                                   .clients = std::nullopt,
                                   .preview = std::nullopt};
            },
        .state = [this] { return state; },
        .runScript = [this](std::string_view code) -> dwe::core::Result<void> {
          scripts.emplace_back(code);
          return {};
        },
        .load = [this](const ControlRequest& request) -> dwe::core::Result<void> {
          loads.push_back(request.path ? request.path->string() : "scene " + request.text);
          return {};
        },
        .screenshot = [this](const std::filesystem::path&,
                             ControlShotDone done) { shot = std::move(done); },
        .trace =
            [this](const std::optional<std::filesystem::path>& path) -> dwe::core::Result<void> {
          trace = path ? path->string() : "stopped";
          return {};
        },
        .quit = [this] { quit = true; },
        .rescan = [this]() -> dwe::core::Result<void> {
          ++rescans;
          return {};
        },
        .reloadMap =
            [this](std::optional<std::int32_t> exit) -> dwe::core::Result<dwe::app::ControlReload> {
          reloads.push_back(exit);
          if (exit == 9) {
            return dwe::core::fail("map 'twna01' has no exit 9");
          }
          return dwe::app::ControlReload{.map = "twna01", .exit = exit};
        },
        .storyState = []() -> dwe::core::Result<std::string> {
          return std::string{R"({"triggers":[1,7],"variables":{"3":2},"script":{"busy":false}})"};
        },
        .overlay =
            [this](std::optional<bool> on) {
              overlayOn = on.value_or(!overlayOn);
              return overlayOn;
            },
        .live =
            [] {
              return ControlLive{
                  .mode = "field",
                  .map = "twna01",
                  .busy = true,
                  .preview = "twna01",
                  .log = 3,
                  .player = ControlPose{.x = 256.0F, .y = 0.0F, .z = 1285.5F, .yaw = 1.5F},
                  .partner = std::nullopt,
                  .npcs = {{.id = 11,
                            .pose = {.x = -839.0F, .y = 0.0F, .z = -2210.0F, .yaw = 0.0F}}},
                  .view = ControlView{.x = 12.0F, .y = 0.0F, .width = 320.0F, .height = 240.0F}};
            },
        .previewMap =
            [this](const ControlRequest& request) -> dwe::core::Result<dwe::app::ControlPreview> {
          previews.push_back(request.text + "|" + request.patch + "|" + request.pack.value_or("-"));
          if (request.text != "twna01") {
            return dwe::core::fail("the game shows twna01, not " + request.text);
          }
          return dwe::app::ControlPreview{.map = request.text,
                                          .reentered = !request.patch.starts_with("collision")};
        },
        .previewClear = [this]() -> dwe::core::Result<dwe::app::ControlPreviewCleared> {
          const bool had = !previews.empty();
          previews.clear();
          return dwe::app::ControlPreviewCleared{.map = "twna01", .cleared = had};
        },
        .logTail =
            [](std::optional<std::uint64_t> since) {
              ControlLogTail tail{.next = 2, .entries = {}};
              if (!since || *since < 1) {
                tail.entries.push_back({.seq = 1, .error = false, .text = "a \"warning\""});
              }
              if (!since || *since < 2) {
                tail.entries.push_back({.seq = 2, .error = true, .text = "an error"});
              }
              return tail;
            },
        .storySet = [this](const StoryWrite& write) -> dwe::core::Result<void> {
          if (storyBusy) {
            return dwe::core::fail("a story script is running");
          }
          writes.push_back(write);
          return {};
        },
    };
  }
};

// The reply lines, whoever they go to.
std::vector<std::string> lines(RemoteControl& control) {
  std::vector<std::string> out;
  for (dwe::app::ControlReply& reply : control.takeReplies()) {
    out.push_back(std::move(reply.line));
  }
  return out;
}

// A key going down (true) or up.
using Changes = std::vector<std::pair<dwe::platform::KeyCode, bool>>;

// The key changes the channel gives the input before the tick `at`.
Changes tick(RemoteControl& control, std::uint64_t at) {
  std::vector<KeyChanged> changes;
  control.beforeTick(at, changes);
  Changes out;
  for (const KeyChanged& change : changes) {
    CHECK_FALSE(change.repeat);
    out.emplace_back(change.key, change.down);
  }
  return out;
}

TEST_CASE("json values are read, picked by path and written back") {
  const auto value = parseJson(TraceLine);
  REQUIRE(value);
  CHECK(writeJson(*value).starts_with(R"({"src":"dwe","f":120,"time":2,"in":4096,)"));
  CHECK(writeJson(*selectJson(*value, "player")) == R"({"x":256,"y":0,"z":-1285,"yaw":1024})");
  CHECK(writeJson(*selectJson(*value, "player.z")) == "-1285");
  CHECK(writeJson(*selectJson(*value, "npcs.1.id")) == "6");
  CHECK(writeJson(*selectJson(*value, "npcs.id=6.x")) == "5");
  CHECK(writeJson(*selectJson(*value, "")) == writeJson(*value));
  const auto missing = selectJson(*value, "player.hp");
  REQUIRE_FALSE(missing);
  CHECK(missing.error().message.contains("'hp' under 'player'"));
  CHECK_FALSE(selectJson(*value, "npcs.id=7"));

  const auto text = parseJson(R"( ["a\"b\\cé\n", 1.5, -2e3, true, false, null, {}] )");
  REQUIRE(text);
  CHECK(writeJson(*text) == R"(["a\"b\\cé\n",1.5,-2000,true,false,null,{}])");
  CHECK_FALSE(parseJson(R"({"a":1,})"));
  CHECK_FALSE(parseJson(R"({"a":1} x)"));
  CHECK_FALSE(parseJson(R"("open)"));
  CHECK_FALSE(parseJson(std::string(200, '[')));
}

TEST_CASE("control requests are read with their fields, and wrong ones say why") {
  auto press =
      parseControlRequest(R"({"id":7,"cmd":"press","keys":["right","return"],"ticks":10})");
  REQUIRE(press.request);
  CHECK(writeJson(press.id) == "7");
  CHECK(press.request->kind == ControlRequest::Kind::Press);
  CHECK(press.request->keys == std::vector{keys::Right, keys::Return});
  CHECK(press.request->ticks == 10);

  auto joined = parseControlRequest(R"({"id":"a","cmd":"hold","keys":"up+left_shift"})");
  REQUIRE(joined.request);
  CHECK(joined.request->keyNames == std::vector<std::string>{"up", "left_shift"});

  CHECK(parseControlRequest(R"({"cmd":"press","keys":["up"]})").request->ticks == 6);
  CHECK(parseControlRequest(R"({"cmd":"release"})").request->keys.empty());
  CHECK(parseControlRequest(R"({"cmd":"state","path":"player.x"})").request->text == "player.x");
  CHECK(parseControlRequest(R"({"cmd":"load","save":"/tmp/a.sav"})").request->path ==
        std::filesystem::path{"/tmp/a.sav"});
  CHECK(parseControlRequest(R"({"cmd":"load","scene":"twna01"})").request->text == "twna01");

  const auto problem = [](std::string_view line) {
    const auto parsed = parseControlRequest(line);
    REQUIRE_FALSE(parsed.request);
    return parsed.request.error().message;
  };
  CHECK(problem(R"({"id":1,"cmd":"jump"})").contains("unknown cmd 'jump'"));
  CHECK(problem(R"({"cmd":"press","keys":["start"]})").contains("unknown key 'start'"));
  CHECK(problem(R"({"cmd":"press","keys":["up"],"ticks":0})").contains("at least one tick"));
  CHECK(problem(R"({"cmd":"advance","ticks":-1})").contains("whole number"));
  CHECK(problem(R"({"cmd":"advance"})").contains("'ticks' is missing"));
  CHECK(problem(R"({"cmd":"place","text":"player 1 2"})").contains("expected"));
  CHECK(problem(R"({"cmd":"load","save":"a","scene":"b"})").contains("one of them"));
  CHECK(problem(R"({"cmd":"screenshot"})").contains("'path'"));
  CHECK(problem("not json").contains("json:"));
  // The id comes back even when the rest of the request is wrong.
  CHECK(writeJson(parseControlRequest(R"({"id":[1,2],"cmd":"x"})").id) == "[1,2]");
}

TEST_CASE("the status gives the textures, the GPU memory, the overlay, the clients and the "
          "preview the game reports") {
  FakeGame game;
  ControlHost host = game.host();
  host.status = [] {
    return ControlStatus{.mode = "field",
                         .map = "twna01",
                         .textures = 812,
                         .gpuBytes = 9437184,
                         .overlay = true,
                         .clients = 2,
                         .preview = "twna01"};
  };
  RemoteControl control{std::move(host)};
  control.receive(1, R"({"id":1,"cmd":"status"})");
  const auto replies = lines(control);
  REQUIRE(replies.size() == 1);
  CHECK(replies[0] ==
        R"({"id":1,"ok":true,"tick":0,"paused":false,"mode":"field","map":"twna01",)"
        R"("held":[],"pending":0,"textures":812,"gpu_bytes":9437184,"overlay":true,"clients":2,)"
        R"("preview":"twna01"})");
}

TEST_CASE("the dispatcher answers at once what needs no ticks") {
  FakeGame game;
  RemoteControl control{game.host()};
  control.receive(1, R"({"id":1,"cmd":"status"})");
  control.receive(1, R"({"id":2,"cmd":"state","path":"npcs.id=11.z"})");
  control.receive(1, R"({"id":3,"cmd":"place","text":"npc:6 243 2565 2109"})");
  control.receive(1, R"({"id":4,"cmd":"battle","target":"story:2"})");
  control.receive(1, R"j({"id":5,"cmd":"battle","target":"x\")"})j");
  control.receive(1, R"({"id":6,"cmd":"load","scene":"twna01,2"})");
  control.receive(1, R"({"id":7,"cmd":"trace","path":"/tmp/t.jsonl"})");
  control.receive(1, R"({"id":8,"cmd":"bogus"})");
  control.receive(1, R"({"id":9,"cmd":"quit"})");
  const auto replies = lines(control);
  REQUIRE(replies.size() == 9);
  CHECK(replies[0] == R"({"id":1,"ok":true,"tick":0,"paused":false,"mode":"field","map":"twna01",)"
                      R"("held":[],"pending":0,"preview":null})");
  CHECK(replies[1] == R"({"id":2,"ok":true,"tick":0,"state":2210})");
  CHECK(replies[2] == R"j({"id":3,"ok":true,"ran":"place(\"npc:6 243 2565 2109\")"})j");
  CHECK(replies[3] == R"j({"id":4,"ok":true,"ran":"battle(\"story:2\")"})j");
  CHECK(replies[4].starts_with(R"({"id":5,"ok":false,"error":)"));
  CHECK(replies[5].starts_with(R"({"id":6,"ok":true,"tick":0)"));
  CHECK(replies[6] == R"({"id":7,"ok":true,"trace":"/tmp/t.jsonl"})");
  CHECK(replies[7].contains("unknown cmd 'bogus'"));
  CHECK(replies[8] == R"({"id":9,"ok":true})");
  CHECK(game.scripts ==
        std::vector<std::string>{R"(place("npc:6 243 2565 2109"))", R"(battle("story:2"))"});
  CHECK(game.loads == std::vector<std::string>{"scene twna01,2"});
  CHECK(game.trace == "/tmp/t.jsonl");
  CHECK(game.quit);

  game.state.reset();
  control.receive(1, R"({"id":10,"cmd":"state"})");
  CHECK(lines(control).front().contains("no game is being played"));
}

TEST_CASE("the map editor's commands reload the map, dump the story and toggle the overlay") {
  FakeGame game;
  RemoteControl control{game.host()};
  control.receive(1, R"({"id":1,"cmd":"rescan"})");
  control.receive(1, R"({"id":2,"cmd":"reload_map"})");
  control.receive(1, R"({"id":3,"cmd":"reload_map","exit":2})");
  control.receive(1, R"({"id":4,"cmd":"reload_map","exit":9})");
  control.receive(1, R"({"id":5,"cmd":"reload_map","exit":10})");
  control.receive(1, R"({"id":6,"cmd":"story_state"})");
  control.receive(1, R"({"id":7,"cmd":"story_state","path":"triggers.1"})");
  control.receive(1, R"({"id":8,"cmd":"overlay"})");
  control.receive(1, R"({"id":9,"cmd":"overlay","on":false})");
  control.receive(1, R"({"id":10,"cmd":"overlay","on":"yes"})");
  const auto replies = lines(control);
  REQUIRE(replies.size() == 10);
  CHECK(replies[0] == R"({"id":1,"ok":true})");
  CHECK(replies[1] == R"({"id":2,"ok":true,"map":"twna01","exit":null,"tick":0})");
  CHECK(replies[2] == R"({"id":3,"ok":true,"map":"twna01","exit":2,"tick":0})");
  CHECK(replies[3] == R"({"id":4,"ok":false,"error":"reload_map: map 'twna01' has no exit 9"})");
  CHECK(replies[4].contains("'exit' must be a whole number from 0 to 9"));
  CHECK(replies[5] == R"({"id":6,"ok":true,"tick":0,"story":{"triggers":[1,7],)"
                      R"("variables":{"3":2},"script":{"busy":false}}})");
  CHECK(replies[6] == R"({"id":7,"ok":true,"tick":0,"story":7})");
  CHECK(replies[7] == R"({"id":8,"ok":true,"overlay":true})");
  CHECK(replies[8] == R"({"id":9,"ok":true,"overlay":false})");
  CHECK(replies[9].contains("'on' must be true or false"));
  CHECK(game.rescans == 1);
  CHECK(game.reloads == std::vector<std::optional<std::int32_t>>{std::nullopt, 2, 9});

  // A host without the editor's commands says so.
  ControlHost bare = game.host();
  bare.reloadMap = nullptr;
  bare.storyState = nullptr;
  RemoteControl plain{std::move(bare)};
  plain.receive(1, R"({"id":1,"cmd":"reload_map"})");
  plain.receive(1, R"({"id":2,"cmd":"story_state"})");
  const auto refused = lines(plain);
  REQUIRE(refused.size() == 2);
  CHECK(refused[0].contains("cannot reload its map"));
  CHECK(refused[1].contains("no story to show"));
}

TEST_CASE("a press holds its keys for exactly its ticks, and answers when they ran") {
  FakeGame game;
  RemoteControl control{game.host()};
  control.afterFrame(100);
  control.receive(1, R"({"id":1,"cmd":"press","keys":["right"],"ticks":3})");
  control.receive(1, R"({"id":2,"cmd":"press","keys":["down"],"ticks":2})");
  CHECK(lines(control).empty());
  // Running: the fixed step's ticks run.
  CHECK(control.ticksThisFrame(2) == 2);
  CHECK(tick(control, 100) == Changes{{keys::Right, true}});
  CHECK(tick(control, 101).empty());
  control.afterFrame(102);
  CHECK(lines(control).empty());
  CHECK(tick(control, 102).empty());
  // The second press follows the first tick for tick.
  CHECK(tick(control, 103) == Changes{{keys::Right, false}, {keys::Down, true}});
  control.afterFrame(104);
  const auto first = lines(control);
  REQUIRE(first.size() == 1);
  CHECK(first[0] == R"({"id":1,"ok":true,"keys":["right"],"from":100,"ticks":3,"tick":104})");
  CHECK(tick(control, 104).empty());
  CHECK(tick(control, 105) == Changes{{keys::Down, false}});
  control.afterFrame(105);
  CHECK(lines(control).front().starts_with(R"({"id":2,"ok":true,"keys":["down"],"from":103)"));
}

TEST_CASE("paused, the logic runs only the ticks advanced or pressed") {
  FakeGame game;
  RemoteControl control{game.host(), true};
  CHECK(control.paused());
  CHECK(control.idle());
  CHECK(control.ticksThisFrame(3) == 0);

  control.receive(1, R"({"id":1,"cmd":"advance","ticks":10})");
  CHECK_FALSE(control.idle());
  std::uint64_t ran = 0;
  std::vector<int> frames;
  while (!control.idle()) {
    const int ticks = control.ticksThisFrame(1);
    frames.push_back(ticks);
    ran += static_cast<std::uint64_t>(ticks);
    control.afterFrame(ran);
  }
  CHECK(frames == std::vector{4, 4, 2});
  CHECK(lines(control) ==
        std::vector<std::string>{R"({"id":1,"ok":true,"advanced":10,"tick":10})"});

  // A press while paused runs its own ticks; a held key stays down across them and after.
  control.receive(1, R"({"id":2,"cmd":"hold","keys":["left_shift"]})");
  control.receive(1, R"({"id":3,"cmd":"press","keys":["up"],"ticks":2})");
  CHECK(lines(control) == std::vector<std::string>{R"({"id":2,"ok":true,"held":["left_shift"]})"});
  CHECK(control.ticksThisFrame(0) == 2);
  CHECK(tick(control, 10) == Changes{{keys::LeftShift, true}, {keys::Up, true}});
  CHECK(tick(control, 11).empty());
  control.afterFrame(12);
  CHECK(control.idle());
  CHECK(lines(control).size() == 1);
  control.receive(1, R"({"id":4,"cmd":"release"})");
  control.receive(1, R"({"id":5,"cmd":"advance","ticks":1})");
  CHECK(control.ticksThisFrame(0) == 1);
  CHECK(tick(control, 12) == Changes{{keys::LeftShift, false}, {keys::Up, false}});

  control.receive(1, R"({"id":6,"cmd":"resume"})");
  CHECK_FALSE(control.paused());
  CHECK(control.ticksThisFrame(3) == 3);
}

TEST_CASE("a screenshot answers once the frame was written") {
  FakeGame game;
  RemoteControl control{game.host()};
  control.receive(1, R"({"id":"s","cmd":"screenshot","path":"/tmp/shot.png"})");
  REQUIRE(game.shot);
  control.afterFrame(5);
  CHECK(lines(control).empty());
  game.shot(std::string{"/tmp/shot.png"});
  control.afterFrame(6);
  CHECK(lines(control) ==
        std::vector<std::string>{R"({"id":"s","ok":true,"path":"/tmp/shot.png","tick":6})"});
}

TEST_CASE("each client gets the replies to its own requests, and a client gone gets none") {
  FakeGame game;
  RemoteControl control{game.host(), true};
  control.receive(1, R"({"id":1,"cmd":"advance","ticks":2})");
  control.receive(2, R"({"id":1,"cmd":"status"})");
  control.receive(2, R"({"id":2,"cmd":"advance","ticks":1})");
  control.receive(3, R"({"id":"s","cmd":"screenshot","path":"/tmp/shot.png"})");
  auto now = control.takeReplies();
  REQUIRE(now.size() == 1);
  CHECK(now[0].client == 2);
  CHECK(now[0].line.starts_with(R"({"id":1,"ok":true,"tick":0,"paused":true)"));

  // Client 3 leaves before its picture is written: its reply is dropped. The ticks still run.
  control.forget(3);
  game.shot(std::string{"/tmp/shot.png"});
  CHECK(control.ticksThisFrame(0) == 3);
  control.afterFrame(3);
  auto done = control.takeReplies();
  REQUIRE(done.size() == 2);
  CHECK(done[0].client == 1);
  CHECK(done[0].line == R"({"id":1,"ok":true,"advanced":2,"tick":3})");
  CHECK(done[1].client == 2);
  CHECK(done[1].line == R"({"id":2,"ok":true,"advanced":1,"tick":3})");

  // A wait of a client that left is dropped, the other's answered.
  control.receive(1, R"({"id":3,"cmd":"press","keys":["up"],"ticks":2})");
  control.receive(2, R"({"id":4,"cmd":"advance","ticks":2})");
  control.forget(1);
  CHECK(control.ticksThisFrame(0) == 4);
  control.afterFrame(7);
  auto rest = control.takeReplies();
  REQUIRE(rest.size() == 1);
  CHECK(rest[0].client == 2);
}

TEST_CASE("live answers where everyone stands in one compact line") {
  FakeGame game;
  RemoteControl control{game.host()};
  control.afterFrame(42);
  control.receive(1, R"({"id":1,"cmd":"live"})");
  CHECK(lines(control) ==
        std::vector<std::string>{
            R"({"id":1,"ok":true,"tick":42,"paused":false,"mode":"field","map":"twna01",)"
            R"("busy":true,"preview":"twna01","log":3,)"
            R"("player":{"x":256,"y":0,"z":1285.5,"yaw":1.5},"partner":null,)"
            R"("npcs":[{"id":11,"x":-839,"y":0,"z":-2210,"yaw":0}],)"
            R"("view":{"x":12,"y":0,"w":320,"h":240}})"});

  ControlHost title = game.host();
  title.live = [] {
    ControlLive live;
    live.mode = "title";
    return live;
  };
  RemoteControl idle{std::move(title)};
  idle.receive(1, R"({"id":2,"cmd":"live"})");
  CHECK(lines(idle) ==
        std::vector<std::string>{
            R"({"id":2,"ok":true,"tick":0,"paused":false,"mode":"title","map":"","busy":false,)"
            R"("preview":null,"log":0,"player":null,"partner":null,"npcs":[],"view":null})"});
}

TEST_CASE("live stays cheap with a crowded map") {
  // A map holds a few dozen NPCs at most; 64 here, polled a thousand times.
  FakeGame game;
  ControlHost host = game.host();
  host.live = [] {
    ControlLive live;
    live.mode = "field";
    live.map = "twna01";
    live.player = ControlPose{.x = 256.25F, .y = 0.0F, .z = 1285.5F, .yaw = 1.5707964F};
    live.partner = live.player;
    for (std::int32_t id = 0; id < 64; ++id) {
      live.npcs.push_back(
          {.id = id,
           .pose = {
               .x = -839.125F * static_cast<float>(id), .y = 0.0F, .z = 2210.5F, .yaw = 3.0F}});
    }
    live.view = ControlView{.x = 12.0F, .y = 0.0F, .width = 320.0F, .height = 240.0F};
    return live;
  };
  RemoteControl control{std::move(host)};
  constexpr int Calls = 1000;
  std::size_t bytes = 0;
  const auto start = std::chrono::steady_clock::now();
  for (int call = 0; call < Calls; ++call) {
    control.receive(1, R"({"id":1,"cmd":"live"})");
    for (const dwe::app::ControlReply& reply : control.takeReplies()) {
      bytes += reply.line.size();
    }
  }
  const std::chrono::duration<double, std::micro> spent = std::chrono::steady_clock::now() - start;
  MESSAGE("live with 64 NPCs: " << spent.count() / Calls << " us and " << bytes / Calls
                                << " bytes per call");
  CHECK(bytes > 0);
}

TEST_CASE("preview_map and preview_clear pass the patch on and say what was done") {
  FakeGame game;
  RemoteControl control{game.host()};
  control.receive(
      1, R"({"id":1,"cmd":"preview_map","map":"twna01","patch":"collision:\n  cells: []\n"})");
  control.receive(
      1, R"({"id":2,"cmd":"preview_map","map":"twna01","patch":"warps: []","pack":"mod_a"})");
  control.receive(1, R"({"id":3,"cmd":"preview_map","map":"twna02","patch":""})");
  control.receive(1, R"({"id":4,"cmd":"preview_map","patch":"{}"})");
  control.receive(1, R"({"id":5,"cmd":"preview_map","map":"twna01"})");
  control.receive(1, R"({"id":6,"cmd":"preview_map","map":"twna01","patch":"{}","pack":3})");
  control.receive(1, R"({"id":7,"cmd":"preview_clear"})");
  control.receive(1, R"({"id":8,"cmd":"preview_clear"})");
  const auto replies = lines(control);
  REQUIRE(replies.size() == 8);
  CHECK(replies[0] == R"({"id":1,"ok":true,"map":"twna01","applied":"collision","tick":0})");
  CHECK(replies[1] == R"({"id":2,"ok":true,"map":"twna01","applied":"reentered","tick":0})");
  CHECK(replies[2] ==
        R"({"id":3,"ok":false,"error":"preview_map: the game shows twna01, not twna02"})");
  CHECK(replies[3].contains("preview_map: 'map' must be a non-empty string"));
  CHECK(replies[4].contains("preview_map: 'patch' must be the patch's YAML text"));
  CHECK(replies[5].contains("preview_map: 'pack' must be a non-empty string"));
  CHECK(replies[6] == R"({"id":7,"ok":true,"map":"twna01","cleared":true})");
  CHECK(replies[7] == R"({"id":8,"ok":true,"map":"twna01","cleared":false})");
  CHECK(game.previews.empty());
  // An empty patch is one that changes nothing.
  CHECK_FALSE(
      parseControlRequest(R"({"cmd":"preview_map","map":"a","patch":""})").request->patch.empty());

  ControlHost bare = game.host();
  bare.previewMap = nullptr;
  RemoteControl plain{std::move(bare)};
  plain.receive(1, R"({"id":1,"cmd":"preview_map","map":"twna01","patch":"{}"})");
  CHECK(lines(plain).front().contains("cannot preview a map"));
}

TEST_CASE("log_tail gives the warnings and errors after the one asked for") {
  FakeGame game;
  RemoteControl control{game.host()};
  control.receive(1, R"({"id":1,"cmd":"log_tail"})");
  control.receive(1, R"({"id":2,"cmd":"log_tail","since":1})");
  control.receive(1, R"({"id":3,"cmd":"log_tail","since":2})");
  control.receive(1, R"({"id":4,"cmd":"log_tail","since":-1})");
  const auto replies = lines(control);
  REQUIRE(replies.size() == 4);
  CHECK(replies[0] == R"({"id":1,"ok":true,"next":2,"entries":[)"
                      R"({"seq":1,"level":"warning","text":"a \"warning\""},)"
                      R"({"seq":2,"level":"error","text":"an error"}]})");
  CHECK(replies[1] == R"({"id":2,"ok":true,"next":2,"entries":[)"
                      R"({"seq":2,"level":"error","text":"an error"}]})");
  CHECK(replies[2] == R"({"id":3,"ok":true,"next":2,"entries":[]})");
  CHECK(replies[3].contains("'since' must be a whole number"));
}

TEST_CASE("story_set writes a trigger or a variable, and says why it cannot") {
  const auto request = [](std::string_view line) { return parseControlRequest(line).request; };
  const auto on = request(R"({"cmd":"story_set","kind":"trigger","number":12,"value":true})");
  REQUIRE(on);
  CHECK(on->story.kind == StoryWrite::Kind::Trigger);
  CHECK(on->story.number == 12);
  CHECK(on->story.value == 1);
  CHECK(request(R"({"cmd":"story_set","kind":"trigger","number":12,"value":0})")->story.value == 0);
  const auto variable = request(R"({"cmd":"story_set","kind":"var","number":3,"value":200})");
  REQUIRE(variable);
  CHECK(variable->story.kind == StoryWrite::Kind::Variable);
  CHECK(variable->story.value == 200);

  const auto problem = [&](std::string_view line) {
    const auto parsed = request(line);
    REQUIRE_FALSE(parsed);
    return parsed.error().message;
  };
  CHECK(problem(R"({"cmd":"story_set","number":1,"value":1})").contains("'kind'"));
  CHECK(problem(R"({"cmd":"story_set","kind":"var","value":1})").contains("'number' is missing"));
  CHECK(problem(R"({"cmd":"story_set","kind":"var","number":1})").contains("'value' is missing"));
  CHECK(problem(R"({"cmd":"story_set","kind":"var","number":1,"value":256})").contains("0 to 255"));
  CHECK(problem(R"({"cmd":"story_set","kind":"trigger","number":1,"value":2})")
            .contains("true, false, 1 or 0"));
  CHECK(problem(R"({"cmd":"story_set","kind":"var","number":-1,"value":1})")
            .starts_with("story_set: 'number'"));

  FakeGame game;
  RemoteControl control{game.host()};
  control.receive(1, R"({"id":1,"cmd":"story_set","kind":"trigger","number":12,"value":true})");
  control.receive(1, R"({"id":2,"cmd":"story_set","kind":"var","number":3,"value":7})");
  game.storyBusy = true;
  control.receive(1, R"({"id":3,"cmd":"story_set","kind":"var","number":3,"value":8})");
  const auto replies = lines(control);
  REQUIRE(replies.size() == 3);
  CHECK(replies[0] == R"({"id":1,"ok":true,"kind":"trigger","number":12,"value":true})");
  CHECK(replies[1] == R"({"id":2,"ok":true,"kind":"var","number":3,"value":7})");
  CHECK(replies[2] == R"({"id":3,"ok":false,"error":"story_set: a story script is running"})");
  REQUIRE(game.writes.size() == 2);
  CHECK(game.writes[1].value == 7);
}

} // namespace
