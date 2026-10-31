#pragma once

#include <cstdint>

namespace dwe::app {

// The shop rival's mood bubble (story routine 28: script_common.c: createMonochromonMoodBubble,
// renderMonochromonMoodBubble, MONOCHROMON_BUBBLE_TIMER): a 32 x 32 face of the map's image drawn
// over the character's condition point. It grows from a sixteenth of its size and rises 16 pixels
// over 30 frames, then stays until a routine 28 without a mood takes it away.
//
// Each routine 28 with a mood adds one more render object (world_object.c: addObject does not look
// for the one there), and each without takes one away: two in a row leave two objects, which count
// the shared timer down twice a frame, and the next removal leaves one. Measured in the original:
// the timer goes from 30 down by one each frame of 20 a second, and the sprite's scale and its
// offset upward are 16 - timer / 2 (scale 0x100 per pixel of offset, 0x1000 the full size).
class MoodBubble {
public:
  // createMonochromonMoodBubble: MONOCHROMON_BUBBLE_TIMER = 0x1e.
  static constexpr std::int32_t GrowFrames = 0x1E;
  // The offset of the grown bubble, in original pixels; also its scale's denominator.
  static constexpr std::int32_t FullOffset = 0x10;
  // The size of a face, in texels and original pixels (GsSPRITE w and h), and of the image row.
  static constexpr std::int32_t Side = 32;

  // A routine 28 with a mood, for a character that is on the map: the face, the timer from the top
  // and one more render object.
  void show(std::int64_t character, std::int32_t mood) {
    character_ = character;
    mood_ = mood;
    timer_ = GrowFrames;
    ++objects_;
  }
  // A routine 28 without: one render object goes (removeObject takes the first it finds).
  void remove() {
    if (objects_ > 0) {
      --objects_;
    }
  }
  // Everything goes (a map change: see FieldStoryProps).
  void clear() { objects_ = 0; }
  // One frame of the original: each render object whose character is on the map counts the timer
  // down after drawing (renderMonochromonMoodBubble returns early without the character).
  void frame(bool characterOnMap) {
    if (!characterOnMap) {
      return;
    }
    for (std::int32_t object = 0; object < objects_ && timer_ > 0; ++object) {
      --timer_;
    }
  }

  [[nodiscard]] bool shown() const { return objects_ > 0; }
  [[nodiscard]] std::int32_t objects() const { return objects_; }
  // The character it hangs over (variable 247) and its face (variable 248).
  [[nodiscard]] std::int64_t character() const { return character_; }
  [[nodiscard]] std::int32_t mood() const { return mood_; }
  // How far up the sprite is drawn, in original pixels, and its scale (offset / 16).
  [[nodiscard]] std::int32_t offset() const { return FullOffset - (timer_ / 2); }
  [[nodiscard]] float scale() const {
    return static_cast<float>(offset()) / static_cast<float>(FullOffset);
  }
  // The face's first texel column in the image (GsSPRITE u = mood * 32).
  [[nodiscard]] std::int32_t firstU() const { return mood_ * Side; }

private:
  std::int64_t character_ = 0;
  std::int32_t mood_ = 0;
  std::int32_t timer_ = 0;
  std::int32_t objects_ = 0;
};

} // namespace dwe::app
