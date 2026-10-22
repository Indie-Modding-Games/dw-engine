#include <dwe/assets/file_system.hpp>
#include <dwe/assets/text_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

using namespace dwe::assets;

namespace {

AssetId idOf(std::string_view text) {
  auto id = AssetId::parse(text);
  REQUIRE(id.has_value());
  return *id;
}

TextTable engineTable(std::string_view yaml, std::string_view area) {
  auto table = parseTextTable(
      {.kind = TextTableKind::Engine, .language = "en", .name = std::string{area}}, yaml);
  REQUIRE_MESSAGE(table.has_value(), table.error().message);
  return *table;
}

dwe::core::Result<TextTable> gameTable(std::string_view yaml) {
  return parseTextTable({.kind = TextTableKind::Game, .language = "en", .name = "main"}, yaml);
}

bool parses(std::string_view yaml) {
  return parseTextTable({.kind = TextTableKind::Engine, .language = "en", .name = "menu"}, yaml)
      .has_value();
}

std::vector<std::string> messagesOf(const std::vector<ContentProblem>& problems) {
  std::vector<std::string> messages;
  messages.reserve(problems.size());
  for (const ContentProblem& problem : problems) {
    messages.push_back(problem.id.str() + ": " + problem.message);
  }
  return messages;
}

std::vector<std::string> placeholders(std::string_view text) {
  auto found = placeholdersOf(text);
  REQUIRE_MESSAGE(found.has_value(), found.error().message);
  return *found;
}

bool mentions(const std::vector<std::string>& messages, std::string_view part) {
  return std::ranges::any_of(messages,
                             [part](const std::string& message) { return message.contains(part); });
}

} // namespace

TEST_CASE("text tables are named by their folder, language and name") {
  const auto engine = textTableOf(idOf("localization/es/pause_menu"));
  REQUIRE(engine.has_value());
  const TextTableName engineName = engine.value_or(TextTableName{});
  CHECK(engineName.kind == TextTableKind::Engine);
  CHECK(engineName.language == "es");
  CHECK(engineName.name == "pause_menu");
  const auto game = textTableOf(idOf("text/en/main"));
  REQUIRE(game.has_value());
  CHECK(game.value_or(TextTableName{}).kind == TextTableKind::Game);
  CHECK_FALSE(textTableOf(idOf("text/en")).has_value());
  CHECK_FALSE(textTableOf(idOf("text/en/main/more")).has_value());
  CHECK_FALSE(textTableOf(idOf("ui/hud/document")).has_value());
}

TEST_CASE("a table maps flat keys to texts under its name") {
  const TextTable table =
      engineTable("title.main: Pause\ntitle.options: Options\nquit: Quit\n", "pause_menu");
  CHECK(table.size() == 3);
  CHECK(table.at("pause_menu.title.main") == "Pause");
  CHECK(table.at("pause_menu.quit") == "Quit");

  CHECK_FALSE(parses("Title: Pause\n"));
  CHECK_FALSE(parses("title..main: Pause\n"));
  CHECK_FALSE(parses("title: [a, b]\n"));
  CHECK_FALSE(parses("title:\n  main: Pause\n"));
  CHECK_FALSE(parses("title: null\n"));
  CHECK_FALSE(parses("- a\n"));
  CHECK_FALSE(parses("count: \"{count\"\n"));
}

TEST_CASE("a table of the original's text keys each string by source, block and padded slot") {
  CHECK(gameTextEntryKey("map_names", 3) == "map_names.0003");
  auto flat = gameTable("map_names.0000: \"  Native Forest  \"\nmap_names.0002: File City\n");
  REQUIRE_MESSAGE(flat.has_value(), flat.error().message);
  CHECK(flat->at("main.map_names.0000") == "  Native Forest  ");
  CHECK(flat->at("main.map_names.0002") == "File City");

  // The format dwx extract wrote before gives the same keys.
  auto listed =
      gameTable("source: main\nstrings:\n  map_names:\n  - \"  Native Forest  \"\n  - null\n"
                "  - \"File City\"\n");
  REQUIRE_MESSAGE(listed.has_value(), listed.error().message);
  CHECK(*listed == *flat);
  CHECK_FALSE(gameTable("source: main\nstrings:\n  map_names: text\n").has_value());
}

TEST_CASE("placeholders are what is between braces, and doubled braces are literal") {
  CHECK(placeholders("{count} of {total}, {count}") == std::vector<std::string>{"count", "total"});
  CHECK(placeholders("{{not}} one {color:2}") == std::vector<std::string>{"color:2"});
  CHECK(placeholders("plain").empty());
  CHECK_FALSE(placeholdersOf("{open").has_value());
  CHECK_FALSE(placeholdersOf("close}").has_value());
}

