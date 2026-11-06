// 3D characters (docs/modulos/apps/game.md, «Personajes en 3D»): the table of the characters the
// maps draw as sprites, where their models stand, and which sprites the models replace.

#include <dwe/assets/map_document.hpp>
#include <dwe/render/map_camera.hpp>
#include <dwe/render/map_layers.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sprite_characters.hpp"
#include "user_settings.hpp"

namespace dwe::app {
namespace {

using assets::MapObjectInstance;

// The opening map's camera (topn01): the ground seen from above and in front.
const render::OriginalMapCamera OpeningCamera{
    .viewpoint = {0, -4301, -5973}, .reference = {0, 0, -256}, .projection = 1813, .tiles = {6, 5}};

assets::MapSprite sprite(std::int32_t width, std::int32_t height) {
  return {.texX = 0,
          .texY = 0,
          .width = width,
          .height = height,
          .position = {0, 0, -2000},
          .clut = 16,
          .transparency = 4};
}

// A character drawn by two sprite instances that take turns, standing over its shadow, and a
// character with no shadow; the shadow's sprite is 32 x 24.
assets::MapObjects openingObjects() {
  assets::MapObjects objects;
  objects.sprites = {sprite(48, 56), sprite(32, 24)};
  objects.instances = {
      {.frames = {{.sprite = 0, .time = 2},
                  {.sprite = assets::MapSpriteFrame::BlankSprite, .time = 2}},
       .x = 465,
       .y = 492,
       .flag = 0},
      {.frames = {{.sprite = assets::MapSpriteFrame::BlankSprite, .time = 2},
                  {.sprite = 0, .time = 2}},
       .x = 465,
       .y = 492,
       .flag = 0},
      {.frames = {{.sprite = 1, .time = 1}}, .x = 470, .y = 520, .flag = 0},
      {.frames = {{.sprite = 0, .time = 1}},
       .x = 300,
       .y = 300,
       .flag = MapObjectInstance::HiddenFlag},
  };
  return objects;
}

SpriteCharacter characterOf(std::string species, std::vector<std::size_t> sprites,
                            std::optional<std::size_t> shadow = std::nullopt) {
  SpriteCharacter character;
  character.species = std::move(species);
  character.sprites = std::move(sprites);
  character.shadow = shadow;
  return character;
}

std::string readPackFile(std::string_view path) {
  std::ifstream stream{std::filesystem::path{DWE_GAME_PACK} / path, std::ios::binary};
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

} // namespace

TEST_CASE("the sprite character table gives each listed map the character") {
  const auto table = parseSpriteCharacters(R"(characters:
  - {maps: [tend01, tend02], species: leom, sprites: [3, 4], shadow: 5, additive: 0.5}
  - {maps: [room08], species: puni, sprites: [7], feet: [528, 179], height: 90, facing: 1024}
)");
  REQUIRE(table);
  REQUIRE(table->of("tend01").size() == 1);
  REQUIRE(table->of("tend02").size() == 1);
  CHECK(table->of("tend02")[0].species == "leom");
  CHECK(table->of("tend02")[0].sprites == std::vector<std::size_t>{3, 4});
  CHECK(table->of("tend02")[0].shadow == std::optional<std::size_t>{5});
  CHECK(table->of("tend02")[0].additive == std::optional<float>{0.5F});
  CHECK(table->of("twna01").empty());
  const SpriteCharacter& punimon = table->of("room08")[0];
  CHECK(punimon.feet == std::optional<glm::vec2>{glm::vec2{528.0F, 179.0F}});
  CHECK(punimon.height == doctest::Approx(90.0F));
  CHECK(punimon.facing == std::optional<std::int32_t>{1024});
  CHECK_FALSE(punimon.shadow);
  CHECK_FALSE(punimon.additive);
}

TEST_CASE("a sprite character needs its maps, species and sprites, takes no other key and glows "
          "within its colours") {
  CHECK_FALSE(parseSpriteCharacters("characters:\n  - {maps: [topn01], species: tane}\n"));
  CHECK_FALSE(
      parseSpriteCharacters("characters:\n  - {maps: [topn01], species: tane, sprites: []}\n"));
  CHECK_FALSE(parseSpriteCharacters(
      "characters:\n  - {maps: [topn01], species: tane, sprites: [0], scale: 2}\n"));
  CHECK_FALSE(parseSpriteCharacters("maps: {}\n"));
  // A glow is above nothing and at most the colours themselves.
  CHECK_FALSE(parseSpriteCharacters(
      "characters:\n  - {maps: [tend01], species: poyo, sprites: [12], additive: 0}\n"));
  CHECK_FALSE(parseSpriteCharacters(
      "characters:\n  - {maps: [tend01], species: poyo, sprites: [12], additive: 1.5}\n"));
}

TEST_CASE("the game pack's sprite characters read, each with a species and its sprites") {
  const auto table = parseSpriteCharacters(readPackFile("field/sprite_characters.yaml"));
  REQUIRE(table);
  // The opening's six babies, the ending's nine, the farm's two, the elder's house's two.
  CHECK(table->of("topn01").size() == 6);
  CHECK(table->of("tend01").size() == 9);
  CHECK(table->of("tend02").size() == 9);
  CHECK(table->of("twna02").size() == 2);
  CHECK(table->of("room08").size() == 2);
  for (const auto& [map, characters] : table->maps) {
    for (const SpriteCharacter& character : characters) {
      CHECK_FALSE(character.species.empty());
      CHECK_FALSE(character.sprites.empty());
      if (character.shadow) {
        CHECK_FALSE(std::ranges::contains(character.sprites, *character.shadow));
      }
    }
  }
}

TEST_CASE("a character's feet are its own pixel, its shadow's middle or its sprite's bottom") {
  const render::MapCamera camera{render::mapCameraDesc(OpeningCamera)};
  const render::MapDepthLayers layers{openingObjects(), camera};
  SpriteCharacter character = characterOf("tane", {0, 1}, 2);
  CHECK(feetPixel(character, layers) == std::optional<glm::vec2>{glm::vec2{486.0F, 532.0F}});
  character.shadow.reset();
  CHECK(feetPixel(character, layers) == std::optional<glm::vec2>{glm::vec2{489.0F, 548.0F}});
  character.feet = glm::vec2{100.0F, 200.0F};
  CHECK(feetPixel(character, layers) == std::optional<glm::vec2>{glm::vec2{100.0F, 200.0F}});
  const SpriteCharacter missing = characterOf("tane", {40});
  CHECK_FALSE(feetPixel(missing, layers));
}

TEST_CASE("a character's model stands on the ground the camera sees at its feet, facing the "
          "camera") {
  const render::MapCamera camera{render::mapCameraDesc(OpeningCamera)};
  const render::MapDepthLayers layers{openingObjects(), camera};
  SpriteCharacter character = characterOf("tane", {0, 1}, 2);
  const auto place = placeOf(character, layers, camera);
  REQUIRE(place);
  CHECK(place->feet.y == doctest::Approx(0.0F));
  const auto seen = camera.projectToBackground(place->feet);
  REQUIRE(seen);
  CHECK(seen->pixel.x == doctest::Approx(486.0F).epsilon(0.001));
  CHECK(seen->pixel.y == doctest::Approx(532.0F).epsilon(0.001));
  // Its forward points at the camera, seen from above.
  const glm::vec3 forward = assets::forwardOfYaw(place->yaw);
  const glm::vec3 toCamera = camera.desc().eye - place->feet;
  const glm::vec2 flatToCamera = glm::normalize(glm::vec2{toCamera.x, toCamera.z});
  CHECK(forward.x == doctest::Approx(flatToCamera.x).epsilon(0.001));
  CHECK(forward.z == doctest::Approx(flatToCamera.y).epsilon(0.001));

  // Raised, it stands at its height on the same pixel; a facing turns it as the original's
  // rotation.
  character.height = 60.0F;
  character.facing = 1024;
  const auto raised = placeOf(character, layers, camera);
  REQUIRE(raised);
  CHECK(raised->feet.y == doctest::Approx(60.0F));
  const auto raisedSeen = camera.projectToBackground(raised->feet);
  REQUIRE(raisedSeen);
  CHECK(raisedSeen->pixel.x == doctest::Approx(486.0F).epsilon(0.001));
  CHECK(raisedSeen->pixel.y == doctest::Approx(532.0F).epsilon(0.001));
  CHECK(raised->yaw == doctest::Approx(assets::yawFromOriginal(1024)));
}

TEST_CASE("the models stand in for the sprites but not the shadows, and only while the scripts "
          "show the sprites") {
  const render::MapCamera camera{render::mapCameraDesc(OpeningCamera)};
  render::MapDepthLayers layers{openingObjects(), camera};
  const SpriteCharacter shown = characterOf("tane", {0, 1}, 2);
  const SpriteCharacter hidden = characterOf("puni", {3});
  const std::vector<SpriteCharacter> characters{shown, hidden};
  const auto drawn = [&layers] {
    std::vector<std::size_t> instances;
    for (const render::DepthLayerQuad& quad : layers.quads()) {
      instances.push_back(quad.instance);
    }
    std::ranges::sort(instances);
    return instances;
  };
  REQUIRE(drawn() == std::vector<std::size_t>{0, 2});
  CHECK(shows(shown, layers));
  CHECK_FALSE(shows(hidden, layers));

  standInFor(characters, layers, true);
  CHECK(drawn() == std::vector<std::size_t>{2});
  // A script shows the second character: its model shows, its sprite stays out.
  layers.setHidden(3, false);
  CHECK(shows(hidden, layers));
  CHECK(drawn() == std::vector<std::size_t>{2});
  // A script hides the first: its model goes with its sprite.
  layers.setHidden(0, true);
  CHECK_FALSE(shows(shown, layers));

  standInFor(characters, layers, false);
  CHECK(drawn() == std::vector<std::size_t>{2, 3});
}

TEST_CASE("3D characters are on until the player turns them off, and the choice is saved") {
  const UserSettings fresh;
  CHECK(fresh.modelCharacters.value_or(true));
  const auto read = parseUserSettings("model_characters: false\n");
  REQUIRE(read);
  CHECK(read->modelCharacters == std::optional<bool>{false});
}

} // namespace dwe::app
