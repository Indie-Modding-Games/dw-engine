#include "sprite_characters.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/render/map_camera.hpp>
#include <dwe/render/map_layers.hpp>

#include <cmath>
#include <utility>

namespace dwe::app {

namespace {

constexpr std::string_view TableName = "field/sprite_characters";

std::size_t instanceOf(assets::YamlReader& reader, const core::YamlNode& node) {
  return reader.integer<std::size_t>(node);
}

SpriteCharacter readCharacter(assets::YamlReader& reader, const core::YamlNode& node) {
  SpriteCharacter character;
  character.species = reader.text(reader.field(node, "species"));
  for (const core::YamlNode& sprite : reader.sequence(reader.field(node, "sprites"), "sprites")) {
    character.sprites.push_back(instanceOf(reader, sprite));
  }
  if (character.sprites.empty()) {
    reader.fail(node, "a character has at least one sprite");
  }
  if (const core::YamlNode* shadow = node.find("shadow")) {
    character.shadow = instanceOf(reader, *shadow);
  }
  if (const core::YamlNode* feet = node.find("feet")) {
    const auto pixel = reader.sequence(*feet, "feet", 2);
    if (pixel.size() == 2) {
      character.feet = glm::vec2{reader.number(pixel[0]), reader.number(pixel[1])};
    }
  }
  if (const core::YamlNode* height = node.find("height")) {
    character.height = reader.number(*height);
  }
  if (const core::YamlNode* facing = node.find("facing")) {
    character.facing = reader.integer<std::int32_t>(*facing);
  }
  if (const core::YamlNode* lookAt = node.find("look_at")) {
    const auto point = reader.sequence(*lookAt, "look_at", 2);
    if (point.size() == 2) {
      character.lookAt = glm::vec2{reader.number(point[0]), reader.number(point[1])};
    }
  }
  if (const core::YamlNode* additive = node.find("additive")) {
    const float strength = reader.number(*additive);
    if (strength <= 0.0F || strength > 1.0F) {
      reader.fail(*additive, "additive is above 0 and at most 1");
    }
    character.additive = strength;
  }
  return character;
}

} // namespace

std::span<const SpriteCharacter> SpriteCharacterTable::of(std::string_view mapId) const {
  const auto found = maps.find(mapId);
  return found != maps.end() ? std::span<const SpriteCharacter>{found->second}
                             : std::span<const SpriteCharacter>{};
}

core::Result<SpriteCharacterTable> parseSpriteCharacters(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{tree.error()};
  }
  assets::YamlReader reader;
  SpriteCharacterTable table;
  if (reader.expectKeys(*tree, "the sprite character table", {"characters"})) {
    for (const core::YamlNode& node :
         reader.sequence(reader.field(*tree, "characters"), "characters")) {
      if (!reader.expectKeys(node, "a sprite character", {"maps", "species", "sprites"},
                             {"shadow", "feet", "height", "facing", "look_at", "additive"})) {
        break;
      }
      const SpriteCharacter character = readCharacter(reader, node);
      for (const core::YamlNode& map : reader.sequence(reader.field(node, "maps"), "maps")) {
        table.maps[reader.text(map)].push_back(character);
      }
    }
  }
  return reader.finish(std::move(table), TableName);
}

std::optional<glm::vec2> feetPixel(const SpriteCharacter& character,
                                   const render::MapDepthLayers& layers) {
  if (character.feet) {
    return character.feet;
  }
  if (character.shadow) {
    const auto position = layers.position(*character.shadow);
    const auto size = layers.spriteSize(*character.shadow);
    if (!position || !size) {
      return std::nullopt;
    }
    return *position + (*size * 0.5F);
  }
  const std::size_t first = character.sprites.front();
  const auto position = layers.position(first);
  const auto size = layers.spriteSize(first);
  if (!position || !size) {
    return std::nullopt;
  }
  return *position + glm::vec2{size->x * 0.5F, size->y};
}

std::optional<SpriteCharacterPlace> placeOf(const SpriteCharacter& character,
                                            const render::MapDepthLayers& layers,
                                            const render::MapCamera& camera) {
  const std::optional<glm::vec2> pixel = feetPixel(character, layers);
  if (!pixel) {
    return std::nullopt;
  }
  const std::optional<glm::vec3> feet =
      camera.pickGround(camera.backgroundToWindow(*pixel), character.height);
  if (!feet) {
    return std::nullopt;
  }
  if (character.facing) {
    return SpriteCharacterPlace{.feet = *feet, .yaw = assets::yawFromOriginal(*character.facing)};
  }
  if (character.lookAt) {
    glm::vec3 toPoint = assets::toWorldPosition(glm::ivec3{static_cast<int>(character.lookAt->x), 0,
                                                           static_cast<int>(character.lookAt->y)}) -
                        *feet;
    toPoint.y = 0.0F;
    if (glm::length(toPoint) > 1.0F) {
      return SpriteCharacterPlace{.feet = *feet, .yaw = assets::yawOfDirection(toPoint)};
    }
  }
  // The yaw whose forward (sin yaw, 0, cos yaw) points at the camera, as the sprites face it.
  const glm::vec3 toCamera = camera.desc().eye - *feet;
  return SpriteCharacterPlace{.feet = *feet, .yaw = std::atan2(toCamera.x, toCamera.z)};
}

bool shows(const SpriteCharacter& character, const render::MapDepthLayers& layers) {
  return !layers.hidden(character.sprites.front());
}

void standInFor(std::span<const SpriteCharacter> characters, render::MapDepthLayers& layers,
                bool on) {
  for (const SpriteCharacter& character : characters) {
    for (const std::size_t sprite : character.sprites) {
      layers.setStoodIn(sprite, on);
    }
  }
}

} // namespace dwe::app
