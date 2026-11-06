#include <dwe/input/keys.hpp>

#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "control_json.hpp"
#include "remote_control.hpp"

namespace {

using dwe::app::ControlHost;
using dwe::app::ControlRequest;
using dwe::app::ControlShotDone;
using dwe::app::ControlStatus;
using dwe::app::JsonValue;
using dwe::app::parseControlRequest;
using dwe::app::parseJson;
using dwe::app::RemoteControl;
using dwe::app::selectJson;
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

  ControlHost host() {
    return {
        .status =
            [] {
              return ControlStatus{.mode = "field",
                                   .map = "twna01",
                                   .textures = std::nullopt,
                                   .gpuBytes = std::nullopt,
                                   .overlay = std::nullopt};
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
    };
  }
};

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

TEST_CASE("the status gives the textures, the GPU memory and the overlay the game reports") {
  FakeGame game;
  ControlHost host = game.host();
  host.status = [] {
    return ControlStatus{
        .mode = "field", .map = "twna01", .textures = 812, .gpuBytes = 9437184, .overlay = true};
  };
  RemoteControl control{std::move(host)};
  control.receive(R"({"id":1,"cmd":"status"})");
  const auto replies = control.takeReplies();
  REQUIRE(replies.size() == 1);
  CHECK(replies[0] ==
        R"({"id":1,"ok":true,"tick":0,"paused":false,"mode":"field","map":"twna01",)"
        R"("held":[],"pending":0,"textures":812,"gpu_bytes":9437184,"overlay":true})");
}

TEST_CASE("the dispatcher answers at once what needs no ticks") {
  FakeGame game;
  RemoteControl control{game.host()};
  control.receive(R"({"id":1,"cmd":"status"})");
  control.receive(R"({"id":2,"cmd":"state","path":"npcs.id=11.z"})");
  control.receive(R"({"id":3,"cmd":"place","text":"npc:6 243 2565 2109"})");
  control.receive(R"({"id":4,"cmd":"battle","target":"story:2"})");
  control.receive(R"j({"id":5,"cmd":"battle","target":"x\")"})j");
  control.receive(R"({"id":6,"cmd":"load","scene":"twna01,2"})");
  control.receive(R"({"id":7,"cmd":"trace","path":"/tmp/t.jsonl"})");
  control.receive(R"({"id":8,"cmd":"bogus"})");
  control.receive(R"({"id":9,"cmd":"quit"})");
  const auto replies = control.takeReplies();
  REQUIRE(replies.size() == 9);
  CHECK(replies[0] == R"({"id":1,"ok":true,"tick":0,"paused":false,"mode":"field","map":"twna01",)"
                      R"("held":[],"pending":0})");
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
  control.receive(R"({"id":10,"cmd":"state"})");
  CHECK(control.takeReplies().front().contains("no game is being played"));
}

TEST_CASE("the map editor's commands reload the map, dump the story and toggle the overlay") {
  FakeGame game;
  RemoteControl control{game.host()};
  control.receive(R"({"id":1,"cmd":"rescan"})");
  control.receive(R"({"id":2,"cmd":"reload_map"})");
  control.receive(R"({"id":3,"cmd":"reload_map","exit":2})");
  control.receive(R"({"id":4,"cmd":"reload_map","exit":9})");
  control.receive(R"({"id":5,"cmd":"reload_map","exit":10})");
  control.receive(R"({"id":6,"cmd":"story_state"})");
  control.receive(R"({"id":7,"cmd":"story_state","path":"triggers.1"})");
  control.receive(R"({"id":8,"cmd":"overlay"})");
  control.receive(R"({"id":9,"cmd":"overlay","on":false})");
  control.receive(R"({"id":10,"cmd":"overlay","on":"yes"})");
  const auto replies = control.takeReplies();
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
  plain.receive(R"({"id":1,"cmd":"reload_map"})");
  plain.receive(R"({"id":2,"cmd":"story_state"})");
  const auto refused = plain.takeReplies();
  REQUIRE(refused.size() == 2);
  CHECK(refused[0].contains("cannot reload its map"));
  CHECK(refused[1].contains("no story to show"));
}

