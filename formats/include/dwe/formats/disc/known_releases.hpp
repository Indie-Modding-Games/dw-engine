#pragma once

#include <dwe/core/sha1.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace dwe::formats::disc {

// What a known digest is the digest of.
enum class ReleaseFile : std::uint8_t { DiscImage, Executable };

// A file of a published release, identified by its SHA-1.
struct KnownRelease {
    std::string_view sha1;    // 40 lowercase hex digits
    std::string_view version; // the version id the decomp uses: us, eu, jp...
    ReleaseFile file;
    std::string_view description; // what the user has, in words

    bool operator==(const KnownRelease&) const = default;
};

// Returns every file the tools can recognise.
std::span<const KnownRelease> knownReleases();

// Returns the known file with this digest, if there is one.
std::optional<KnownRelease> findKnownRelease(const core::Sha1Digest& digest);

// Returns the name of a kind of file, as the tools print it.
std::string_view releaseFileName(ReleaseFile file);

} // namespace dwe::formats::disc
