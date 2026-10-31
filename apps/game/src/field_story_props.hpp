#pragma once

#include <dwe/game/field/white_fade.hpp>
#include <dwe/scene/world.hpp>
#include <dwe/session/story_props.hpp>

#include <cstdint>
#include <optional>
#include <string>

#include "mood_bubble.hpp"
#include "screen_shake.hpp"

namespace dwe::assets {
struct DigimonTable;
} // namespace dwe::assets

namespace dwe::session {
class StoryScenes;
} // namespace dwe::session

namespace dwe::app {

class FieldScene;
class BossArrivalScene;

// The app's side of the story's props (session/story_props.hpp): it draws the Drill Tunnel's dirt
// cart in front of the partner (map_object.c: renderDirtCartModel), the dirt pile, the boulder and
// Trash Mountain's giant poop (renderDirtPile, renderBoulder, renderTrainingPoop) as models of the
// packs, shakes and reddens the screen for routine 34 (map.c: tickMeramonShake), fades the screen
// to white and back for routines 45 and 46 (fade.c: fadeToWhite, fadeFromWhite), hangs the shop
// rival's mood bubble over its character (script_common.c: renderMonochromonMoodBubble), moves a
// map sprite to a character's node (overworld.c: spawnSpriteAtEntity) and draws the Angemon
// shrine's pedestal (renderAngemonPedestal) and Toy Town's gearbox and boxes, opening the selected
// box's lid a step an original frame (renderGearbox, renderToyTownBoxes). Entering a map takes the
// cart, the pile, the boulder, the pedestal, the gearbox and the boxes away (map_object.c:
// unloadMapParts).
class FieldStoryProps {
public:
  // The scene must outlive this object.
  explicit FieldStoryProps(FieldScene& scene);

  // The species table, for where a character's mood bubble hangs (its condition point); without it
  // no bubble shows.
  void setSpecies(const assets::DigimonTable* species) { species_ = species; }
  // Where the final boss's charge goes (routine 40); without it the charge is not shown.
  void setBossArrival(BossArrivalScene* boss) { boss_ = boss; }

  // Takes the story's requests, once a frame, after a map change is seen.
  void update(session::StoryScenes& scenes);
  // Advances the shake, the boxes' lids and the fade to white by the frame's time, unless paused.
  void advance(double seconds);
  // Hands the scene this frame's models and shake.
  void show();
  // How much white the story's fade adds to the screen, from 0 to 1 (render::ScreenFade).
  [[nodiscard]] float whiteness() const { return whiteFade_.whiteness(); }

private:
  void take(const session::StoryPropRequest& request);
  void placeSprite(const session::SpriteAtCharacter& sprite);
  void startWhiteFade(const session::WhiteFadeStarted& fade);
  void takeMoodBubble(const session::MoodBubbleShown& bubble);
  // The entity of the character with this script id on the map (script_value.c:
  // scriptIdToEntityId): the player, the partner or an NPC.
  [[nodiscard]] std::optional<scene::Entity> characterEntity(std::int64_t character) const;
  void showMoodBubble();
  // The shake's red square over the screen (tickMeramonShake's POLY_FT4).
  void showShakeSquare();
  void clearMapParts();
  void advanceLids(double seconds);

  FieldScene* scene_;
  std::string mapId_;
  // What the story asked for on this map; nothing until it loads them.
  std::optional<std::int32_t> cartModel_;
  std::optional<std::int32_t> pileSize_;
  std::optional<session::BoulderShown> boulder_;
  std::optional<session::AngemonPedestalShown> pedestal_;
  bool gearbox_ = false;
  bool toyBoxes_ = false;
  // TOY_TOWN_SELECTED_BOX, BOX_LID_PROGRESS.big/.small and the time toward the lids' next frame.
  std::int32_t selectedBox_ = 0;
  std::int32_t bigLid_ = 0;
  std::int32_t smallLid_ = 0;
  double lidSeconds_ = 0.0;
  // The shake and the time toward its next frame.
  ScreenShake shake_;
  double shakeSeconds_ = 0.0;
  // The fade to white and the time toward its next frame.
  game::field::WhiteFade whiteFade_;
  double whiteSeconds_ = 0.0;
  // The mood bubble and the time toward its next frame.
  MoodBubble moodBubble_;
  double bubbleSeconds_ = 0.0;
  const assets::DigimonTable* species_ = nullptr;
  BossArrivalScene* boss_ = nullptr;
};

} // namespace dwe::app
