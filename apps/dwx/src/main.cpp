#include <cstdio>
#include <filesystem>
#include <optional>
#include <print>
#include <span>
#include <string_view>
#include <vector>

#include "dwx.hpp"
#include "validate.hpp"

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
    std::println(stderr, "  dwx cook <pack folder> -o <folder>");
    std::println(stderr,
                 "      Cooks a pack into the forms the game loads: PNG to KTX2 with mipmaps, binary glTF checked,");
    std::println(stderr,
                 "      everything else copied. A folder from an earlier cook is updated: only what changed is");
    std::println(stderr, "      cooked again. A folder that dwx cook did not write is refused.");
    std::println(stderr, "  dwx pack <pack folder> -o <file.pak>");
    std::println(stderr, "      Writes a pack as a single .pak archive and checks that it reads back.");
    std::println(stderr, "  dwx validate [--schemas <folder>] <pack folder|content folder>");
    std::println(stderr, "      Checks a pack, or every pack of a folder, before the engine loads them: each YAML");
    std::println(stderr, "      document against its JSON Schema (default: the schemas folder next to the packs),");
    std::println(stderr, "      the assets they name, the models against their contracts and that every asset opens.");
    std::println(stderr, "Exit codes: 0 known and consistent (or written), 1 unknown or inconsistent, 2 error.");
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
    if ((command == "cook" || command == "pack") && arguments.size() == 5) {
        const std::string_view option{arguments[3]};
        if (option == "-o" || option == "--out") {
            const std::filesystem::path folder{arguments[2]};
            const std::filesystem::path out{arguments[4]};
            return command == "cook" ? cook(folder, out) : pack(folder, out);
        }
    }
    if (command == "validate") {
        std::optional<std::filesystem::path> schemas;
        std::vector<std::string_view> folders;
        const auto options = arguments.subspan(2);
        for (std::size_t i = 0; i < options.size(); ++i) {
            const std::string_view option{options[i]};
            if (option == "--schemas" && i + 1 < options.size()) {
                schemas = options[++i];
            } else {
                folders.push_back(option);
            }
        }
        if (folders.size() == 1) {
            return validate(std::filesystem::path{folders.front()}, schemas);
        }
    }
    printUsage();
    return ExitFailed;
}
