#include <cstdio>
#include <filesystem>
#include <print>
#include <span>
#include <string_view>
#include <vector>

#include "dwx.hpp"

namespace dwe::dwx {

namespace {

constexpr std::string_view DefaultOutFolder = "content/base";

void printUsage() {
    std::println(stderr, "Usage:");
    std::println(stderr, "  dwx verify <file>");
    std::println(stderr, "      Hashes a disc image or an executable and says which known release it belongs to.");
    std::println(stderr, "  dwx extract --list [--verify-image] <image.cue|image.bin|image.iso>");
    std::println(stderr, "      Lists every file of the disc (by sector) with its size and SHA-1, identifies the");
    std::println(stderr, "      version by its executable and checks the executable's file table against the disc.");
    std::println(stderr, "      --verify-image also hashes the whole image file (slow).");
    std::println(stderr, "  dwx extract [--out <folder>] [--force] <image.cue|image.bin|image.iso>");
    std::println(stderr, "      Writes the base pack (default content/base) and its coverage report, then checks that");
    std::println(stderr,
                 "      the engine mounts it. A folder that dwx did not write is refused unless --force, which");
    std::println(stderr, "      writes into it without deleting anything.");
    std::println(stderr, "Exit codes: 0 known and consistent (extract: written), 1 unknown or inconsistent, 2 error.");
}

} // namespace

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
        bool force = false;
        std::filesystem::path out = DefaultOutFolder;
        std::vector<std::string_view> paths;
        const auto options = arguments.subspan(2);
        for (std::size_t i = 0; i < options.size(); ++i) {
            const std::string_view option{options[i]};
            if (option == "--list") {
                list = true;
            } else if (option == "--verify-image") {
                verifyImage = true;
            } else if (option == "--force") {
                force = true;
            } else if (option == "--out" && i + 1 < options.size()) {
                out = options[++i];
            } else {
                paths.push_back(option);
            }
        }
        if (paths.size() == 1) {
            return list ? listDisc(std::filesystem::path{paths.front()}, verifyImage)
                        : extractDisc(std::filesystem::path{paths.front()}, out, force);
        }
    }
    printUsage();
    return ExitFailed;
}
