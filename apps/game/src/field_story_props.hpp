#pragma once

#include <dwe/session/story_props.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace dwe::session {
class StoryScenes;
} // namespace dwe::session

namespace dwe::app {

class FieldScene;

// The app's side of the story's props (session/story_props.hpp): it draws the Drill Tunnel's dirt
// cart in front of the partner (map_object.c: renderDirtCartModel), the dirt pile and the boulder
// (renderDirtPile, renderBoulder) as models of the packs, shakes the screen for routine 34
// (map.c: tickMeramonShake) and moves a map sprite to a character's node (overworld.c:
// spawnSpriteAtEntity). Entering a map takes the cart, the pile and the boulder away
// (map_object.c: unloadMapParts).
class FieldStoryProps {
public:
  // The scene must outlive this object.
  explicit FieldStoryProps(FieldScene& scene);

  // Takes the story's requests, once a frame, after a map change is seen.
  void update(session::StoryScenes& scenes);
  // Advances the shake by the frame's time, unless paused.
  void advance(double seconds);
  // Hands the scene this frame's models and shake.
  void show();

private:
  void take(const session::StoryPropRequest& request);
  void placeSprite(const session::SpriteAtCharacter& sprite);
  void clearMapParts();

  FieldScene* scene_;
  std::string mapId_;
  // What the story asked for on this map; nothing until it loads them.
  std::optional<std::int32_t> cartModel_;
  std::optional<std::int32_t> pileSize_;
  std::optional<session::BoulderShown> boulder_;
  // The shake's frame (MERAMON_SHAKE_FRAME_COUNT), its drawing offset in original pixels and the
  // time toward its next frame; no frame when it is not shaking.
  std::optional<std::int32_t> shakeFrame_;
  std::int32_t shakeOffset_ = 0;
  double shakeSeconds_ = 0.0;
};

} // namespace dwe::app
