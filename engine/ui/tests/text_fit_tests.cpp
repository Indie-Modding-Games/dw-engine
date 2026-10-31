#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::ui {
namespace {

// The team pack from the source tree (its fonts and base style sheet) and a pack for the test's
// document, both the engine's own, in memory.
struct Content {
  assets::MemoryFileSystem memory;
  std::unique_ptr<assets::VirtualFileSystem> files;

  Content() {
    const std::filesystem::path game = DWE_GAME_PACK;
    for (const auto& entry : std::filesystem::recursive_directory_iterator{game}) {
      if (!entry.is_regular_file()) {
        continue;
      }
      std::ifstream stream{entry.path(), std::ios::binary};
      const std::string bytes{std::istreambuf_iterator<char>{stream},
                              std::istreambuf_iterator<char>{}};
      memory.addFile(std::filesystem::path{"packs/game"} / entry.path().lexically_relative(game),
                     bytes);
    }
    memory.addFile("packs/base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\npriority: 0\n");
    memory.addFile("packs/tests/pack.yaml",
                   "id: tests\nversion: 0.1.0\nlayer: base\npriority: 2\n");
  }

  const assets::VirtualFileSystem& mount(std::string_view document) {
    memory.addFile("packs/tests/ui/fit/document.rml", document);
    const auto location = [this](std::string_view root) {
      return assets::PackLocation{
          .fileSystem = &memory, .root = std::string{root}, .origin = assets::PackOrigin::Engine};
    };
    auto mounted = assets::VirtualFileSystem::mount(
        {location("packs/base"), location("packs/game"), location("packs/tests")});
    REQUIRE_MESSAGE(mounted.has_value(), mounted.error().message);
    files = std::make_unique<assets::VirtualFileSystem>(std::move(*mounted));
    return *files;
  }
};

// Boxes round texts that fit and texts that do not, in the modern font at 20 dp.
constexpr std::string_view Document = R"(<rml>
<head>
<link type="text/rcss" href="../common/base.rcss"/>
<style>
body { width: 100%; height: 100%; font-family: Noto Sans; font-size: 20dp; color: #ffffff; }
.box { position: absolute; background-color: #000080; }
.line { white-space: nowrap; }
#roomy { left: 100dp; top: 100dp; width: 300dp; height: 60dp; }
#narrow { left: 100dp; top: 200dp; width: 60dp; height: 40dp; }
#short { left: 100dp; top: 300dp; width: 120dp; height: 30dp; }
#framed { left: 500dp; top: 100dp; width: 80dp; height: 40dp; border: 3dp #ffffff; }
#clip { position: absolute; left: 500dp; top: 200dp; width: 50dp; height: 40dp; overflow: hidden; }
#unboxed { position: absolute; left: 500dp; top: 300dp; width: 20dp; }
#edge { position: absolute; left: 1200dp; top: 400dp; }
#ghost { left: 100dp; top: 400dp; width: 40dp; height: 30dp; }
#ghost .line { opacity: 1; }
#clear { position: absolute; left: 100dp; top: 500dp; width: 20dp; height: 30dp; color: transparent; background-color: #000080; }
#gone { left: 300dp; top: 500dp; width: 20dp; height: 30dp; display: none; }
#value { left: 700dp; top: 100dp; width: 100dp; height: 30dp; }
#label { position: absolute; left: 640dp; top: 100dp; }
#inset { left: 900dp; top: 100dp; width: 100dp; height: 30dp; }
#inside { position: absolute; left: 905dp; top: 100dp; }
#under { left: 900dp; top: 300dp; width: 150dp; height: 60dp; }
#over { left: 900dp; top: 310dp; width: 100dp; height: 60dp; background-color: #101010; }
#first { position: absolute; left: 700dp; top: 600dp; }
#second { position: absolute; left: 720dp; top: 600dp; }
</style>
</head>
<body>
<div id="roomy" class="box"><span class="line">It fits</span></div>
<div id="narrow" class="box"><span class="line">Far too long for it</span></div>
<div id="short" class="box">Words that wrap below the box</div>
<div id="framed" class="box"><span class="line">Under the border</span></div>
<div id="clip"><span class="line">Cut by the clip</span></div>
<div id="unboxed"><span class="line">Nothing drawn round it</span></div>
<div id="edge"><span class="line">Off the window's edge</span></div>
<div id="ghost" class="box box-closed"><span class="line">A closed box</span></div>
<div id="clear"><span class="line">Transparent</span></div>
<div id="gone" class="box"><span class="line">Not displayed</span></div>
<div id="value" class="box"></div><span id="label" class="line">Label over it</span>
<div id="inset" class="box"></div><span id="inside" class="line">Within</span>
<div id="under" class="box"><span class="line">Under it</span></div>
<div id="over" class="box"><span class="line">On top</span></div>
<span id="first" class="line">First text</span><span id="second" class="line">Second</span>
</body>
</rml>
)";

std::unique_ptr<Ui> uiWith(const assets::VirtualFileSystem& files, int width, int height) {
  UiDesc desc;
  desc.files = &files;
  desc.width = width;
  desc.height = height;
  auto ui = Ui::create(desc);
  REQUIRE_MESSAGE(ui.has_value(), ui.error().message);
  auto id = assets::AssetId::parse("ui/fit/document");
  REQUIRE(id.has_value());
  REQUIRE((*ui)->loadDocument("fit", *id));
  (*ui)->show("fit");
  (*ui)->update();
  return std::move(*ui);
}

const Ui::TextOverflow* overflowOf(const std::vector<Ui::TextOverflow>& found,
                                   std::string_view text) {
  const auto it = std::ranges::find_if(
      found, [text](const Ui::TextOverflow& overflow) { return overflow.text.contains(text); });
  return it == found.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE(
    "a text that sticks out of its box, past a clip, off the window, across a box or over a text is found, at any size") {
  Content content;
  const assets::VirtualFileSystem& files = content.mount(Document);
  for (const auto [width, height] :
       {std::pair{1280, 720}, std::pair{1920, 1080}, std::pair{1024, 768}, std::pair{1366, 768}}) {
    CAPTURE(width);
    auto ui = uiWith(files, width, height);
    const std::vector<Ui::TextOverflow> found = ui->overflowingTexts();
    for (const Ui::TextOverflow& overflow : found) {
      MESSAGE(overflow.element << " '" << overflow.text << "' " << overflow.box << " by "
                               << overflow.overflow << " dp");
    }
    CHECK(overflowOf(found, "It fits") == nullptr);
    const Ui::TextOverflow* narrow = overflowOf(found, "Far too long");
    REQUIRE(narrow != nullptr);
    CHECK(narrow->document == "fit");
    CHECK(narrow->box == "out of div#narrow.box");
    CHECK(narrow->element == "div#narrow.box > span.line");
    CHECK(narrow->overflow > 60.0F);
    // A text that wraps in a box too short for its lines goes out at the bottom.
    const Ui::TextOverflow* wrapped = overflowOf(found, "Words that");
    REQUIRE(wrapped != nullptr);
    CHECK(wrapped->box == "out of div#short.box");
    CHECK(wrapped->text.contains(" / "));
    // Within the box but over its border.
    const Ui::TextOverflow* framed = overflowOf(found, "Under the border");
    REQUIRE(framed != nullptr);
    CHECK(framed->box == "out of div#framed.box");
    const Ui::TextOverflow* clipped = overflowOf(found, "Cut by the clip");
    REQUIRE(clipped != nullptr);
    CHECK(clipped->box == "out of div#clip");
    // With no box drawn round it, only the window bounds a text.
    CHECK(overflowOf(found, "Nothing drawn round it") == nullptr);
    const Ui::TextOverflow* edge = overflowOf(found, "Off the window");
    REQUIRE(edge != nullptr);
    CHECK(edge->box == "off the window");
    // What the player does not see is not checked: a box the original look's animation keeps
    // closed (even with a text in it that sets its own opacity), a transparent text, one not
    // displayed.
    CHECK(overflowOf(found, "A closed box") == nullptr);
    CHECK(overflowOf(found, "Transparent") == nullptr);
    CHECK(overflowOf(found, "Not displayed") == nullptr);
    // A text that crosses the edge of a box beside it, not one wholly inside it.
    const Ui::TextOverflow* label = overflowOf(found, "Label over it");
    REQUIRE(label != nullptr);
    CHECK(label->box == "across the edge of div#value.box");
    CHECK(overflowOf(found, "Within") == nullptr);
    // Texts over each other: the first is reported, naming the second.
    const Ui::TextOverflow* first = overflowOf(found, "First text");
    REQUIRE(first != nullptr);
    CHECK(first->box.contains("Second"));
    CHECK(overflowOf(found, "Second") == nullptr);
    // A window over another hides what it covers: neither is out of place.
    CHECK(overflowOf(found, "Under it") == nullptr);
    CHECK(overflowOf(found, "On top") == nullptr);
    CHECK(found.size() == 7);
  }
}

TEST_CASE("a hidden document's texts are not checked") {
  Content content;
  const assets::VirtualFileSystem& files = content.mount(Document);
  auto ui = uiWith(files, 1280, 720);
  REQUIRE_FALSE(ui->overflowingTexts().empty());
  ui->hide("fit");
  ui->update();
  CHECK(ui->overflowingTexts().empty());
}

} // namespace dwe::ui
