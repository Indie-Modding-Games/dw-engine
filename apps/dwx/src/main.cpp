#include <dwe/core/error.hpp>
#include <dwe/core/sha1.hpp>
#include <dwe/formats/disc/known_releases.hpp>

#include <cstdio>
#include <format>
#include <fstream>
#include <print>
#include <span>
#include <string_view>
#include <vector>

namespace {

using namespace dwe;

// Exit codes, so scripts can tell a copy that is not recognised from a command that could not run.
constexpr int ExitKnown = 0;
constexpr int ExitUnknown = 1;
constexpr int ExitFailed = 2;

constexpr std::size_t ReadChunkSize = std::size_t{1} << 20U;

void printUsage() {
    std::println(stderr, "Usage: dwx verify <file>");
    std::println(stderr, "  Hashes a disc image or an executable and says which known release it belongs to.");
}

// Hashes a file in chunks, so a whole disc image never has to fit in memory.
core::Result<core::Sha1Digest> hashFile(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return core::fail(std::format("cannot open {}", path));
    }
    core::Sha1 hasher;
    std::vector<char> chunk(ReadChunkSize);
    while (file) {
        file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto count = static_cast<std::size_t>(file.gcount());
        hasher.update(std::as_bytes(std::span{chunk}.first(count)));
    }
    if (!file.eof()) {
        return core::fail(std::format("cannot read {}", path));
    }
    return hasher.finish();
}

int verify(const char* path) {
    const auto digest = hashFile(path);
    if (!digest) {
        std::println(stderr, "dwx: {}", digest.error().message);
        return ExitFailed;
    }
    std::println("{}  {}", core::toHex(*digest), path);

    const auto release = formats::disc::findKnownRelease(*digest);
    if (!release) {
        std::println("Not a known release. Only the executables of each version are known for now; whole disc images "
                     "will be recognised once their digests are added.");
        return ExitUnknown;
    }
    std::println("Known {}: {} (version {})", formats::disc::releaseFileName(release->file), release->description,
                 release->version);
    return ExitKnown;
}

} // namespace

int main(int argc, char** argv) {
    const std::span arguments{argv, static_cast<std::size_t>(argc)};
    if (arguments.size() == 3 && std::string_view{arguments[1]} == "verify") {
        return verify(arguments[2]);
    }
    printUsage();
    return ExitFailed;
}
