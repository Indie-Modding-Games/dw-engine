#include <dwe/assets/asset_id.hpp>
#include <dwe/ui/text_font.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

using fixture::Screen;

std::string probe(std::string_view language, std::string_view blocks) {
  return std::format(R"(<rml><head><style>
body.probe-root {{ font-family: "Noto Sans"; font-size: 20dp; line-height: 30dp; color: #ffffff; }}
.probe {{ display: inline-block; max-width: 110dp; }}
.wide {{ display: inline-block; }}
.caps {{ text-transform: uppercase; }}
</style></head><body class="probe-root" lang="{}">{}</body></rml>)",
                     language, blocks);
}

float width(const Screen& screen, std::string_view id) {
  const auto rect = screen.ui->elementRect("probe", id);
  REQUIRE_MESSAGE(rect.has_value(), id);
  return rect->right - rect->left;
}

float lines(const Screen& screen, std::string_view id) {
  const auto rect = screen.ui->elementRect("probe", id);
  REQUIRE_MESSAGE(rect.has_value(), id);
  return (rect->bottom - rect->top) / 30.0F;
}

// The UI keeps pointers into its Screen, so the Screen must not move.
std::unique_ptr<Screen> probeScreen(std::string_view language, std::string_view blocks) {
  const std::string document = probe(language, blocks);
  auto screen = std::make_unique<Screen>("probe", language, [&](assets::MemoryFileSystem& memory) {
    memory.addFile("packs/game/ui/probe/document.rml", document);
  });
  screen->ui->show("probe");
  screen->frames(2);
  return screen;
}

} // namespace

TEST_CASE(
    "the game pack's fallback chains give every family the CJK faces, the language's own first") {
  fixture::Content content;
  const auto id = assets::AssetId::parse(FontFallbacksAsset);
  REQUIRE(id.has_value());
  const auto bytes = content.files->read(*id);
  REQUIRE(bytes.has_value());
  const std::string yaml(reinterpret_cast<const char*>(bytes->data()), bytes->size());
  const auto chains = parseFontFallbacks(yaml);
  REQUIRE_MESSAGE(chains.has_value(), chains.error().message);
  std::vector<std::string> families;
  for (const FallbackChain& chain : *chains) {
    families.insert(families.end(), chain.families.begin(), chain.families.end());
    CHECK_FALSE(chain.faces.empty());
  }
  for (const std::string_view family : {"Noto Sans", "Pixel Body", "Pixel Small"}) {
    CHECK(std::ranges::find(families, family) != families.end());
  }

  const std::vector<FallbackFace> faces{{.family = "SC", .language = "zh", .scale = 1.0F},
                                        {.family = "JP", .language = "ja", .scale = 1.0F},
                                        {.family = "KR", .language = "ko", .scale = 1.0F},
                                        {.family = "Smooth SC", .language = "zh", .scale = 1.0F},
                                        {.family = "Smooth JP", .language = "ja", .scale = 1.0F},
                                        {.family = "Any", .language = "", .scale = 1.0F}};
  const auto names = [](const std::vector<FallbackFace>& ordered) {
    std::vector<std::string> out;
    for (const FallbackFace& face : ordered) {
      out.push_back(face.family);
    }
    return out;
  };
  CHECK(names(fallbackOrder(faces, "ja")) ==
        std::vector<std::string>{"JP", "SC", "KR", "Smooth JP", "Smooth SC", "Any"});
  CHECK(names(fallbackOrder(faces, "ko")) ==
        std::vector<std::string>{"KR", "SC", "JP", "Smooth SC", "Smooth JP", "Any"});
  CHECK(names(fallbackOrder(faces, "en")) == names(faces));
}

TEST_CASE("a fallback chain that names no face or a scale that is not positive is refused") {
  CHECK_FALSE(
      parseFontFallbacks("chains:\n  - families: [\"Noto Sans\"]\n    faces: []\n").has_value());
  CHECK_FALSE(
      parseFontFallbacks(
          "chains:\n  - families: [\"Noto Sans\"]\n    faces:\n      - { family: X, scale: 0 }\n")
          .has_value());
  CHECK_FALSE(
      parseFontFallbacks(
          "chains:\n  - families: [\"Noto Sans\"]\n    faces:\n      - { family: X, size: 2 }\n")
          .has_value());
}

TEST_CASE("the fonts draw every alphabet of the nine languages, and nothing else is promised") {
  const Screen screen{"probe_fonts", "en", [](assets::MemoryFileSystem& memory) {
                        memory.addFile("packs/game/ui/probe_fonts/document.rml",
                                       "<rml><body></body></rml>");
                      }};
  CHECK(screen.ui->fontsDraw("Ação, Größe, Français, Español"));
  CHECK(screen.ui->fontsDraw("Ёлка и ёж"));
  CHECK(screen.ui->fontsDraw("简体中文，你好。"));
  CHECK(screen.ui->fontsDraw("ひらがな、カタカナ、漢字。"));
  CHECK(screen.ui->fontsDraw("한국어 이름"));
  CHECK_FALSE(screen.ui->fontsDraw("\U0001F600"));
  CHECK_FALSE(screen.ui->fontsDraw("龘"));
}

TEST_CASE("Chinese and Japanese lines break between characters, never before closing punctuation") {
  const auto probed =
      probeScreen("ja", R"(<div class="probe" id="ten">一二三四五六七八九十</div><br/>
<div class="probe" id="kinsoku">一二三四五。六七八九</div><br/>
<div class="wide" id="one">一二三四五</div>)");
  CHECK(width(*probed, "one") == doctest::Approx(100.0F).epsilon(0.05));
  CHECK(lines(*probed, "ten") == doctest::Approx(2.0F));
  CHECK(width(*probed, "ten") <= 110.0F);
  CHECK(lines(*probed, "kinsoku") == doctest::Approx(3.0F));
}

TEST_CASE("Korean lines break at spaces, as Korean is written, not inside a word") {
  const auto probed = probeScreen("ko", R"(<div class="probe" id="words">가나다 라마바</div><br/>
<div class="probe" id="word">가나다라마바사</div>)");
  CHECK(lines(*probed, "words") == doctest::Approx(2.0F));
  CHECK(lines(*probed, "word") == doctest::Approx(1.0F));
}

TEST_CASE("text-transform capitals know Cyrillic and accented Latin letters") {
  const auto probed = probeScreen("ru", R"(<span class="wide caps" id="small">ёлка жук ç</span><br/>
<span class="wide" id="capitals">ЁЛКА ЖУК Ç</span>)");
  CHECK(width(*probed, "small") == doctest::Approx(width(*probed, "capitals")));
}

TEST_CASE("the language box lists all nine languages in compact rows, each in its own language") {
  for (const std::string_view skin : {"original", "pixel", "devvago"}) {
    CAPTURE(skin);
    Screen screen{"main_menu"};
    screen.skin(skin);
    screen.ui->setSetting("title.boot", true);
    screen.ui->setSetting("title.language_first", true);
    screen.ui->show("main_menu", true);
    screen.frames();
    REQUIRE(screen.ui->elementHasClass("main_menu", "language-box", "compact"));
    CHECK(screen.ui->elementText("main_menu", "language-name-8") == "РУССКИЙ");
    CHECK(screen.ui->elementText("main_menu", "language-name-5") == "日本語");
    const auto box = screen.ui->elementRect("main_menu", "language-box");
    REQUIRE(box.has_value());
    CHECK(box->top >= 0.0F);
    CHECK(box->bottom <= 720.0F);
  }
}

} // namespace dwe::ui
