#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string_view>

#include "cook.hpp"

namespace dwe::dwx {

// The steps of dwx cook-pak, as its --progress lines name them.
namespace cook_pak_step {
inline constexpr std::string_view Cook = "cook";
inline constexpr std::string_view Pack = "pack";
inline constexpr std::string_view Check = "check";
inline constexpr std::string_view Done = "done";
} // namespace cook_pak_step

// How far a cook into an archive is: a percent of the whole job, from 0 to 100, and its step.
using CookPakProgress = std::function<void(int percent, std::string_view step)>;

// Where cookPakArchive cooks the pack before it packs it: a folder beside the archive. A cook that
// stops leaves it, and the next one takes it over. The caller keeps it out of any folder the game
// mounts, since it holds a pack with the same id as the archive.
[[nodiscard]] std::filesystem::path cookFolderOf(const std::filesystem::path& archive);
// Where cookPakArchive writes the archive before it is checked: it takes the archive's name only
// once it reads back and the engine opens every asset, so the archive is never half written.
[[nodiscard]] std::filesystem::path partialArchiveOf(const std::filesystem::path& archive);

// What a cook into an archive did.
struct CookPakSummary {
  CookSummary cook;
  std::size_t entries = 0;
  std::uint64_t archiveBytes = 0;
};

// Cooks the pack in a folder (cookPack, into cookFolderOf(archive)) and packs the cooked pack into
// a .pak archive (assets::writePak, into partialArchiveOf(archive)), then checks the archive: every
// entry reads back and matches its SHA-1, the manifest is the pack's, it has the source's asset
// ids, and the engine opens every asset of it (assets::checkContent). Only then does the archive
// take its name, replacing one there, and the cooked folder go. The source pack is never changed.
// Fails for an archive that does not end in .pak or lies in the pack; on a failure the archive
// already there is kept and the partial one removed.
core::Result<CookPakSummary> cookPakArchive(std::span<const Cooker> registry,
                                            const std::filesystem::path& pack,
                                            const std::filesystem::path& archive,
                                            const CookPakProgress& progress = {});

// dwx cook-pak: cookPakArchive with dwx's cookers; with progress, prints "progress <percent>
// <step>" lines (cook_pak_step) on the standard error, the percent being of the whole job.
int cookPak(const std::filesystem::path& pack, const std::filesystem::path& archive,
            bool progress = false);

} // namespace dwe::dwx
