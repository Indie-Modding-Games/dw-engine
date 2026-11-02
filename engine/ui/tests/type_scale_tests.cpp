// The modern look's type scale (docs/architecture/ui.md, «Escala tipográfica»): its own texts take
// five sizes, the original's font whole multiples of its cell, and nothing is put in capitals by a
// style.

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <string_view>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

using fixture::Screen;

std::string readFile(const std::filesystem::path& path) {
  std::ifstream stream{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

} // namespace

TEST_CASE("the modern look's styles use only the sizes of its type scale") {
  // 14, 18, 22, 30 and 48dp, and 33dp for the original's font (three of its cells). The battle
  // keeps its own sizes for now.
  constexpr std::array<int, 6> Scale{14, 18, 22, 30, 33, 48};
  const std::filesystem::path modern = std::filesystem::path{DWE_GAME_PACK} / "ui/skins/modern";
  const std::regex size{R"(font-size:\s*([0-9]+)dp)"};
  int checked = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator{modern}) {
    if (entry.path().extension() != ".rcss" || entry.path().parent_path().filename() == "battle") {
      continue;
    }
    const std::string text = readFile(entry.path());
    CAPTURE(entry.path().lexically_relative(modern).string());
    for (auto match = std::sregex_iterator{text.begin(), text.end(), size};
         match != std::sregex_iterator{}; ++match) {
      const int dp = std::stoi((*match)[1].str());
      CAPTURE(dp);
      CHECK(std::ranges::contains(Scale, dp));
      ++checked;
    }
    CHECK(text.find("text-transform: uppercase") == std::string::npos);
  }
  CHECK(checked > 20);
}

TEST_CASE(
    "the original's list rows take the modern look's row size there, and keep 33dp in the original look") {
  for (const std::string_view document :
       {"card_shop", "item_keeper", "merit_shop", "discard_menu", "bird_transport", "jukebox"}) {
    CAPTURE(document);
    const std::string row = document == "item_keeper" ? "bag-row-0" : "row-0";
    for (const auto& [skin, expected] :
         {std::pair<std::string_view, std::string_view>{"modern", "22dp"}, {"original", "33dp"}}) {
      CAPTURE(skin);
      Screen screen{document};
      screen.skin(skin);
      screen.ui->show(document);
      screen.frames(2);
      CHECK(screen.ui->styleProperty(document, row, "font-size") == expected);
    }
  }
}

} // namespace dwe::ui
