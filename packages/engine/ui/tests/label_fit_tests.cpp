#include <dwe/ui/minimap.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

using fixture::Screen;

constexpr std::array<std::string_view, 4> Skins{"original", "modern", "pixel", "devvago"};
constexpr std::array<std::string_view, 9> Languages{"en", "es", "pt", "fr", "de",
                                                    "ru", "zh", "ko", "ja"};

std::filesystem::path translationTexts(std::string_view language) {
  return std::filesystem::path{DWE_GAME_PACK}.parent_path() /
         std::format("{}-translation", language) / "text" / language;
}

std::string fileText(const std::filesystem::path& path) {
  std::ifstream stream{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

std::unique_ptr<Screen> screenIn(std::string_view document, std::string_view language) {
  const std::filesystem::path texts = translationTexts(language);
  return std::make_unique<Screen>(
      document, language, [&texts, language](assets::MemoryFileSystem& memory) {
        if (!std::filesystem::is_directory(texts)) {
          return;
        }
        for (const auto& entry : std::filesystem::directory_iterator{texts}) {
          memory.addFile(std::filesystem::path{"packs/base/text"} / language /
                             entry.path().filename(),
                         fileText(entry.path()));
        }
      });
}

struct Text {
  std::string key;
  std::string value;
};

std::vector<Text> textsOf(std::string_view language, std::string_view prefix) {
  std::vector<Text> found;
  const std::filesystem::path main = translationTexts(language) / "main.yaml";
  if (!std::filesystem::exists(main)) {
    return found;
  }
  const std::regex line{R"re(^"([^"]+)": "(.*)"\s*$)re"};
  std::ifstream stream{main};
  std::string text;
  while (std::getline(stream, text)) {
    std::smatch match;
    if (std::regex_match(text, match, line) && match[1].str().starts_with(prefix)) {
      found.push_back({.key = "main." + match[1].str(), .value = match[2].str()});
    }
  }
  return found;
}

std::vector<Text> longestTexts(std::string_view language, std::string_view prefix,
                               std::size_t count) {
  std::vector<Text> found = textsOf(language, prefix);
  std::ranges::sort(found, [](const Text& left, const Text& right) {
    return left.value.size() > right.value.size();
  });
  if (found.size() > count) {
    found.resize(count);
  }
  return found;
}

std::size_t longestWord(std::string_view text) {
  std::size_t longest = 0;
  for (const auto word : std::views::split(text, ' ')) {
    longest = std::max(longest, static_cast<std::size_t>(std::ranges::distance(word)));
  }
  return longest;
}

std::optional<Text> textWithLongestWord(std::string_view language, std::string_view prefix) {
  const std::vector<Text> found = textsOf(language, prefix);
  const auto most =
      std::ranges::max_element(found, {}, [](const Text& text) { return longestWord(text.value); });
  return most == found.end() ? std::nullopt : std::optional<Text>{*most};
}

bool namesDiscText(const std::string& text) {
  static const std::regex key{R"([a-z0-9_]+\.[a-z0-9_]+\.[0-9]{4})"};
  return std::regex_search(text, key);
}

std::vector<Ui::TextOverflow> overflowsOf(const Ui& ui, std::string_view document) {
  std::vector<Ui::TextOverflow> found = ui.overflowingTexts();
  std::erase_if(found, [document](const Ui::TextOverflow& overflow) {
    return overflow.document != document || namesDiscText(overflow.text);
  });
  for (const Ui::TextOverflow& overflow : found) {
    MESSAGE(overflow.document << ": " << overflow.element << " '" << overflow.text << "' "
                              << overflow.box << " by " << overflow.overflow << " dp");
  }
  return found;
}

std::vector<std::string> documents() {
  std::vector<std::string> found;
  const std::filesystem::path ui = std::filesystem::path{DWE_GAME_PACK} / "ui";
  for (const auto& entry : std::filesystem::directory_iterator{ui}) {
    if (entry.is_directory() && std::filesystem::exists(entry.path() / "document.rml")) {
      found.push_back(entry.path().filename().string());
    }
  }
  std::ranges::sort(found);
  return found;
}

bool apart(const Ui::Rect& one, const Ui::Rect& other) {
  return one.right <= other.left || other.right <= one.left || one.bottom <= other.top ||
         other.bottom <= one.top;
}

bool inside(const Ui::Rect& inner, const Ui::Rect& outer) {
  constexpr float Tolerance = 0.5F;
  return inner.left >= outer.left - Tolerance && inner.top >= outer.top - Tolerance &&
         inner.right <= outer.right + Tolerance && inner.bottom <= outer.bottom + Tolerance;
}

void publishShop(Ui& ui, std::string_view language, std::string_view phase, int rows = 8) {
  const std::vector<Text> names = longestTexts(language, "item_names.", 8);
  ui.setSetting("shop.money", 99999.0);
  ui.setSetting("shop.bits_shown", true);
  ui.setSetting("shop.mode", std::string{"buy"});
  ui.setSetting("shop.rows", static_cast<double>(rows));
  for (int row = 0; row < rows; ++row) {
    const std::string prefix = std::format("shop.row.{}.", row);
    const auto index = static_cast<std::size_t>(row % 8);
    const std::string name = index < names.size() ? names[index].key : std::string{"shop.buy"};
    ui.setSetting(prefix + "name", name);
    ui.setSetting(prefix + "price", row == 0 ? 99999.0 : 50.0 * (row + 1));
    ui.setSetting(prefix + "enabled", row != 7);
    ui.setSetting(prefix + "description", std::string{"shop.buy"});
  }
  ui.setSetting("shop.described", phase == "description" ? 0.0 : -1.0);
  ui.setSetting("shop.phase", std::string{phase});
  const UiValue revision = ui.setting("shop.revision");
  const double* count = std::get_if<double>(&revision);
  ui.setSetting("shop.revision", (count == nullptr ? 0.0 : *count) + 1.0);
}

} // namespace

TEST_CASE("every screen's labels stay inside their boxes in every skin and language") {
  const std::vector<std::string> names = documents();
  for (const std::string_view language : Languages) {
    CAPTURE(language);
    const auto screen = screenIn("hud", language);
    for (const std::string& name : names) {
      if (name == "hud") {
        continue;
      }
      auto id = assets::AssetId::parse("ui/" + name + "/document");
      REQUIRE(id.has_value());
      REQUIRE(screen->ui->loadDocument(name, *id));
    }
    for (const std::string_view skin : Skins) {
      CAPTURE(skin);
      screen->skin(skin);
      for (const std::string& name : names) {
        CAPTURE(name);
        screen->ui->show(name);
        screen->frames(8);
        CHECK(overflowsOf(*screen->ui, name).empty());
        screen->ui->hide(name);
        screen->frames(1);
      }
    }
  }
}

TEST_CASE("the shop's rows set the price in its own column, apart from the longest names") {
  for (const std::string_view language : Languages) {
    const auto screen = screenIn("shop_menu", language);
    publishShop(*screen->ui, language, "list", 12);
    for (const std::string_view skin : Skins) {
      CAPTURE(language);
      CAPTURE(skin);
      screen->skin(skin);
      screen->ui->show("shop_menu");
      screen->frames(40);
      CHECK(overflowsOf(*screen->ui, "shop_menu").empty());
      std::optional<float> priceRight;
      for (int slot = 1; slot <= 8; ++slot) {
        CAPTURE(slot);
        const auto name =
            screen->ui->elementDrawnRect("shop_menu", std::format("row-{}-name", slot));
        const auto price =
            screen->ui->elementDrawnRect("shop_menu", std::format("row-{}-price", slot));
        const auto row = screen->ui->elementDrawnRect("shop_menu", std::format("row-{}", slot));
        REQUIRE(name.has_value());
        REQUIRE(price.has_value());
        REQUIRE(row.has_value());
        CHECK(price->right - price->left > 0.0F);
        CHECK(name->right + 12.0F <= price->left + 0.5F);
        CHECK(inside(*price, *row));
        if (priceRight) {
          CHECK(price->right == doctest::Approx(*priceRight).epsilon(0.002));
        }
        priceRight = price->right;
      }
      screen->ui->hide("shop_menu");
      screen->frames(1);
    }
  }
}

TEST_CASE("the shop's description box opens clear of the list, its rows and its prompts") {
  for (const std::string_view language : {"en", "ru", "ja", "de"}) {
    const auto screen = screenIn("shop_menu", language);
    for (const std::string_view skin : Skins) {
      CAPTURE(skin);
      CAPTURE(language);
      screen->skin(skin);
      publishShop(*screen->ui, language, "list");
      screen->ui->show("shop_menu");
      screen->frames(10);
      publishShop(*screen->ui, language, "description");
      screen->frames(40);
      REQUIRE(screen->ui->elementShown("shop_menu", "description"));
      const auto box = screen->ui->elementDrawnRect("shop_menu", "description");
      REQUIRE(box.has_value());
      for (const std::string_view part : {"row-8", "list-back", "prompts"}) {
        CAPTURE(part);
        const auto drawn = screen->ui->elementDrawnRect("shop_menu", part);
        REQUIRE(drawn.has_value());
        CHECK(apart(*box, *drawn));
      }
      CHECK(overflowsOf(*screen->ui, "shop_menu").empty());
      screen->ui->hide("shop_menu");
      screen->frames(1);
    }
  }
}

TEST_CASE("the minimap's name and clock stay inside their boxes in every skin and language") {
  for (const std::string_view language : Languages) {
    const std::vector<Text> longest = longestTexts(language, "map_names.", 1);
    const std::optional<Text> wordy = textWithLongestWord(language, "map_names.");
    const std::array<std::string, 2> names{longest.empty() ? std::string{"Grey Lord's Mansion"}
                                                           : longest[0].key,
                                           wordy ? wordy->key : std::string{"Greylordsmansion"}};
    const auto screen = screenIn("hud", language);
    for (const std::string_view skin : Skins) {
      for (const auto& [name, size] :
           std::array<std::pair<std::string, std::string_view>, 4>{{{names[0], "medium"},
                                                                    {names[1], "small"},
                                                                    {names[1], "medium"},
                                                                    {names[0], "small"}}}) {
        CAPTURE(language);
        CAPTURE(skin);
        CAPTURE(name);
        CAPTURE(size);
        screen->skin(skin);
        screen->ui->setSetting("minimap", std::string{"auto"});
        screen->ui->setSetting("minimap_size", std::string{size});
        screen->ui->show("hud");
        MinimapGrid grid{.columns = 4,
                         .rows = 2,
                         .cells = {},
                         .cellSize = 100.0F,
                         .originX = 0.0F,
                         .originZ = 0.0F};
        grid.cells.assign(8, MinimapCell::Walkable);
        screen->ui->setMinimap(grid, name);
        screen->frames(40);
        std::vector<Ui::TextOverflow> found = overflowsOf(*screen->ui, "hud");
        std::erase_if(found, [](const Ui::TextOverflow& overflow) {
          return !overflow.element.contains("minimap");
        });
        CHECK(found.empty());
        CHECK_FALSE(namesDiscText(screen->ui->elementText("hud", "minimap-name")));
        const auto drawn = screen->ui->elementDrawnRect("hud", "minimap-name");
        REQUIRE(drawn.has_value());
        CHECK(inside(*drawn, {.left = 0.0F, .top = 0.0F, .right = 1280.0F, .bottom = 720.0F}));
        screen->ui->hide("hud");
        screen->frames(1);
      }
    }
  }
}

TEST_CASE("the naming box's prompts sit in its window or over the name's box, clear of both") {
  for (const std::string_view language : Languages) {
    const auto screen = screenIn("naming_box", language);
    for (const std::string_view skin : Skins) {
      CAPTURE(skin);
      CAPTURE(language);
      screen->skin(skin);
      screen->ui->show("naming_box", true);
      screen->frames(10);
      CHECK(overflowsOf(*screen->ui, "naming_box").empty());
      const auto prompts = screen->ui->elementDrawnRect("naming_box", "prompts");
      const auto panel = screen->ui->elementDrawnRect("naming_box", "panel");
      const auto name = screen->ui->elementDrawnRect("naming_box", "name-box");
      REQUIRE(prompts.has_value());
      REQUIRE(panel.has_value());
      REQUIRE(name.has_value());
      CAPTURE(prompts->top);
      CAPTURE(prompts->bottom);
      CAPTURE(prompts->right);
      const bool inPanel = inside(*prompts, *panel);
      const bool overName = apart(*prompts, *panel) && prompts->left >= name->left - 0.5F &&
                            prompts->right <= name->right + 0.5F;
      CHECK((inPanel || overName));
      CHECK(apart(*prompts, *name));
      screen->ui->hide("naming_box");
      screen->frames(1);
    }
  }
}

TEST_CASE("the co-op name tags of players standing together do not cover each other") {
  for (const std::string_view skin : Skins) {
    CAPTURE(skin);
    const auto screen = screenIn("coop_hud", "de");
    screen->skin(skin);
    const std::array<std::string_view, 3> prefixes{"coop_hud.tag.", "coop_hud.tag.2.",
                                                   "coop_hud.tag.3."};
    int number = 2;
    for (const std::string_view prefix : prefixes) {
      const std::string base{prefix};
      screen->ui->setSetting(base + "visible", true);
      screen->ui->setSetting(base + "x", 50.0 + number);
      screen->ui->setSetting(base + "y", 40.0);
      screen->ui->setSetting(base + "name", std::format("Spielerin {}", number));
      screen->ui->setSetting(base + "number", static_cast<double>(number));
      screen->ui->setSetting(base + "trade", number == 2);
      ++number;
    }
    screen->ui->setSetting("coop_hud.tag.revision", 1.0);
    screen->ui->show("coop_hud");
    screen->frames(10);
    const std::array<std::string_view, 3> tags{"tag", "tag-2", "tag-3"};
    for (std::size_t one = 0; one < tags.size(); ++one) {
      for (std::size_t other = one + 1; other < tags.size(); ++other) {
        CAPTURE(tags[one]);
        CAPTURE(tags[other]);
        const auto first = screen->ui->elementDrawnRect("coop_hud", tags[one]);
        const auto second = screen->ui->elementDrawnRect("coop_hud", tags[other]);
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        CHECK(apart(*first, *second));
      }
    }
    CHECK(overflowsOf(*screen->ui, "coop_hud").empty());
  }
}

} // namespace dwe::ui
