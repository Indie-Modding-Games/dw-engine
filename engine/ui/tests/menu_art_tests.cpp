// The original's word pictures (MENU, COUNT, ITEM...) are English: a document's body has
// MenuArtClass while the language is English, and the skin draws the text key's words without it.

#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <string_view>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

constexpr std::string_view LabelDocument = R"(<rml><head><title>Labels</title></head>
<body id="labels"><span id="menu-label" text-key="list_labels.menu"></span></body></rml>)";

void addLabelDocument(assets::MemoryFileSystem& memory) {
  memory.addFile("packs/game/ui/labels_test/document.rml", std::string{LabelDocument});
}

} // namespace

TEST_CASE("a document shows the original's word pictures only while the language is English") {
  {
    const fixture::Screen english{"labels_test", "en", addLabelDocument};
    CHECK(english.ui->elementHasClass("labels_test", "labels", MenuArtClass));
    CHECK(english.ui->elementText("labels_test", "menu-label") == "MENU");
  }
  const fixture::Screen spanish{"labels_test", "es", addLabelDocument};
  CHECK_FALSE(spanish.ui->elementHasClass("labels_test", "labels", MenuArtClass));
  CHECK(spanish.ui->elementText("labels_test", "menu-label") == "OBJETO");
}

TEST_CASE("changing the language in game swaps the word pictures for words and back") {
  fixture::Screen screen{"labels_test", "en", addLabelDocument};
  REQUIRE(screen.texts.setLanguage("es"));
  screen.ui->refreshTexts();
  CHECK_FALSE(screen.ui->elementHasClass("labels_test", "labels", MenuArtClass));
  CHECK(screen.ui->elementText("labels_test", "menu-label") == "OBJETO");
  REQUIRE(screen.texts.setLanguage("en"));
  screen.ui->refreshTexts();
  CHECK(screen.ui->elementHasClass("labels_test", "labels", MenuArtClass));
  CHECK(screen.ui->elementText("labels_test", "menu-label") == "MENU");
}

} // namespace dwe::ui