TEST_CASE("a translation is compared with its reference key by key") {
  const TextTable reference = engineTable("a: A\nb: \"{count} B\"\nc: C\n", "t");
  const TextTable translation = engineTable("b: \"{total} B\"\nc: C\nd: D\n", "t");
  const auto problems = compareTranslation(reference, translation);
  REQUIRE(problems.size() == 3);
  CHECK(problems[0].key == "t.a");
  CHECK(problems[0].kind == TranslationProblem::Kind::MissingKey);
  CHECK(problems[1].key == "t.b");
  CHECK(problems[1].kind == TranslationProblem::Kind::PlaceholderMismatch);
  CHECK(problems[2].key == "t.d");
  CHECK(problems[2].kind == TranslationProblem::Kind::ExtraKey);
  CHECK(compareTranslation(reference, reference).empty());
}

TEST_CASE("the translation check finds missing tables, keys, extra keys and placeholders") {
  MemoryFileSystem memory;
  memory.addFile("game/pack.yaml", "id: game\nversion: 1.0.0\nlayer: base\n");
  memory.addFile("game/localization/en/menu.yaml",
                 "title: Pause\ncount: \"{count} items\"\nquit: Quit\n");
  memory.addFile("game/localization/en/hud.yaml", "menu: Menu\n");
  memory.addFile("game/localization/es/menu.yaml",
                 "title: Pausa\ncount: \"{total} objetos\"\nextra: Más\n");
  memory.addFile("game/text/en/main.yaml", "names.0000: Agumon\nnames.0001: Gabumon\n");
  memory.addFile("game/localization/fr/hud.yaml", "menu: Menu\n");
  auto vfs = VirtualFileSystem::mount(memory, std::vector<std::filesystem::path>{"game"});
  REQUIRE_MESSAGE(vfs.has_value(), vfs.error().message);
  const std::vector<std::string> messages = messagesOf(checkTranslations(*vfs));
  INFO(messages.size());
  CHECK(mentions(messages, "localization/es/menu: misses the key menu.quit"));
  CHECK(mentions(messages, "localization/es/menu: has the key menu.extra"));
  CHECK(mentions(messages,
                 "localization/es/menu: has the key menu.count with the placeholders {total}"));
  CHECK(mentions(messages, "localization/en/hud: has no es translation (localization/es/hud)"));
  CHECK(mentions(messages, "localization/en/menu: has no fr translation (localization/fr/menu)"));
  // A language without the original's text shows it in English: that is not a problem.
  CHECK_FALSE(mentions(messages, "text/"));
  CHECK(messages.size() == 5);
}

TEST_CASE(
    "a translation of the original's text from another pack is checked against the reference") {
  MemoryFileSystem memory;
  memory.addFile("base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  memory.addFile("base/text/en/main.yaml",
                 "names.0000: \"{color:2}Agumon\"\nnames.0001: Gabumon\n");
  memory.addFile("mod/pack.yaml", "id: spanish_text\nversion: 1.0.0\nlayer: mods\n");
  memory.addFile("mod/text/es/main.yaml", "names.0000: Agumon\n");
  memory.addFile("mod/text/es/btl.yaml", "names.0000: Agumon\n");
  auto vfs = VirtualFileSystem::mount(memory, std::vector<std::filesystem::path>{"base", "mod"});
  REQUIRE_MESSAGE(vfs.has_value(), vfs.error().message);
  const std::vector<std::string> messages = messagesOf(checkTranslations(*vfs));
  CHECK(mentions(
      messages,
      "text/es/main: has the key main.names.0000 with the placeholders {}, where en has {color:2}"));
  CHECK(mentions(messages, "text/es/btl: has no en table to translate (text/en/btl)"));
  CHECK(messages.size() == 2);
}

TEST_CASE("a translation's lock names the English text of each translated string") {
  const auto empty = parseSourcesLock("sources: {}\nkept_english: []\n");
  CHECK(parseSourcesLock("sources: {}\nkept_english: []\nkept_original: [main.a.0000]\n")
            .has_value());
  REQUIRE_MESSAGE(empty.has_value(), empty.error().message);
  CHECK(empty->sources.empty());
  CHECK(empty->keptEnglish.empty());
  CHECK(sourceHash("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d");
  for (const std::string_view bad :
       {"sources: {}\n", "sources: {a.b: ABC}\nkept_english: []\n",
        "sources: []\nkept_english: []\n", "sources: {}\nkept_english: [Bad Key]\n",
        "sources: {}\nkept_english: []\nextra: 1\n"}) {
    CHECK_FALSE_MESSAGE(parseSourcesLock(bad).has_value(), bad);
  }

  const TextTable english{
      {"main.a.0000", "{color:1}One"}, {"main.a.0001", "Two"}, {"main.a.0002", "Three"}};
  const TextTable translation{{"main.a.0000", "{color:1}Uno"}, {"main.a.0001", "Dos"}};
  SourcesLock lock;
  lock.sources = {{"main.a.0000", sourceHash("{color:1}One")}};
  lock.keptOriginal = {"main.a.0002"};
  const auto messages = compareSourcesLock(lock, english, translation);
  REQUIRE(messages.size() == 1);
  CHECK(messages[0] == "main.a.0001 is translated, but the lock has no hash of its English text");
  lock.sources.insert_or_assign("main.a.0001", sourceHash("Two"));
  CHECK(compareSourcesLock(lock, english, translation).empty());
}
