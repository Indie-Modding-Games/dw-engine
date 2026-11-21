#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/input/action_map.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/input/keys.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/platform/events.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

#include "naming_box.hpp"

namespace dwe::app {
namespace {

struct NamingUi {
  assets::MemoryFileSystem memory;
  std::unique_ptr<assets::VirtualFileSystem> files;
  std::unique_ptr<localization::Catalog> texts;
  std::unique_ptr<ui::Ui> ui;

  explicit NamingUi(std::string_view language) {
    const std::filesystem::path pack{DWE_GAME_PACK};
    for (const auto& entry : std::filesystem::recursive_directory_iterator{pack}) {
      if (entry.is_regular_file()) {
        std::ifstream stream{entry.path(), std::ios::binary};
        memory.addFile(
            std::filesystem::path{"packs/game"} / entry.path().lexically_relative(pack),
            std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}});
      }
    }
    memory.addFile("packs/base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\npriority: 0\n");
    const auto location = [this](std::string_view root) {
      return assets::PackLocation{
          .fileSystem = &memory, .root = std::string{root}, .origin = assets::PackOrigin::Engine};
    };
    const std::array locations{location("packs/base"), location("packs/game")};
    auto mounted = assets::VirtualFileSystem::mount(locations);
    REQUIRE_MESSAGE(mounted.has_value(), mounted.error().message);
    files = std::make_unique<assets::VirtualFileSystem>(std::move(*mounted));
    auto catalog = localization::Catalog::load(*files, language);
    REQUIRE_MESSAGE(catalog.has_value(), catalog.error().message);
    texts = std::make_unique<localization::Catalog>(std::move(*catalog));
    auto created = ui::Ui::create(ui::UiDesc{.files = files.get(),
                                             .texts = texts.get(),
                                             .width = 1280,
                                             .height = 720,
                                             .clock = {},
                                             .scripts = {},
                                             .cursor = {}});
    REQUIRE_MESSAGE(created.has_value(), created.error().message);
    ui = std::move(*created);
    auto document = assets::AssetId::parse("ui/naming_box/document");
    REQUIRE_MESSAGE(document.has_value(), document.error().message);
    auto loaded = ui->loadDocument(NamingBoxDocument, *document);
    REQUIRE_MESSAGE(loaded.has_value(), loaded.error().message);
  }

  [[nodiscard]] std::string text(std::string_view setting) const {
    const ui::UiValue value = ui->setting(setting);
    const std::string* found = std::get_if<std::string>(&value);
    return found != nullptr ? *found : std::string{};
  }

  [[nodiscard]] double number(std::string_view setting) const {
    const ui::UiValue value = ui->setting(setting);
    const double* found = std::get_if<double>(&value);
    return found != nullptr ? *found : -1.0;
  }
};

NamingRules rules() {
  NamingRules naming{.length = 6,
                     .columns = 5,
                     .rows = {"ABCDE", "abcde"},
                     .rowsPage = "naming.page.latin",
                     .pages = {},
                     .typedBeyondPages = {"zh", "ja", "ko"},
                     .fullDeleteTakesTwo = false,
                     .repeatDelayFrames = 6,
                     .repeatFrames = 2,
                     .frameTicks = 1};
  naming.pages["ru"] = {NamingPage{.name = "naming.page.cyrillic", .rows = {"АБВГЁ", "абвгё"}}};
  naming.pages["ko"] = {
      NamingPage{.name = "naming.page.hangul", .rows = {"ㄱㄴㄷㄹㅎ", "ㅏㅓㅗㅡㅣ"}}};
  return naming;
}

bool tick(NamingBox& box, platform::KeyCode key = 0) {
  input::InputState input{input::defaultActionMap()};
  if (key != 0) {
    input.handle(platform::KeyChanged{.key = key, .down = true, .repeat = false});
  }
  return box.press(input);
}

} // namespace

TEST_CASE(
    "a language with an alphabet of its own shows its page first and the page keys turn to the Latin one") {
  // Only one UI may exist at a time.
  {
    const NamingUi russian{"ru"};
    NamingBox box{*russian.ui, rules(), {}};
    box.open(NameTarget::Tamer, "");
    CHECK(russian.number("naming_box.pages") == 2.0);
    CHECK(russian.text("naming_box.page_name") == "naming.page.cyrillic");
    CHECK(russian.text("naming_box.cell.0.0") == "А");
    CHECK(russian.text("naming_box.cell.0.5") == "а");

    tick(box, input::keys::E); // tab_next
    CHECK(russian.text("naming_box.page_name") == "naming.page.latin");
    CHECK(russian.text("naming_box.cell.0.0") == "A");
    tick(box, input::keys::E);
    CHECK(russian.text("naming_box.cell.0.0") == "А");

    box.type("ЁжA", false);
    tick(box);
    CHECK(box.name() == "ЁA");
  }
  const NamingUi english{"en"};
  NamingBox latin{*english.ui, rules(), {}};
  latin.open(NameTarget::Tamer, "");
  CHECK(english.number("naming_box.pages") == 1.0);
  latin.type("ЁA", false);
  tick(latin);
  CHECK(latin.name() == "A");
}

TEST_CASE(
    "Korean letters written in the naming box join into syllables and an erase takes them apart") {
  const NamingUi korean{"ko"};
  NamingBox box{*korean.ui, rules(), {}};
  box.open(NameTarget::Partner, "");
  CHECK(korean.text("naming_box.cell.0.0") == "ㄱ");
  box.type("ㅎㅏㄴㄱㅡㄹ", false);
  tick(box);
  CHECK(box.name() == "한글");
  tick(box, input::keys::Backspace); // cancel
  CHECK(box.name() == "한그");
  tick(box, input::keys::Backspace);
  CHECK(box.name() == "한ㄱ");
  box.type("늘", false);
  tick(box);
  CHECK(box.name() == "한ㄱ늘");
}

TEST_CASE(
    "an input method's characters are taken where the language types them and the fonts draw them") {
  {
    const NamingUi japanese{"ja"};
    NamingBox box{*japanese.ui, rules(), {}};
    box.open(NameTarget::Tamer, "");
    box.type("ゆう樹", false);
    tick(box);
    CHECK(box.name() == "ゆう樹");
  }
  const NamingUi english{"en"};
  NamingBox latin{*english.ui, rules(), {}};
  latin.open(NameTarget::Tamer, "");
  latin.type("ゆう樹A", false);
  tick(latin);
  CHECK(latin.name() == "A");
}

} // namespace dwe::app
