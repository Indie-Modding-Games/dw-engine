#include <dwe/formats/disc/known_releases.hpp>

#include <cstdio>
#include <format>
#include <fstream>
#include <print>
#include <span>
#include <vector>

#include "dwx.hpp"

namespace dwe::dwx {

namespace {

constexpr std::size_t ReadChunkSize = std::size_t{1} << 20U;

} // namespace

core::Result<core::Sha1Digest> hashFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return core::fail(std::format("cannot open {}", path.string()));
    }
    core::Sha1 hasher;
    std::vector<char> chunk(ReadChunkSize);
    while (file) {
        file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto count = static_cast<std::size_t>(file.gcount());
        hasher.update(std::as_bytes(std::span{chunk}.first(count)));
    }
    if (!file.eof()) {
        return core::fail(std::format("cannot read {}", path.string()));
    }
    return hasher.finish();
}

int verify(const std::filesystem::path& path) {
    const auto digest = hashFile(path);
    if (!digest) {
        std::println(stderr, "dwx: {}", digest.error().message);
        return ExitFailed;
    }
    std::println("{}  {}", core::toHex(*digest), path.string());

    const auto release = formats::disc::findKnownRelease(*digest);
    if (!release) {
        std::println("Not a known release. Only the executables of each version are known for now; whole disc images "
                     "will be recognised once their digests are added.");
        return ExitUnknown;
    }
    std::println("Known {}: {} (version {})", formats::disc::releaseFileName(release->file), release->description,
                 release->version);
    return ExitOk;
}

} // namespace dwe::dwx
