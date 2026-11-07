#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <span>
#include <vector>

#include "cook.hpp"

namespace dwe::dwx {

// The AV1 settings of the video cooker (docs/architecture/peliculas.md). Fixed, and one encoder
// thread, so the same Y4M always cooks to the same bytes; the cook runs its files in parallel.
// The constant quality level keeps the movies close to the source, whose own compression is
// coarser; the speed preset is libaom's fast end of its good-quality mode.
inline constexpr unsigned VideoQuality = 24;
inline constexpr int VideoSpeed = 6;
// A key frame at least every ten seconds of a 30 fps movie.
inline constexpr unsigned VideoKeyFrameDistance = 300;

// Cooks a YUV4MPEG2 video (8-bit 4:2:0, full range, as dwx extract writes the movies) into AV1 in
// an IVF file with libaom: one IVF frame per shown frame, the size and frame rate of the source.
// video::Av1Reader reads it back.
core::Result<std::vector<std::byte>> cookVideo(std::span<const std::byte> source,
                                               const CookOptions& options);

} // namespace dwe::dwx
