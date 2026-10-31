#pragma once

#include <dwe/ruleset/random.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "field_effects.hpp"

namespace dwe::app {

// endi.c: NUM_ENDI_PARTICLES.
inline constexpr std::size_t EndingSparkCount = 80;
// The scale the original writes as a whole (4096ths).
inline constexpr std::int32_t EndingScaleOne = 0x1000;

// One spark of light (endi.c: EndiParticle): before the release it hangs on a bone of the player,
// `offset` in the bone's space; after it, `offset` is where it was in the world when released.
struct EndingSpark {
  std::int32_t bone = 0;
  OriginalPoint offset{0};
};

// A sound the scene asks for in slot 8 (the ending's bank, loaded by routine 52).
struct EndingSound {
  enum class Kind : std::uint8_t { Play, Stop };
  Kind kind = Kind::Play;
  std::int64_t sound = 0;
};

// Where a spark hanging on a bone is in the world: the bone, its offset already scaled once by the
// player's scale (the bone's matrix scales it again), and that scale (4096ths). Nothing when the
// bone is not drawn.
using EndingSparkPlacer = std::function<std::optional<OriginalPoint>(
    std::int32_t bone, OriginalPoint offset, std::int32_t scale)>;

// The player's last scene (endi.c, started by the player's ending tick, tamer.c: tamerTickEnding):
// the player turns into additive light and fades out over 60 frames while sparks gather on its
// bones, one a frame; it shrinks and swells, then the sparks rise together and slow down until they
// stop; the scene is then over and the story's sync bit goes up. A frame is the field's (1/20 s).
class EndingEffects {
public:
  // `boneCount` is the player's model's (DIGIMON_DATA[type].boneCount): bone 0 never gets a spark.
  EndingEffects(std::int32_t boneCount, std::uint64_t seed);

  // ENDI_tickEnding's first call: frame 0, the sparks cleared and following the bones, sound 0.
  void start();
  // ENDI_updateEnding: one frame. The placer is asked on the release's frame only.
  void tickFrame(const EndingSparkPlacer& place);

  // Whether the scene ran its course (frame -1: the player's ending tick then sets the sync bit).
  [[nodiscard]] bool over() const { return started_ && frame_ < 0; }
  [[nodiscard]] bool started() const { return started_; }
  [[nodiscard]] std::int32_t frame() const { return frame_; }
  [[nodiscard]] std::int32_t phase() const { return phase_; }
  // ENDI_fadeClut: how much of its colours the player keeps, from 1 to 0 over the first 60 frames.
  [[nodiscard]] float fade() const;
  // posData->scale, in 4096ths.
  [[nodiscard]] std::int32_t scale() const { return scale_; }
  // Whether the sparks left the bones (ENDI_DATA.flag == 0).
  [[nodiscard]] bool released() const { return released_; }
  // How far the released sparks rose (ENDI_DATA.base.vy's change), negative being up.
  [[nodiscard]] std::int32_t rise() const { return rise_; }
  [[nodiscard]] const std::vector<EndingSpark>& sparks() const { return sparks_; }

  // The sounds asked since the last call, in order.
  [[nodiscard]] std::vector<EndingSound> takeSounds() { return std::exchange(sounds_, {}); }

  // ENDI_renderParticles: each spark as a particle of a random grey-blue size and colour (rolled
  // every frame); a spark whose bone the placer cannot place is left out.
  [[nodiscard]] std::vector<FieldGlow> glows(const EndingSparkPlacer& place);

private:
  std::int32_t rand();
  void spawn();
  void release(const EndingSparkPlacer& place);

  ruleset::Random random_;
  std::int32_t boneCount_ = 1;
  bool started_ = false;
  std::int32_t frame_ = 0;
  std::int32_t phase_ = 0;
  std::int32_t scale_ = EndingScaleOne;
  std::int32_t velocity_ = 0;
  std::int32_t rise_ = 0;
  bool released_ = false;
  std::vector<EndingSpark> sparks_;
  std::vector<EndingSound> sounds_;
};

} // namespace dwe::app
