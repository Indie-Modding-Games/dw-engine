#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/sha1.hpp>

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <span>

namespace dwe::dwx {

// Exit codes, so scripts can tell a copy that is not recognised (or does not check out) from a command that could
// not run.
constexpr int ExitOk = 0;
constexpr int ExitUnknown = 1;
constexpr int ExitFailed = 2;

// Hashes a file in chunks, so a whole disc image never has to fit in memory.
core::Result<core::Sha1Digest> hashFile(const std::filesystem::path& path);

// Writes a whole file, creating the folders it needs.
core::Result<void> writeFile(const std::filesystem::path& file, std::span<const std::byte> bytes);

// dwx verify: hashes a file and says which known release it belongs to.
int verify(const std::filesystem::path& path);

// dwx extract --list: lists every file of a disc image and checks it against the executable's file table.
int listDisc(const std::filesystem::path& path, bool verifyImage);

// Writes the base pack and its coverage report from a disc image into a folder, then checks that the engine mounts it.
// Progress goes to report and warnings to the standard error.
core::Result<void> extractBasePack(const std::filesystem::path& path, const std::filesystem::path& out, bool force,
                                   std::FILE* report);

// dwx extract: writes the base pack and its coverage report from a disc image into a folder, then checks that the
// engine mounts it.
int extractDisc(const std::filesystem::path& path, const std::filesystem::path& out, bool force);

// dwx cook: cooks the pack in one folder into another, in the forms the game loads.
int cook(const std::filesystem::path& pack, const std::filesystem::path& out);

// dwx pack: writes the pack in a folder as a .pak archive and checks that it reads back.
int pack(const std::filesystem::path& folder, const std::filesystem::path& archive);

} // namespace dwe::dwx
