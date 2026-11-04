#include <dwe/game/fishing/fishing.hpp>
#include <dwe/ruleset/random.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace {

using namespace dwe;
using namespace dwe::game::fishing;

// A view straight down on the water: a screen pixel is a world unit on x and z.
class TopView final : public FishingView {
public:
  [[nodiscard]] std::optional<glm::vec2> toScreen(glm::vec3 world) const override {
    return glm::vec2{world.x, world.z};
  }
  [[nodiscard]] std::optional<glm::vec3> toGround(glm::vec2 screen) const override {
    return glm::vec3{screen.x, 0.0F, screen.y};
  }
  [[nodiscard]] glm::vec2 screenMin() const override { return glm::vec2{-1000.0F}; }
  [[nodiscard]] glm::vec2 screenMax() const override { return glm::vec2{1000.0F}; }
  [[nodiscard]] ViewRange viewRange() const override { return {}; }
  [[nodiscard]] glm::ivec2 viewCentre() const override { return glm::ivec2{0}; }
};

// The population of FISH_tickFishPopulation: 33 slots, one new fish a frame below 32, the sea
// serpent looked for every 20 frames.
Fishing pond() {
  FishingRules rules;
  rules.population = {.pool = 33, .spawnBelow = 32, .lifetime = {.min = 900, .max = 1200}};
  rules.seaSerpent.checkEvery = 20;
  FishingTables tables;
  tables.kinds.assign(6, FishKind{.item = 0,
                                  .size = {.min = 20, .max = 120},
                                  .moveDelay = {.min = 5, .max = 20},
                                  .attractedBy = std::nullopt});
  tables.seaSerpent.size = {.min = 300, .max = 300};
  FishingPlace place;
  place.seaSerpentTimes = {800, 1700};
  place.shoreline = {.axis = Shoreline::Axis::X, .points = {{-1000, -1000}, {1000, -1000}}};
  place.spots.assign(6, FishingSpot{.weight = 1, .points = {{-300, -300}, {300, -300}, {0, 300}}});
  place.seaSerpentPath = {{-500, 0}, {500, 0}, {500, 500}};
  return Fishing{rules, tables, place};
}

bool hasSeaSerpent(const Fishing& water) {
  return std::ranges::any_of(water.fish(), [](const Fish& f) { return f.kind == SeaSerpentKind; });
}

} // namespace

TEST_CASE("fishing water: with the rod away the fish come and swim") {
  // FISH_init adds FISH_tickFishingWorld as a field object on the map's load: the water runs
  // before the player takes the rod out (the original measured 32 fish on mayo06 with the rod
  // away, the pool's frame count going on 20 a second, under the pause menu too).
  Fishing water = pond();
  const TopView view;
  ruleset::Random random{7};
  FishingInput input;
  input.hour = 10;
  (void)water.tick(input, view, random);
  CHECK_FALSE(water.fishing());
  CHECK(water.fish().size() == 1);
  for (int frame = 0; frame < 40; ++frame) {
    (void)water.tick(input, view, random);
  }
  CHECK(water.fish().size() == 32);
  CHECK_FALSE(water.cameraCentre().has_value());
  std::vector<glm::vec3> before;
  for (const Fish& fish : water.fish()) {
    before.push_back(fish.position);
  }
  for (int frame = 0; frame < 60; ++frame) {
    (void)water.tick(input, view, random);
  }
  std::size_t moved = 0;
  for (std::size_t i = 0; i < before.size() && i < water.fish().size(); ++i) {
    moved += water.fish()[i].position != before[i] ? 1U : 0U;
  }
  CHECK(moved > 0);
}

TEST_CASE("fishing water: the sea serpent comes at its hour with the rod away") {
  // The thin black lines on the water at 8 and 17 o'clock: the sea serpent's flat pieces seen
  // from the shore, as the original draws them.
  const TopView view;
  ruleset::Random random{11};
  FishingInput input;
  SUBCASE("at 8") {
    Fishing water = pond();
    input.hour = 8;
    for (int frame = 0; frame < 21; ++frame) {
      (void)water.tick(input, view, random);
    }
    CHECK(hasSeaSerpent(water));
  }
  SUBCASE("at 10") {
    Fishing water = pond();
    input.hour = 10;
    for (int frame = 0; frame < 200; ++frame) {
      (void)water.tick(input, view, random);
    }
    CHECK_FALSE(hasSeaSerpent(water));
  }
}

TEST_CASE("fishing water: switched off it neither moves nor rolls") {
  // setFishingDisabled: FISH_tickFishingWorld returns at once.
  Fishing water = pond();
  water.setDisabled(true);
  const TopView view;
  ruleset::Random random{3};
  const std::uint64_t state = random.state();
  FishingInput input;
  for (int frame = 0; frame < 10; ++frame) {
    (void)water.tick(input, view, random);
  }
  CHECK(water.fish().empty());
  CHECK(random.state() == state);
}
