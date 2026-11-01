#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/localization/catalog.hpp>

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

using namespace dwe::localization;
using dwe::assets::MemoryFileSystem;
using dwe::assets::VirtualFileSystem;

namespace {

// Packs in memory: the engine's texts in English and Spanish, the original's text in English only,
// and a mod.
struct Packs {
  MemoryFileSystem memory;
  std::unique_ptr<VirtualFileSystem> files;

  Packs() {
    memory.addFile("base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
    memory.addFile("base/text/en/main.yaml",
                   "map_names.0000: \"  Native Forest  \"\nmap_names.0001: File City\n");
    memory.addFile("game/pack.yaml", "id: game\nversion: 1.0.0\nlayer: base\npriority: 1\n");
    memory.addFile(
        "game/localization/en/menu.yaml",
        "title: Pause\nitems: \"{count} of {total} items\"\nonly_english: Only in English\n");
    memory.addFile("game/localization/es/menu.yaml",
                   "title: Pausa\nitems: \"{count} de {total} objetos\"\n");
  }

  VirtualFileSystem& mount(const std::vector<std::filesystem::path>& roots = {"base", "game"}) {
    auto mounted = VirtualFileSystem::mount(memory, roots);
    REQUIRE_MESSAGE(mounted.has_value(), mounted.error().message);
    files = std::make_unique<VirtualFileSystem>(std::move(*mounted));
    return *files;
  }
};

Catalog loaded(const VirtualFileSystem& files, std::string_view language) {
  auto catalog = Catalog::load(files, language);
  REQUIRE_MESSAGE(catalog.has_value(), catalog.error().message);
  return std::move(*catalog);
}

} // namespace

TEST_CASE("a catalog reads the engine's texts and the original's text of a language") {
  Packs packs;
  const Catalog catalog = loaded(packs.mount(), "en");
  CHECK(catalog.language() == "en");
  CHECK(catalog.languages().size() == 2);
  CHECK(catalog.text("menu.title") == "Pause");
  CHECK(catalog.text("main.map_names.0001") == "File City");
  CHECK(catalog.warnings().empty());
}

TEST_CASE("a key missing in the language falls back to English, with a warning") {
  Packs packs;
  const Catalog catalog = loaded(packs.mount(), "es");
  CHECK(catalog.text("menu.title") == "Pausa");
  CHECK(catalog.text("menu.only_english") == "Only in English");
  // No Spanish table of the original's text: it shows in English.
  CHECK(catalog.text("main.map_names.0001") == "File City");
  REQUIRE(catalog.warnings().size() == 2);
  CHECK(catalog.warnings()[0].contains("localization/es/menu misses 1 keys of en"));
  CHECK(catalog.warnings()[0].contains("menu.only_english"));
  CHECK(catalog.warnings()[1] ==
        "pack base has no es translation of 1 tables (2 texts), which show in en: text/en/main");
}

TEST_CASE("the tables a language has no translation of at all make one warning per pack") {
  Packs packs;
  const int storyTables = 7;
  for (int i = 0; i < storyTables; ++i) {
    packs.memory.addFile(std::format("base/text/en/story_{:03}.yaml", i), "line.0: A\nline.1: B\n");
  }
  packs.memory.addFile("game/text/en/shop.yaml", "line.0: Buy\n");
  const Catalog catalog = loaded(packs.mount(), "es");
  // The menu's missing key, then base's 8 tables (main and the stories) and game's one, each pack
  // in one line.
  REQUIRE(catalog.warnings().size() == 3);
  CHECK(catalog.warnings()[0].contains("menu.only_english"));
  CHECK(
      catalog.warnings()[1] ==
      "pack base has no es translation of 8 tables (16 texts), which show in en: text/en/main, text/en/story_000, "
      "text/en/story_001, text/en/story_002, text/en/story_003, ...");
  CHECK(catalog.warnings()[2] ==
        "pack game has no es translation of 1 tables (1 texts), which show in en: text/en/shop");
}

TEST_CASE("the keys a translation's lock keeps on purpose are not missing") {
  Packs packs;
  packs.memory.addFile("base/text/en/btl.yaml", "names.0000: \"   \"\n");
  packs.memory.addFile("mod/pack.yaml", "id: spanish_text\nversion: 1.0.0\nlayer: mods\n");
  packs.memory.addFile("mod/text/es/main.yaml", "map_names.0001: Ciudad Archivo\n");
  packs.memory.addFile(
      "mod/text/es/.sources.lock",
      "sources:\n  main.map_names.0001: \"0123456789abcdef0123456789abcdef01234567\"\n"
      "kept_english: [main.map_names.0000]\nkept_original: [btl.names.0000]\n");
  const Catalog catalog = loaded(packs.mount({"base", "game", "mod"}), "es");
  // Only the menu's key: main keeps its other name in English and btl, untranslated, keeps all.
  REQUIRE(catalog.warnings().size() == 1);
  CHECK(catalog.warnings()[0].contains("menu.only_english"));
  CHECK(catalog.text("main.map_names.0000") == "  Native Forest  ");
}

TEST_CASE("a key no table has shows as itself") {
  Packs packs;
  const Catalog catalog = loaded(packs.mount(), "es");
  CHECK(catalog.text("menu.nothing") == "menu.nothing");
  CHECK_FALSE(catalog.find("menu.nothing").has_value());
}

TEST_CASE("a translation of the original's text in a mod pack replaces the English one") {
  Packs packs;
  packs.memory.addFile("mod/pack.yaml", "id: spanish_text\nversion: 1.0.0\nlayer: mods\n");
  packs.memory.addFile("mod/text/es/main.yaml", "map_names.0001: Ciudad Archivo\n");
  const Catalog catalog = loaded(packs.mount({"base", "game", "mod"}), "es");
  CHECK(catalog.text("main.map_names.0001") == "Ciudad Archivo");
  CHECK(loaded(*packs.files, "en").text("main.map_names.0001") == "File City");
}

TEST_CASE("a mod pack changes single keys of a table and keeps the rest") {
  Packs packs;
  packs.memory.addFile("mod/pack.yaml", "id: wording\nversion: 1.0.0\nlayer: mods\n");
  packs.memory.addFile("mod/localization/es/menu.yaml", "title: Descanso\n");
  VirtualFileSystem& files = packs.mount({"base", "game", "mod"});
  const Catalog catalog = loaded(files, "es");
  CHECK(catalog.text("menu.title") == "Descanso");
  CHECK(catalog.find("menu.items") == "{count} de {total} objetos");
  // The merged table is what dwx validate checks too: the override misses no key.
  for (const auto& problem : dwe::assets::checkTranslations(files)) {
    CHECK_MESSAGE(!problem.message.contains("menu.items"), problem.message);
  }
}

TEST_CASE("placeholders are filled by name, and doubled braces are literal") {
  Packs packs;
  const Catalog catalog = loaded(packs.mount(), "es");
  const std::array arguments{TextArgument{.name = "count", .value = "3"},
                             TextArgument{.name = "total", .value = "7"}};
  CHECK(catalog.text("menu.items", arguments) == "3 de 7 objetos");
  CHECK(formatText("{{braces}} {name} {digimon:pstat=3}", arguments) ==
        "{braces} {name} {digimon:pstat=3}");
  // The layout tags of the original's text become plain text.
  CHECK(formatText("{color:1}Agumon{color:7} learned!{br}Next{wait}", arguments) ==
        "Agumon learned!\nNext\n");
  CHECK(formatText("Total {0} m {1} cm", std::array{TextArgument{.name = "1", .value = "20"},
                                                    TextArgument{.name = "0", .value = "3"}}) ==
        "Total 3 m 20 cm");
  CHECK(formatText("{count}{count}", arguments) == "33");
  CHECK(formatText("open { and close }", arguments) == "open { and close }");
  CHECK(numberText(3.0) == "3");
  CHECK(numberText(-12.0) == "-12");
  CHECK(numberText(0.75) == "0.75");
}

TEST_CASE("a plural placeholder picks the word for one only for a count of exactly 1") {
  const auto days = [](std::string_view count) {
    return formatText("{days} {days:día|días}",
                      std::array{TextArgument{.name = "days", .value = std::string{count}}});
  };
  CHECK(days("1") == "1 día");
  CHECK(days("2") == "2 días");
  CHECK(days("0") == "0 días");
  CHECK(days("1.5") == "1.5 días");
  CHECK(days("") == " días");
  CHECK(formatText("{n} card{n:|s}", std::array{TextArgument{.name = "n", .value = "1"}}) ==
        "1 card");
  CHECK(formatText("{n} card{n:|s}", std::array{TextArgument{.name = "n", .value = "3"}}) ==
        "3 cards");
  // Without its count, it stays as written, as any placeholder without an argument.
  CHECK(formatText("{days:day|days}", {}) == "{days:day|days}");
  CHECK(takesOne(1.0));
  CHECK_FALSE(takesOne(0.0));
  CHECK_FALSE(takesOne(-1.0));
  CHECK_FALSE(takesOne(11.0));
  const dwe::assets::PluralPlaceholder lives{.count = "amount", .one = "life", .other = "lives"};
  CHECK(pluralWord(lives, "1") == "life");
  CHECK(pluralWord(lives, "3") == "lives");
  CHECK(pluralWord(lives, "one") == "lives");
}

TEST_CASE("the language changes at run time and an unknown one keeps the current") {
  Packs packs;
  Catalog catalog = loaded(packs.mount(), "en");
  const auto first = catalog.revision();
  REQUIRE(catalog.setLanguage("es").has_value());
  CHECK(catalog.language() == "es");
  CHECK(catalog.text("menu.title") == "Pausa");
  CHECK(catalog.revision() != first);

  const auto second = catalog.revision();
  const auto failed = catalog.setLanguage("fr");
  REQUIRE_FALSE(failed.has_value());
  CHECK(failed.error().message.contains("no texts in 'fr'"));
  CHECK(catalog.language() == "es");
  CHECK(catalog.text("menu.title") == "Pausa");
  CHECK(catalog.revision() == second);
  CHECK_FALSE(Catalog::load(*packs.files, "fr").has_value());
}

TEST_CASE("a broken table fails the load") {
  Packs packs;
  packs.memory.addFile("game/localization/es/broken.yaml", "title: \"{open\"\n");
  VirtualFileSystem& files = packs.mount();
  CHECK_FALSE(Catalog::load(files, "es").has_value());
  CHECK(Catalog::load(files, "en").has_value());
}

TEST_CASE("tr reads the catalog the app sets") {
  Packs packs;
  const Catalog catalog = loaded(packs.mount(), "es");
  CHECK(tr("menu.title") == "menu.title");
  useCatalog(&catalog);
  CHECK(tr("menu.title") == "Pausa");
  useCatalog(nullptr);
}

TEST_CASE("the default language is the system's when the packs have it") {
  const std::vector<std::string> available{"en", "es"};
  CHECK(pickLanguage(available, "es") == "es");
  CHECK(pickLanguage(available, "en") == "en");
  CHECK(pickLanguage(available, "fr") == "en");
  CHECK(pickLanguage(available, "") == "en");
}

TEST_CASE("map names are keys of the original's map_names block") {
  CHECK(mapNameKey(3) == "main.map_names.0003");
  CHECK(mapNameKey(-1).empty());
  CHECK(gameTextKey("btl", "menu", 87) == "btl.menu.0087");
}

TEST_CASE("the game pack's Spanish texts cover every English key") {
  MemoryFileSystem memory;
  const std::filesystem::path game = DWE_GAME_PACK;
  for (const auto& entry : std::filesystem::recursive_directory_iterator{game}) {
    if (entry.is_regular_file()) {
      std::ifstream stream{entry.path(), std::ios::binary};
      const std::string bytes{std::istreambuf_iterator<char>{stream},
                              std::istreambuf_iterator<char>{}};
      memory.addFile(std::filesystem::path{"game"} / entry.path().lexically_relative(game), bytes);
    }
  }
  memory.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\n");
  auto files = VirtualFileSystem::mount(memory, std::vector<std::filesystem::path>{"base", "game"});
  REQUIRE_MESSAGE(files.has_value(), files.error().message);
  const Catalog catalog = loaded(*files, "es");
  CHECK(catalog.warnings().empty());
  CHECK(catalog.text("language.name") == "Español");
}
