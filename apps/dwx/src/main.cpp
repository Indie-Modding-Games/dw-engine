#include <cstdio>
#include <print>
#include <span>
#include <string_view>
#include <vector>

#include "dwx.hpp"

namespace dwe::dwx {

namespace {

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
