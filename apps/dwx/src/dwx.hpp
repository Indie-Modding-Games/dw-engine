#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/sha1.hpp>

#include <filesystem>

namespace dwe::dwx {

// Exit codes, so scripts can tell a copy that is not recognised (or does not check out) from a command that could
// not run.
constexpr int ExitOk = 0;
constexpr int ExitUnknown = 1;
constexpr int ExitFailed = 2;

// Hashes a file in chunks, so a whole disc image never has to fit in memory.
core::Result<core::Sha1Digest> hashFile(const std::filesystem::path& path);

// dwx verify: hashes a file and says which known release it belongs to.
int verify(const std::filesystem::path& path);

// dwx extract --list: lists every file of a disc image and checks it against the executable's file table.
int listDisc(const std::filesystem::path& path, bool verifyImage);

} // namespace dwe::dwx
