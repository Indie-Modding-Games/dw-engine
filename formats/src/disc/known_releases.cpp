#include <dwe/formats/disc/known_releases.hpp>

#include <array>
#include <string>

namespace dwe::formats::disc {

namespace {

// The executables' digests are the `sha1` of config/<version>/main.yaml in the decomp (dw_decomp), which checks its
// build against them. The digests of whole disc images (Redump's track hashes) are still missing: they must come from
// a verified dump or the Redump entry, never from a guess.
constexpr std::array KnownReleases = {
    KnownRelease{.sha1 = "31ec666b987d5b007dec40eecc4092d27ed9085b",
                 .version = "us",
                 .file = ReleaseFile::Executable,
                 .description = "US release, SLUS_010.32"},
    KnownRelease{.sha1 = "a805aad8bb6b2a12b145bd2172697aff7a28d25e",
                 .version = "eu",
                 .file = ReleaseFile::Executable,
                 .description = "EU release, SLES_029.14"},
    KnownRelease{.sha1 = "d11336b31aeceec48267062676e9c3f0833ec13f",
                 .version = "jp",
                 .file = ReleaseFile::Executable,
                 .description = "JP release, SLPS_017.97"},
    KnownRelease{.sha1 = "82212496037146885e96573bd43aa3f0f4ccee8e",
                 .version = "jp_rev1",
                 .file = ReleaseFile::Executable,
                 .description = "JP release, revision 1, SLPS_017.97"},
    KnownRelease{.sha1 = "5f8f4973c1420f061c6112868c77ca6a5dedc1ac",
                 .version = "jp_trial",
                 .file = ReleaseFile::Executable,
                 .description = "JP trial, SLPM_804.01"},
    KnownRelease{.sha1 = "2647af3160da17078cb015432fa1b553bc486cb4",
                 .version = "jp_bombom",
                 .file = ReleaseFile::Executable,
                 .description = "JP, the decomp's jp_bombom version, SLPM_804.02"},
};

} // namespace

std::span<const KnownRelease> knownReleases() {
    return KnownReleases;
}

std::optional<KnownRelease> findKnownRelease(const core::Sha1Digest& digest) {
    const std::string hex = core::toHex(digest);
    for (const auto& release : KnownReleases) {
        if (release.sha1 == hex) {
            return release;
        }
    }
    return std::nullopt;
}

std::string_view releaseFileName(ReleaseFile file) {
    switch (file) {
    case ReleaseFile::DiscImage:
        return "disc image";
    case ReleaseFile::Executable:
        return "executable";
    }
    return "unknown";
}

} // namespace dwe::formats::disc
