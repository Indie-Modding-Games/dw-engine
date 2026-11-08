#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <span>
#include <vector>

#include "cook.hpp"

namespace dwe::dwx {

// The AV1 settings of the video cooker (docs/architecture/peliculas.md). Fixed, and one encoder
// thread, so the same Y4M always cooks to the same bytes; the cook runs its files in parallel.
// The constant quality level is where the re-encode adds no visible loss to the restored 10-bit
// source (a mean PSNR near 47 dB on the movies, the worst frame near 42 dB); the speed preset is
// libaom's fast end of its good-quality mode.
inline constexpr unsigned VideoQuality = 14;
inline constexpr int VideoSpeed = 6;
// A key frame at least every ten seconds of a 30 fps movie.
inline constexpr unsigned VideoKeyFrameDistance = 300;

// Cooks a YUV4MPEG2 video (8- or 10-bit 4:2:0, full range; dwx extract writes the movies in 10
// bits) into AV1 of the same depth in an IVF file with libaom, its matrix marked BT.601: one IVF
// frame per shown frame, the size and frame rate of the source.
// video::Av1Reader reads it back.
core::Result<std::vector<std::byte>> cookVideo(std::span<const std::byte> source,
                                               const CookOptions& options);

} // namespace dwe::dwx
