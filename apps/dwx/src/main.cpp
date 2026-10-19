#include <dwe/formats/disc/known_releases.hpp>

#include <cstdio>
#include <format>
#include <fstream>
#include <print>
#include <span>
#include <string_view>
#include <vector>

#include "dwx.hpp"

namespace dwe::dwx {

namespace {

constexpr std::size_t ReadChunkSize = std::size_t{1} << 20U;

void printUsage() {
    std::println(stderr, "Usage:");
    std::println(stderr, "  dwx verify <file>");
    std::println(stderr, "      Hashes a disc image or an executable and says which known release it belongs to.");
    std::println(stderr, "  dwx extract --list [--verify-image] <image.cue|image.bin|image.iso>");
    std::println(stderr, "      Lists every file of the disc (by sector) with its size and SHA-1, identifies the");
    std::println(stderr, "      version by its executable and checks the executable's file table against the disc.");
    std::println(stderr, "      --verify-image also hashes the whole image file (slow).");
    std::println(stderr, "Exit codes: 0 known and consistent, 1 unknown or inconsistent, 2 error.");
}

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

int main(int argc, char** argv) {
    using namespace dwe::dwx;
    const std::span arguments{argv, static_cast<std::size_t>(argc)};
    const std::string_view command = arguments.size() > 1 ? arguments[1] : "";
    if (command == "verify" && arguments.size() == 3) {
        return verify(arguments[2]);
    }
    if (command == "extract") {
        bool list = false;
        bool verifyImage = false;
        std::vector<std::string_view> paths;
        for (const char* argument : arguments.subspan(2)) {
            const std::string_view option{argument};
            if (option == "--list") {
                list = true;
            } else if (option == "--verify-image") {
                verifyImage = true;
            } else {
                paths.push_back(option);
            }
        }
        // Only the listing exists so far; extracting files comes with the first extractors.
        if (list && paths.size() == 1) {
            return listDisc(std::filesystem::path{paths.front()}, verifyImage);
        }
    }
    printUsage();
    return ExitFailed;
}
