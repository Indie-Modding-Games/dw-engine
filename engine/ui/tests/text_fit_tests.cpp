#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
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
    const std::array locations{location("packs/base"), location("packs/game"),
                               location("packs/tests")};
    auto mounted = assets::VirtualFileSystem::mount(locations);
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
#list { position: absolute; left: 100dp; top: 560dp; width: 200dp; height: 60dp; overflow-y: auto; background-color: #000080; }
.row { display: block; height: 30dp; white-space: nowrap; }
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
<div id="list"><span class="row">Listed one</span><span class="row">Listed two</span><span class="row">Listed three</span><span class="row">Listed four</span></div>
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
    // A list's rows scrolled out of its box are not on screen.
    CHECK(overflowOf(found, "Listed") == nullptr);
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

// A scrim over the window, a box, a box closed by the original look's animation, a hidden one, a
// transparent one and an image, at 1280x720 (one pixel a dp).
constexpr std::string_view Windows = R"(<rml>
<head>
<link type="text/rcss" href="../common/base.rcss"/>
<style>
body { width: 100%; height: 100%; font-family: Noto Sans; font-size: 20dp; color: #ffffff; }
#scrim { position: absolute; left: 0; top: 0; width: 100%; height: 100%; background-color: #00000080; }
.box { position: absolute; background-color: #000080; }
#window { left: 100dp; top: 100dp; width: 300dp; height: 60dp; }
#closed { left: 100dp; top: 300dp; width: 300dp; height: 60dp; }
#gone { left: 100dp; top: 400dp; width: 300dp; height: 60dp; display: none; }
#faded { left: 100dp; top: 500dp; width: 300dp; height: 60dp; opacity: 0; }
#picture { position: absolute; left: 1200dp; top: 600dp; width: 200dp; height: 50dp; }
</style>
</head>
<body>
<div id="scrim"></div>
<div id="window" class="box"></div>
<div id="closed" class="box box-closed"></div>
<div id="gone" class="box"></div>
<div id="faded" class="box"></div>
<img id="picture"/>
</body>
</rml>
)";

TEST_CASE("the drawn rectangles of the other documents leave out scrims and what is not seen") {
  Content content;
  const assets::VirtualFileSystem& files = content.mount(Windows);
  auto ui = uiWith(files, 1280, 720);
  const std::vector<Ui::Rect> drawn = ui->drawnRects("save_indicator");
  // The window, and the image cut at the window's edge; not the scrim over the whole window, the
  // closed, hidden or transparent boxes.
  REQUIRE(drawn.size() == 2);
  CHECK(drawn[0] == Ui::Rect{.left = 100.0F, .top = 100.0F, .right = 400.0F, .bottom = 160.0F});
  CHECK(drawn[1] == Ui::Rect{.left = 1200.0F, .top = 600.0F, .right = 1280.0F, .bottom = 650.0F});
  CHECK(ui->drawnRects("fit").empty());
  CHECK(ui->drawnRectsIn("fit") == drawn);
  CHECK(ui->drawnRectsIn("save_indicator").empty());
  CHECK(ui->elementRect("fit", "window") ==
        Ui::Rect{.left = 100.0F, .top = 100.0F, .right = 400.0F, .bottom = 160.0F});
  CHECK_FALSE(ui->elementRect("fit", "nothing").has_value());
  ui->hide("fit");
  ui->update();
  CHECK(ui->drawnRects("save_indicator").empty());
  CHECK(ui->drawnRectsIn("fit").empty());
}

// A panel holding a label, a clock with a time box under it, and a box beside the clock, at
// 1280x720 (one pixel a dp).
constexpr std::string_view Parts = R"(<rml>
<head>
<link type="text/rcss" href="../common/base.rcss"/>
<style>
body { width: 100%; height: 100%; font-family: Noto Sans; font-size: 20dp; color: #ffffff; }
.box { position: absolute; background-color: #000080; }
#panel { left: 600dp; top: 400dp; width: 300dp; height: 100dp; }
#label { position: absolute; left: 10dp; top: 10dp; width: 100dp; height: 30dp; background-color: #800000; }
#clock { left: 100dp; top: 100dp; width: 100dp; height: 100dp; }
#time { position: absolute; left: 10dp; top: 110dp; width: 80dp; height: 30dp; background-color: #800000; }
#beside { left: 210dp; top: 100dp; width: 50dp; height: 50dp; }
#gone { display: none; }
</style>
</head>
<body>
<div id="panel" class="box"><div id="label"></div></div>
<div id="clock" class="box"><div id="time"></div></div>
<div id="beside" class="box"></div>
<div id="gone"><div id="hidden-part" class="box"></div></div>
</body>
</rml>
)";

TEST_CASE("one part of a document is checked against the rest of it but the panel round it") {
  Content content;
  const assets::VirtualFileSystem& files = content.mount(Parts);
  auto ui = uiWith(files, 1280, 720);
  CHECK(ui->drawnRectsOf("fit", "time") ==
        std::vector<Ui::Rect>{{.left = 110.0F, .top = 210.0F, .right = 190.0F, .bottom = 240.0F}});
  // Around the time box: the panel, its label, the clock (which it hangs under, not inside) and the
  // box beside it.
  CHECK(ui->drawnRectsAround("fit", "time").size() == 4);
  // Around the label: the panel holds it whole and is its frame, so it is left out.
  const std::vector<Ui::Rect> aroundLabel = ui->drawnRectsAround("fit", "label");
  CHECK(aroundLabel.size() == 3);
  CHECK(std::ranges::find(
            aroundLabel,
            Ui::Rect{.left = 600.0F, .top = 400.0F, .right = 900.0F, .bottom = 500.0F}) ==
        aroundLabel.end());
  // A part inside a hidden element, or none, shows nothing.
  CHECK(ui->drawnRectsOf("fit", "hidden-part").empty());
  CHECK(ui->drawnRectsOf("fit", "nothing").empty());
}

} // namespace dwe::ui
