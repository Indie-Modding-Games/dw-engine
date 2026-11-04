#pragma once

#include <dwe/core/math.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// The fishing's own screen work (fish.c): its camera, which scrolls the map's background in the
// original's screen pixels, the line drawn from the rod's tip to the bobber, and the sea
// serpent's body. Pure functions: the Fishing calls the camera's, the caller draws with the rest.
namespace dwe::game::fishing {

// The centres the view may take, corner to corner: FISH_screenLeft(0xa0) to
// FISH_screenRight(0xa0), FISH_screenTop(0x78) to FISH_screenBottom(0x78).
struct ViewRange {
  glm::ivec2 min{0};
  glm::ivec2 max{0};
};

// FISH_scrollCameraTo's clamp: a centre kept inside the range.
[[nodiscard]] glm::ivec2 clampToRange(glm::ivec2 centre, const ViewRange& range);

// FISH_panCameraTo: the view's next centre while it follows a screen point. Within `deadZone` of
// the centre on an axis the view stays on that axis; farther, it moves until the point is just
// inside it. A positive `radius` limits the step a frame to that many pixels (0 jumps at once).
// Nothing when the view stays where it is.
[[nodiscard]] std::optional<glm::ivec2> panCameraTo(glm::ivec2 centre, glm::ivec2 target,
                                                    const ViewRange& range, std::int32_t radius,
                                                    glm::ivec2 deadZone);

// FISH_panCameraWithDPad: the d-pad moves the view `step` pixels a frame on each axis it holds
// (direction: -1, 0 or 1 per axis, y down). Nothing when the view stays where it is.
[[nodiscard]] std::optional<glm::ivec2> panCameraWithDPad(glm::ivec2 centre, glm::ivec2 direction,
                                                          const ViewRange& range,
                                                          std::int32_t step);

// A point of the line on the screen: background pixels and its depth from the camera.
struct LinePoint {
  glm::vec2 pixel{0.0F};
  float depth = 0.0F;
};

// FISH_clipLineToRect: cuts a line to a rectangle, first across x then across y; false when it
// misses it. Only the pixels move, as the original's.
[[nodiscard]] bool clipLineToRect(std::array<glm::vec2, 2>& points, glm::vec2 min, glm::vec2 max);

// FISH_drawFishingLine: the line from the rod's tip to the bobber, cut to the visible rectangle and
// split into pieces of about LinePieceLength pixels, each sorted at the deeper of its two ends.
// None when an end is behind the camera or past its far end, or the line misses the rectangle.
inline constexpr float LinePieceLength = 64.0F;
inline constexpr float LineFarDepth = 65536.0F;
[[nodiscard]] std::vector<std::array<LinePoint, 2>> fishingLine(LinePoint from, LinePoint to,
                                                                glm::vec2 min, glm::vec2 max);

// FISH_updateTension: the line's colour by its tension, from blue (slack) through white (none)
// and yellow to red (about to snap); white with no tension shown (FISH_resetTension).
[[nodiscard]] glm::u8vec3 tensionColor(std::int32_t level, std::int32_t limit);

// The sea serpent's body (FISH_drawSeadramon): five pieces, each turned as the head was one more
// frame ago (FISH_pushSeadramonSegment) and hung this far behind the one before it.
inline constexpr std::size_t SeaSerpentPieces = 5;
inline constexpr std::array<float, SeaSerpentPieces - 1> SeaSerpentPieceGaps{200.0F, 175.0F, 125.0F,
                                                                             140.0F};

struct SeaSerpentPiece {
  glm::vec3 position{0.0F};
  // Radians about +Y, as Fish::yaw.
  float yaw = 0.0F;
};

// FISH_pushSeadramonSegment: the newest turn first, the oldest dropped.
void pushSeaSerpentTurn(std::array<float, SeaSerpentPieces>& turns, float yaw);

// Where each piece is drawn: the first at the head, each next one behind it along its own turn.
[[nodiscard]] std::array<SeaSerpentPiece, SeaSerpentPieces>
seaSerpentBody(glm::vec3 head, const std::array<float, SeaSerpentPieces>& turns);

// FISH_bobberCommand's animation of the bobber: hanging from the rod, sinking into the water,
// floating (and bobbing now and then), dipping at a nibble or a bite, under while the fish holds it
// and coming back up when the hook is lost.
class BobberAnimation {
public:
  enum class Command : std::uint8_t { ToHand, Sink, Nibble, Bite, Lost };

  // FISH_resetBobberAnim: hidden at rest.
  void reset() { *this = {}; }
  void command(Command command);
  // Command 0, once a frame. Floating, the caller rolls whether the bobber bobs this frame.
  void tick(bool bobs);
  [[nodiscard]] bool floating() const { return state_ == State::Floating; }
  // The sprite's frame, 1 to 7; hidden at 0 or below.
  [[nodiscard]] std::int32_t frame() const { return frame_; }

private:
  // The original's states 3, 4, 7 to 11, 13 and 14 (its 5, 6 and 12 are never entered).
  enum class State : std::uint8_t {
    Rest,
    Hanging,
    Sinking,
    Floating,
    Bobbing,
    Nibble,
    Bite,
    Under,
    Lost
  };
  void toFloating();

  State state_ = State::Rest;
  std::int32_t frame_ = 0;
  std::int32_t timer_ = 0;
};

} // namespace dwe::game::fishing
