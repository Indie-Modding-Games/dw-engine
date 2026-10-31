#include <cstdio>
#include <filesystem>
#include <optional>
#include <print>
#include <span>
#include <string_view>
#include <vector>

#include "disc_cache.hpp"
#include "dwx.hpp"
#include "story_conversion.hpp"
#include "upscale.hpp"
#include "validate.hpp"

namespace dwe::dwx {

namespace {

constexpr std::string_view DefaultOutFolder = "content/base";

void printUsage() {
  std::println(stderr, "Usage:");
  std::println(stderr, "  dwx ui-chart-detail <original-executable> <new-output.yaml>");
  std::println(stderr, "  dwx ui-chart <original-executable> <new-output.yaml>");
  std::println(stderr, "  dwx verify <file>");
  std::println(
      stderr,
      "      Hashes a disc image or an executable and says which known release it belongs to.");
  std::println(stderr, "  dwx identify <image.cue|image.bin|image.iso>");
  std::println(
      stderr,
      "      Says whether dwx extract supports the disc: prints \"supported <version>\" (exit 0),");
  std::println(
      stderr,
      "      \"other-version <version>\" for another region's release (exit 3) or \"unknown -\" (exit 1).");
  std::println(stderr, "  dwx extract --list [--verify-image] <image.cue|image.bin|image.iso>");
  std::println(
      stderr,
      "      Lists every file of the disc (by sector) with its size and SHA-1, identifies the");
  std::println(
      stderr,
      "      version by its executable and checks the executable's file table against the disc.");
  std::println(stderr, "      --verify-image also hashes the whole image file (slow).");
  std::println(
      stderr,
      "  dwx extract [--out <folder>] [--force] [--progress] <image.cue|image.bin|image.iso>");
  std::println(
      stderr,
      "      Writes the base pack (default content/base) and its coverage report, then checks that");
  std::println(
      stderr,
      "      the engine mounts it and opens every asset. A folder that dwx did not write is refused");
  std::println(stderr, "      unless --force, which writes into it without deleting anything.");
  std::println(
      stderr,
      "      --progress prints \"progress <percent> <step>\" lines on the standard error as it goes.");
  std::println(stderr, "  dwx extract --cache <folder> <image.cue|image.bin|image.iso>");
  std::println(
      stderr,
      "      Prints the content folder of the disc in a dev cache, extracting it there first when it is");
  std::println(
      stderr,
      "      not there yet. Entries are named by the image's SHA-1 and the extractor version; nothing");
  std::println(stderr,
               "      else goes to the standard output. The game uses it with --dev-assets.");
  std::println(stderr, "  dwx cook <pack folder> -o <folder>");
  std::println(
      stderr,
      "      Cooks a pack into the forms the game loads: PNG to KTX2 with mipmaps, binary glTF checked,");
  std::println(
      stderr,
      "      everything else copied. A folder from an earlier cook is updated: only what changed is");
  std::println(stderr, "      cooked again. A folder that dwx cook did not write is refused.");
  std::println(stderr, "  dwx convert-script <script/dg/<n>.yaml|script/maphead.yaml> -o <folder>");
  std::println(
      stderr,
      "      Converts a script listing, such as one a mod edited, to the Lua module that dwx extract");
  std::println(
      stderr,
      "      writes for it: <name>.lua, its line map <name>_lines.yaml and its texts <name>_texts.yaml.");
  std::println(stderr, "  dwx pack <pack folder> -o <file.pak>");
  std::println(stderr,
               "      Writes a pack as a single .pak archive and checks that it reads back.");
  std::println(stderr, "  dwx upscale <recipe.yaml> <pack folder|content folder> -o <folder>");
  std::println(
      stderr,
      "      Grows the indexed images a recipe selects with the built-in index_scale step (pixel-art");
  std::println(
      stderr,
      "      neighbour rules on palette indices, no network, the same bytes on every machine) and");
  std::println(
      stderr,
      "      writes them, with their metadata, as a remaster pack named after the recipe, such as");
  std::println(
      stderr,
      "      dwx upscale content/enhance/ui_original.recipe.yaml <content> -o <content>/ui_original");
  std::println(
      stderr,
      "  dwx validate [--schemas <folder>] [--with <folder>]... [--engine] <pack folder|content folder>");
  std::println(
      stderr,
      "      Checks a pack, or every pack of a folder, before the engine loads them: each YAML");
  std::println(
      stderr,
      "      document against its JSON Schema (default: the schemas folder next to the packs),");
  std::println(
      stderr,
      "      the assets they name, the models against their contracts and that every asset opens.");
  std::println(
      stderr,
      "      --with mounts the game's own packs at another folder too (checked with the rest, as the");
  std::println(
      stderr,
      "      game's), for packs that need them: dwx validate --with <content>/base <mod folder>");
  std::println(
      stderr,
      "      The packs are checked as the game loads a mod: only the mods and user layers, and none");
  std::println(
      stderr,
      "      of the base's rights. --engine checks them as the game's own packs instead, such as");
  std::println(stderr, "      dwx validate --engine --with <content>/base content/packs/game.");
  std::println(
      stderr,
      "Exit codes: 0 known and consistent (or written), 1 unknown or inconsistent, 2 error, 3 another "
      "region's release (identify).");
}

} // namespace

} // namespace dwe::dwx

