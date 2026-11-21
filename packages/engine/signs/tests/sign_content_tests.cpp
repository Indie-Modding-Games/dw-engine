#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/signs/map_signs.hpp>
#include <dwe/signs/sign_painter.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

using namespace dwe::signs;

namespace {

const std::filesystem::path& packsFolder() {
  static const std::filesystem::path folder = std::filesystem::path{DWE_GAME_PACK}.parent_path();
  return folder;
}

std::vector<std::byte> fileBytes(const std::filesystem::path& path) {
  std::ifstream stream{path, std::ios::binary};
  REQUIRE_MESSAGE(stream.good(), path.string());
  const std::vector<char> bytes{std::istreambuf_iterator<char>{stream}, {}};
  std::vector<std::byte> file(bytes.size());
  std::ranges::transform(bytes, file.begin(), [](char c) { return static_cast<std::byte>(c); });
  return file;
}

std::vector<std::filesystem::path> shippedPacks() {
  std::vector<std::filesystem::path> packs{packsFolder() / "game"};
  for (const auto& entry : std::filesystem::directory_iterator{packsFolder()}) {
    if (entry.is_directory() && entry.path().filename().string().ends_with("-translation")) {
      packs.push_back(entry.path());
    }
  }
  return packs;
}

std::set<std::string> signMaps(std::span<const std::filesystem::path> packs) {
  std::set<std::string> maps;
  for (const std::filesystem::path& pack : packs) {
    const std::filesystem::path folder = pack / "signs";
    if (!std::filesystem::is_directory(folder)) {
      continue;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator{folder}) {
      if (entry.is_regular_file() && entry.path().extension() == ".yaml") {
        maps.insert(entry.path().stem().string());
      }
    }
  }
  return maps;
}

std::map<std::string, std::string> signTexts(const std::string& language) {
  const std::filesystem::path path =
      packsFolder() / "game" / "localization" / language / "signs.yaml";
  std::ifstream stream{path, std::ios::binary};
  REQUIRE_MESSAGE(stream.good(), path.string());
  const std::string yaml{std::istreambuf_iterator<char>{stream}, {}};
  auto document = dwe::core::parseYaml(yaml);
  REQUIRE_MESSAGE(document.has_value(), path.string());
  std::map<std::string, std::string> texts;
  for (const dwe::core::YamlEntry& entry : document->entries()) {
    texts.emplace("signs." + entry.key, entry.value.text());
  }
  return texts;
}

std::vector<std::string> languages() {
  std::vector<std::string> found;
  for (const auto& entry :
       std::filesystem::directory_iterator{packsFolder() / "game" / "localization"}) {
    if (std::filesystem::exists(entry.path() / "signs.yaml")) {
      found.push_back(entry.path().filename().string());
    }
  }
  std::ranges::sort(found);
  return found;
}

} // namespace

TEST_CASE("every shipped sign fits its box in every language, at no less than its floor size") {
  const std::vector<std::filesystem::path> packs = shippedPacks();
  dwe::assets::MemoryFileSystem memory;
  memory.addFile("shipped/pack.yaml", "id: shipped\nversion: 1.0.0\nlayer: base\n");
  for (const std::filesystem::path& pack : packs) {
    for (const char* folder : {"signs", "ui/fonts"}) {
      if (!std::filesystem::is_directory(pack / folder)) {
        continue;
      }
      for (const auto& entry : std::filesystem::recursive_directory_iterator{pack / folder}) {
        if (entry.is_regular_file()) {
          memory.addFile(("shipped" / entry.path().lexically_relative(pack)).generic_string(),
                         fileBytes(entry.path()));
        }
      }
    }
  }
  auto files =
      dwe::assets::VirtualFileSystem::mount(memory, std::vector<std::filesystem::path>{"shipped"});
  REQUIRE_MESSAGE(files.has_value(), files.error().message);
  MapSigns mapSigns{*files};
  const std::set<std::string> maps = signMaps(packs);
  const std::map<std::string, std::string> english = signTexts("en");

  std::size_t checked = 0;
  std::vector<std::string> problems;
  for (const std::string& language : languages()) {
    const std::map<std::string, std::string> texts = signTexts(language);
    const auto text = [&](std::string_view key) {
      if (const auto found = texts.find(std::string{key}); found != texts.end()) {
        return found->second;
      }
      problems.push_back(std::format("{}: no text {}", language, key));
      const auto fallback = english.find(std::string{key});
      return fallback == english.end() ? std::string{key} : fallback->second;
    };
    for (const std::string& map : maps) {
      const SignOverlay overlay = mapSigns.overlay(map, language);
      const std::vector<SignTextLayout> layouts = mapSigns.layouts(map, language, text);
      REQUIRE(layouts.size() == overlay.signs.size());
      for (std::size_t index = 0; index < layouts.size(); ++index) {
        const SignTextLayout& layout = layouts[index];
        const std::string where = std::format("{} {}/{}", language, map, overlay.signs[index].id);
        ++checked;
        if (layout.lines.empty()) {
          problems.push_back(
              std::format("{}: nothing laid out (font {}?)", where, overlay.signs[index].font));
          continue;
        }
        if (layout.overflows()) {
          problems.push_back(std::format("{}: ink {:.1f}x{:.1f} past room {:.1f}x{:.1f}", where,
                                         layout.ink.x, layout.ink.y, layout.room.x, layout.room.y));
        }
        if (layout.tooSmall()) {
          problems.push_back(std::format("{}: size {:.1f} under the floor {:.1f} (base {:.1f})",
                                         where, layout.size, layout.floorSize, layout.baseSize));
        }
        if (layout.missing > 0) {
          problems.push_back(std::format("{}: {} letters no font has", where, layout.missing));
        }
      }
    }
  }
  MESSAGE(std::format("{} signs checked in {} languages over {} maps", checked, languages().size(),
                      maps.size()));
  for (const std::string& problem : problems) {
    INFO(problem);
    CHECK(false);
  }
  CHECK(checked > 0);
}
