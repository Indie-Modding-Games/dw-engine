#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

// What the story's routines and orders put on the field the app draws besides the map and its
// characters: the Drill Tunnel's dirt cart, dirt pile and boulder (map_object.c), Meramon's shake
// (map.c: createMeramonShake) and a map sprite moved to a character (overworld.c:
// spawnSpriteAtEntity). The story keeps the state the original keeps for them; each request
// carries what the app needs to draw, so the app only draws. The cart, the pile and the boulder go
// away when the field loads a map, as the original's unloadMapParts removes them.
namespace dwe::session {

// The dirt pile's size counter starts at 15 when the game boots and never goes back
// (map_object.c: DIRT_PILE_SIZE_COUNTER): neither a map nor a loaded game resets it.
inline constexpr std::int32_t DirtPileFullSize = 15;
// The pile is drawn while its size is this or more (map_object.c: renderDirtPile).
inline constexpr std::int32_t DirtPileSmallestShown = 6;
// The cart's model when none shows (map_object.c: loadDirtCartModel sets 2).
inline constexpr std::int32_t DirtCartHidden = 2;

// Routines 26 and 19 (loadDirtCartModel, setDirtCartModel): the cart in front of the partner
// shows model 0 (ETCNA\DOSYA, the cart full of dirt) or 1 (ETCNA\HAKO, the empty box);
// DirtCartHidden or any other number shows none (renderDirtCartModel draws only 0 and 1).
struct DirtCartShown {
  std::int32_t model = DirtCartHidden;
};

// Routines 27 and 20 (loadDirtPileModel, decreaseDirtPileSize): the pile on tunn05 at
// (2037, 0, 849), scaled by size / DirtPileFullSize, drawn while the size is at least
// DirtPileSmallestShown.
struct DirtPileShown {
  std::int32_t size = DirtPileFullSize;
};

// spawn_boulder, move_boulder and despawn_boulder (map_object.c: spawnBoulder, moveBoulder;
// removeObject 0xFB6): the boulder ETCNA\IWA where the original's axes put it (y down), turned
// about its x axis by `pitch` (4096 a turn); `shown` false takes it away.
struct BoulderShown {
  bool shown = false;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;
  std::int32_t pitch = 0;
};

// Routine 34 (createMeramonShake): the screen shakes for its frames.
struct ScreenShakeStarted {};

// spawn_sprite_at_entity (overworld.c: spawnSpriteAtEntity): the map's sprite `object` is moved,
// centred, to where node `node` of the character with script id `character` shows.
struct SpriteAtCharacter {
  std::int64_t character = 0;
  std::int32_t node = 0;
  std::int32_t object = 0;
};

using StoryPropRequest =
    std::variant<DirtCartShown, DirtPileShown, BoulderShown, ScreenShakeStarted, SpriteAtCharacter>;

// Trash Mountain's giant poop: map.c: loadMap calls initializeTrainingPoop on map 0xa5 (gomi02) and
// on no other, whatever the story, which puts ETCNA\TRY at TRAINING_POOP_TRANSLATION, unturned and
// at its size (map_object.c); renderTrainingPoop draws it while the map layer is. The place is in
// the original's axes (y down).
struct TrainingPoop {
  std::int32_t x = 1922;
  std::int32_t y = 0;
  std::int32_t z = 503;
};

// The poop on the map `mapId`, if that map has it.
[[nodiscard]] constexpr std::optional<TrainingPoop> trainingPoopOn(std::string_view mapId) {
  return mapId == "gomi02" ? std::optional{TrainingPoop{}} : std::nullopt;
}

} // namespace dwe::session