TEST_CASE("a press holds its keys for exactly its ticks, and answers when they ran") {
  FakeGame game;
  RemoteControl control{game.host()};
  control.afterFrame(100);
  control.receive(R"({"id":1,"cmd":"press","keys":["right"],"ticks":3})");
  control.receive(R"({"id":2,"cmd":"press","keys":["down"],"ticks":2})");
  CHECK(control.takeReplies().empty());
  // Running: the fixed step's ticks run.
  CHECK(control.ticksThisFrame(2) == 2);
  CHECK(tick(control, 100) == Changes{{keys::Right, true}});
  CHECK(tick(control, 101).empty());
  control.afterFrame(102);
  CHECK(control.takeReplies().empty());
  CHECK(tick(control, 102).empty());
  // The second press follows the first tick for tick.
  CHECK(tick(control, 103) == Changes{{keys::Right, false}, {keys::Down, true}});
  control.afterFrame(104);
  const auto first = control.takeReplies();
  REQUIRE(first.size() == 1);
  CHECK(first[0] == R"({"id":1,"ok":true,"keys":["right"],"from":100,"ticks":3,"tick":104})");
  CHECK(tick(control, 104).empty());
  CHECK(tick(control, 105) == Changes{{keys::Down, false}});
  control.afterFrame(105);
  CHECK(
      control.takeReplies().front().starts_with(R"({"id":2,"ok":true,"keys":["down"],"from":103)"));
}

TEST_CASE("paused, the logic runs only the ticks advanced or pressed") {
  FakeGame game;
  RemoteControl control{game.host(), true};
  CHECK(control.paused());
  CHECK(control.idle());
  CHECK(control.ticksThisFrame(3) == 0);

  control.receive(R"({"id":1,"cmd":"advance","ticks":10})");
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
  CHECK(control.takeReplies() ==
        std::vector<std::string>{R"({"id":1,"ok":true,"advanced":10,"tick":10})"});

  // A press while paused runs its own ticks; a held key stays down across them and after.
  control.receive(R"({"id":2,"cmd":"hold","keys":["left_shift"]})");
  control.receive(R"({"id":3,"cmd":"press","keys":["up"],"ticks":2})");
  CHECK(control.takeReplies() ==
        std::vector<std::string>{R"({"id":2,"ok":true,"held":["left_shift"]})"});
  CHECK(control.ticksThisFrame(0) == 2);
  CHECK(tick(control, 10) == Changes{{keys::LeftShift, true}, {keys::Up, true}});
  CHECK(tick(control, 11).empty());
  control.afterFrame(12);
  CHECK(control.idle());
  CHECK(control.takeReplies().size() == 1);
  control.receive(R"({"id":4,"cmd":"release"})");
  control.receive(R"({"id":5,"cmd":"advance","ticks":1})");
  CHECK(control.ticksThisFrame(0) == 1);
  CHECK(tick(control, 12) == Changes{{keys::LeftShift, false}, {keys::Up, false}});

  control.receive(R"({"id":6,"cmd":"resume"})");
  CHECK_FALSE(control.paused());
  CHECK(control.ticksThisFrame(3) == 3);
}

TEST_CASE("a screenshot answers once the frame was written") {
  FakeGame game;
  RemoteControl control{game.host()};
  control.receive(R"({"id":"s","cmd":"screenshot","path":"/tmp/shot.png"})");
  REQUIRE(game.shot);
  control.afterFrame(5);
  CHECK(control.takeReplies().empty());
  game.shot(std::string{"/tmp/shot.png"});
  control.afterFrame(6);
  CHECK(control.takeReplies() ==
        std::vector<std::string>{R"({"id":"s","ok":true,"path":"/tmp/shot.png","tick":6})"});
}

} // namespace