int main(int argc, char** argv) {
  using namespace dwe::dwx;
  const std::span arguments{argv, static_cast<std::size_t>(argc)};
  const std::string_view command = arguments.size() > 1 ? arguments[1] : "";
  if (command == "ui-chart-detail" && arguments.size() == 4) {
    return extractUiChartDetail(arguments[2], arguments[3]);
  }
  if (command == "ui-chart" && arguments.size() == 4) {
    return extractUiChart(arguments[2], arguments[3]);
  }
  if (command == "verify" && arguments.size() == 3) {
    return verify(arguments[2]);
  }
  if (command == "identify" && arguments.size() == 3) {
    return identify(arguments[2]);
  }
  if (command == "extract") {
    bool list = false;
    bool verifyImage = false;
    bool force = false;
    bool progress = false;
    std::optional<std::filesystem::path> out;
    std::optional<std::filesystem::path> cache;
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
      } else if (option == "--progress") {
        progress = true;
      } else if (option == "--out" && i + 1 < options.size()) {
        out = options[++i];
      } else if (option == "--cache" && i + 1 < options.size()) {
        cache = options[++i];
      } else {
        paths.push_back(option);
      }
    }
    // --cache picks the folder itself and prints only it: it goes with no other option.
    const bool optionsAgree =
        (!cache || (!list && !verifyImage && !force && !out)) && (!progress || (!cache && !list));
    if (paths.size() == 1 && optionsAgree) {
      const std::filesystem::path image{paths.front()};
      if (cache) {
        return extractToCache(image, *cache);
      }
      return list ? listDisc(image, verifyImage)
                  : extractDisc(image, out.value_or(DefaultOutFolder), force, progress);
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
  if (command == "upscale" && arguments.size() == 6) {
    const std::string_view option{arguments[4]};
    if (option == "-o" || option == "--out") {
      return upscale(arguments[2], arguments[3], arguments[5]);
    }
  }
  if (command == "convert-script" && arguments.size() == 5) {
    const std::string_view option{arguments[3]};
    if (option == "-o" || option == "--out") {
      return convertScript(std::filesystem::path{arguments[2]},
                           std::filesystem::path{arguments[4]});
    }
  }
  if (command == "validate") {
    std::optional<std::filesystem::path> schemas;
    std::vector<std::filesystem::path> withFolders;
    std::vector<std::string_view> folders;
    dwe::assets::PackOrigin origin = dwe::assets::PackOrigin::Mod;
    const auto options = arguments.subspan(2);
    bool usable = true;
    for (std::size_t i = 0; i < options.size(); ++i) {
      const std::string_view option{options[i]};
      if (option == "--schemas") {
        usable = usable && i + 1 < options.size();
        if (usable) {
          schemas = options[++i];
        }
      } else if (option == "--engine") {
        origin = dwe::assets::PackOrigin::Engine;
      } else if (option == "--with") {
        usable = usable && i + 1 < options.size();
        if (usable) {
          withFolders.emplace_back(options[++i]);
        }
      } else {
        folders.push_back(option);
      }
    }
    if (usable && folders.size() == 1) {
      return validate(std::filesystem::path{folders.front()}, schemas, withFolders, origin);
    }
  }
  printUsage();
  return ExitFailed;
}
