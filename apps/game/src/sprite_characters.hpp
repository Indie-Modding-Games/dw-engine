#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::render {
class MapCamera;
class MapDepthLayers;
} // namespace dwe::render

namespace dwe::app {

// 3D characters (an engine improvement; docs/modulos/apps/game.md, «Personajes en 3D»): a few maps
// draw characters as prerendered sprites of their depth layers (overworld.c: renderMapOverlays),
// the opening and the ending around the elder's house, the farm, the elder's house and the market
// stall. With the setting on, the field leaves those sprites out and draws the species' model in
// their place with its idle clip. It changes no rule: the sprites keep the flags the scripts set,
// and a model shows exactly while its sprite would.
inline constexpr std::string_view ModelCharactersSetting = "model_characters";

// A character a map draws with sprites (field/sprite_characters.yaml of the game pack).
struct SpriteCharacter {
  std::string species;
  // The depth-layer instances that draw it; the first decides whether it shows.
  std::vector<std::size_t> sprites;
  // The instance of its shadow on the ground, which stays drawn under the model.
  std::optional<std::size_t> shadow;
  // The background pixel under its feet, when no shadow gives it.
  std::optional<glm::vec2> feet;
  // How far above the map's ground its feet stand, in world units (a planter, a bed).
  float height = 0.0F;
  // The original Y rotation it faces; facing the camera when left out.
  std::optional<std::int32_t> facing;
  // The ground point it faces, in the original's world units (x, z): the scene's centre its
  // drawing looks at (the opening's babies turn to the player waking up). A facing outweighs it.
  std::optional<glm::vec2> lookAt;
  // Draws the model added to what is behind, its colours scaled by this: a sprite drawn with the
  // additive semi-transparency (a glow).
  std::optional<float> additive;
};

struct SpriteCharacterTable {
  std::map<std::string, std::vector<SpriteCharacter>, std::less<>> maps;

  // The characters of a map, none for a map without them.
  [[nodiscard]] std::span<const SpriteCharacter> of(std::string_view mapId) const;
};

[[nodiscard]] core::Result<SpriteCharacterTable> parseSpriteCharacters(std::string_view yaml);

// Where a character stands in the world and the yaw it faces.
struct SpriteCharacterPlace {
  glm::vec3 feet{0.0F};
  float yaw = 0.0F;
};

// The background pixel under a character's feet: its own, the middle of its shadow, or the bottom
// middle of its first sprite; nothing when the map lacks the instance.
[[nodiscard]] std::optional<glm::vec2> feetPixel(const SpriteCharacter& character,
                                                 const render::MapDepthLayers& layers);

// The world point the map's camera sees at the character's feet pixel, at its height, and its yaw;
// nothing when the map lacks the instance or the camera does not see that ground.
[[nodiscard]] std::optional<SpriteCharacterPlace> placeOf(const SpriteCharacter& character,
                                                          const render::MapDepthLayers& layers,
                                                          const render::MapCamera& camera);

// Whether its sprites would be drawn: the scripts have not hidden the first.
[[nodiscard]] bool shows(const SpriteCharacter& character, const render::MapDepthLayers& layers);

// Leaves the characters' sprites out of the drawing (on) or draws them again (off). Their shadows
// stay.
void standInFor(std::span<const SpriteCharacter> characters, render::MapDepthLayers& layers,
                bool on);

} // namespace dwe::app
