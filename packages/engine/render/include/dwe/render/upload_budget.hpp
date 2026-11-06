#pragma once

#include <cstddef>

namespace dwe::render {

// The share of a frame's upload memory queued texture copies leave free: the passes upload their
// per-frame data (the map's vertices, the models' matrices) after the copies, into the same memory.
inline constexpr std::size_t DrawUploadShare = 4;

// Whether a queued texture copy of `bytes` goes into this frame's upload memory, of which `used` of
// `perFrame` bytes are taken, keeping `reserve` free for the passes. Copies that do not go wait
// for the next frame. One that would not go even in an empty frame with the reserve kept goes as
// the frame's first copy, in what is left: alone, so the next frames are not starved of it.
[[nodiscard]] constexpr bool textureCopyFits(std::size_t used, std::size_t bytes,
                                             std::size_t perFrame, std::size_t reserve,
                                             bool firstCopy) {
  if (used + bytes + reserve <= perFrame) {
    return true;
  }
  return firstCopy && bytes + reserve > perFrame && used + bytes <= perFrame;
}

} // namespace dwe::render
