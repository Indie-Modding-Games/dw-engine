#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/sha1.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace dwe::dwx {

// Exit codes, so scripts can tell a copy that is not recognised (or does not check out) from a
// command that could not run.
constexpr int ExitOk = 0;
constexpr int ExitUnknown = 1;
constexpr int ExitFailed = 2;
// dwx identify: a known release of the game that dwx extract cannot name the assets of (another
// region's).
constexpr int ExitUnsupported = 3;

// Hashes a file in chunks, so a whole disc image never has to fit in memory.
core::Result<core::Sha1Digest> hashFile(const std::filesystem::path& path);

// Writes a whole file, creating the folders it needs.
core::Result<void> writeFile(const std::filesystem::path& file, std::span<const std::byte> bytes);

// dwx verify: hashes a file and says which known release it belongs to.
int verify(const std::filesystem::path& path);

// Exports Chart metadata from an original executable without extracting the whole disc.
int extractUiChartDetail(const std::filesystem::path& executable,
                         const std::filesystem::path& output);
int extractUiChart(const std::filesystem::path& executable, const std::filesystem::path& output);

// dwx extract --list: lists every file of a disc image and checks it against the executable's file
// table.
int listDisc(const std::filesystem::path& path, bool verifyImage);

// What a disc image is, for the game's first start before it extracts one.
enum class DiscKind : std::uint8_t {
  // A release dwx extract knows the tables of.
  Supported,
  // A known release whose tables dwx does not know: another region's.
  OtherVersion,
  // No known release: another game, or no game at all.
  Unknown,
};

struct DiscIdentity {
  DiscKind kind = DiscKind::Unknown;
  // The release's version ("us", "eu"...), empty when unknown.
  std::string version;
};

// Reads the image's volume and boot executable and says which release it is. Fails when the image
// cannot be read (a missing file, a CUE sheet whose BIN is missing, something that is not a disc
// image).
core::Result<DiscIdentity> identifyDisc(const std::filesystem::path& path);

// dwx identify: prints "<kind> <version>" (supported us, other-version eu, unknown -) and exits
// with ExitOk, ExitUnsupported or ExitUnknown, or ExitFailed when the image cannot be read.
int identify(const std::filesystem::path& path);

// How far an extraction is: a percent from 0 to 100 and the step it is in (ExtractStep).
using ExtractProgress = std::function<void(int percent, std::string_view step)>;

// The steps of an extraction, as dwx extract --progress names them.
namespace ExtractStep {
inline constexpr std::string_view Read = "read";
inline constexpr std::string_view Extract = "extract";
inline constexpr std::string_view Write = "write";
inline constexpr std::string_view Check = "check";
inline constexpr std::string_view Upscale = "upscale";
inline constexpr std::string_view Done = "done";
} // namespace ExtractStep

// Writes the base pack and its coverage report from a disc image into a folder, then checks that
// the engine mounts it. The report goes to report and warnings to the standard error; progress,
// when given, is told each step and percent.
core::Result<void> extractBasePack(const std::filesystem::path& path,
                                   const std::filesystem::path& out, bool force, std::FILE* report,
                                   const ExtractProgress& progress = {});

// dwx extract: writes the base pack and its coverage report from a disc image into a folder, then
// checks that the engine mounts it. With progress, also prints "progress <percent> <step>" lines
// on the standard error as it goes, for the game's first start.
int extractDisc(const std::filesystem::path& path, const std::filesystem::path& out, bool force,
                bool progress = false);

// dwx cook: cooks the pack in one folder into another, in the forms the game loads.
int cook(const std::filesystem::path& pack, const std::filesystem::path& out);

// Whether `path` is `folder` or lies under it, comparing them as written: resolve both
// (weakly_canonical) first. cook and upscale use it to refuse an output folder that holds, or sits
// in, a pack they read.
bool isWithin(const std::filesystem::path& path, const std::filesystem::path& folder);

// dwx pack: writes the pack in a folder as a .pak archive and checks that it reads back.
int pack(const std::filesystem::path& folder, const std::filesystem::path& archive);

} // namespace dwe::dwx
