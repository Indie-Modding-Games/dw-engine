#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>

namespace dwe::dwx {

// The file of an export that names its maps: one "<map id>\t<name>" line per map, the name in the
// language asked for when the packs translate it, else the table's own, else empty.
inline constexpr std::string_view ExportedMapsFile = "maps.tsv";

// What dwx export-images wrote.
struct ExportSummary {
  std::size_t backgrounds = 0;
  std::size_t images = 0;
  std::size_t maps = 0;
};

// Writes the images a player can replace (docs/guides/imagenes-personalizadas.md), read from the
// packs in `folders`, into `out` with the names a custom image pack uses: every map background as
// map/<map>/background_<variant>.png, and every image of texture/ once per palette as
// texture/.../<image>_palette<N>.png in full colour (palette 0 for an image without palettes).
// Each is the base pack's, at its size. Also writes ExportedMapsFile with names in `language`.
// A folder at `out` is only replaced when it is an export.
core::Result<ExportSummary> exportImages(std::span<const std::filesystem::path> folders,
                                         const std::filesystem::path& out,
                                         const std::string& language);

// dwx export-images: exportImages, then prints what it wrote.
int exportImagesCommand(std::span<const std::filesystem::path> folders,
                        const std::filesystem::path& out, const std::string& language);

} // namespace dwe::dwx
