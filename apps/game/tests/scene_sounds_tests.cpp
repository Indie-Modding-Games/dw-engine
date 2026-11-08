#include <dwe/game/story/story_ports.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "gym_box_close.hpp"
#include "scene_sounds.hpp"

using dwe::app::HeldSceneSounds;
using dwe::app::PartnerSceneHolds;

namespace {

// A port whose held sounds keep playing until they are stopped, as a looping sample does.
class LoopingPort final : public dwe::game::story::SoundPort {
public:
  void playSound(std::int64_t slot, std::int64_t sound) override {
    played.emplace_back(slot, sound);
  }
  std::optional<std::int64_t> playHeldSound(std::int64_t slot, std::int64_t sound) override {
    played.emplace_back(slot, sound);
    playing.insert(next);
    return next++;
  }
  void stopHeldSound(std::int64_t held) override { playing.erase(held); }

  std::vector<std::pair<std::int64_t, std::int64_t>> played;
  std::set<std::int64_t> playing;

private:
  std::int64_t next = 0;
};

// A port that cannot hold its sounds (story_ports.hpp: playHeldSound's default).
class PlainPort final : public dwe::game::story::SoundPort {
public:
  void playSound(std::int64_t /*slot*/, std::int64_t /*sound*/) override { ++plays; }

  int plays = 0;
};

} // namespace

TEST_CASE("scene sounds: the burst's looping sounds stop with stopSound, nothing outlives it") {
  LoopingPort port;
  HeldSceneSounds sounds;
  // evl.c:416: playSound2(8, 1) and playSound2(8, 0) at the burst.
  sounds.play(&port, 1);
  sounds.play(&port, 0);
  CHECK(port.played == std::vector<std::pair<std::int64_t, std::int64_t>>{{8, 1}, {8, 0}});
  CHECK(port.playing.size() == 2U);
  CHECK(sounds.held() == 2U);

  // evl.c:503: stopSound, then the roar.
  sounds.stopAll(&port);
  sounds.play(&port, 5);
  CHECK(port.playing.size() == 1U);
  CHECK(sounds.held() == 1U);

  // The scene's end (or a skip) stops what is left.
  sounds.stopAll(&port);
  CHECK(port.playing.empty());
  CHECK(sounds.held() == 0U);
}

TEST_CASE("scene sounds: without a port or a port that cannot hold, nothing is kept") {
  HeldSceneSounds sounds;
  sounds.play(nullptr, 1);
  CHECK(sounds.held() == 0U);
  sounds.stopAll(nullptr);

  PlainPort port;
  sounds.play(&port, 1);
  CHECK(port.plays == 1);
  CHECK(sounds.held() == 0U);
}

TEST_CASE("gym box: the last box closes beside the partner's scenes, the field's tick does it "
          "otherwise") {
  using dwe::app::closesGymBoxBesideScene;
  // An evolution right after a session: the stats box keeps shrinking under the scene.
  CHECK(closesGymBoxBesideScene(false, PartnerSceneHolds{.evolution = true}));
  CHECK(closesGymBoxBesideScene(false, PartnerSceneHolds{.death = true}));
  CHECK(closesGymBoxBesideScene(false, PartnerSceneHolds{.sleep = true}));
  CHECK(closesGymBoxBesideScene(false, PartnerSceneHolds{.pile = true}));
  CHECK(closesGymBoxBesideScene(false, PartnerSceneHolds{.toilet = true}));
  CHECK(closesGymBoxBesideScene(false, PartnerSceneHolds{.feed = true}));
  CHECK(closesGymBoxBesideScene(false, PartnerSceneHolds{.praiseScold = true}));
  // No scene: the field's own tick closes it; a running course ticks itself.
  CHECK_FALSE(closesGymBoxBesideScene(false, PartnerSceneHolds{}));
  CHECK_FALSE(closesGymBoxBesideScene(true, PartnerSceneHolds{.evolution = true}));
}
