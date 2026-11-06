// The game pack's menus write every word the player reads through a text key, so the words come
// out in the player's language and change with it: a document gives an element its words with
// text-key="..." and a script with t(key). These tests catch words written straight into a
// document or a script (docs/architecture/localizacion.md).

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <vector>

namespace dwe::ui {
namespace {

std::string readFile(const std::filesystem::path& path) {
  std::ifstream stream{path, std::ios::binary};
  return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

std::vector<std::filesystem::path> filesWithExtension(std::string_view extension) {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::recursive_directory_iterator{DWE_GAME_PACK}) {
    if (entry.is_regular_file() && entry.path().extension() == extension) {
      files.push_back(entry.path());
    }
  }
  return files;
}

// The words between a document's tags, outside its comments and its <title> (which the player
// never sees): two letters in a row make a word.
std::vector<std::string> wordsInDocument(std::string text) {
  static const std::regex Comments{R"(<!--[\s\S]*?-->)"};
  static const std::regex Title{R"(<title>[\s\S]*?</title>)"};
  static const std::regex Between{R"(>([^<>]*)<)"};
  static const std::regex Word{R"([A-Za-z]{2,})"};
  text = std::regex_replace(text, Comments, "");
  text = std::regex_replace(text, Title, "");
  std::vector<std::string> words;
  for (auto it = std::sregex_iterator{text.begin(), text.end(), Between};
       it != std::sregex_iterator{}; ++it) {
    const std::string between = (*it)[1].str();
    // An entity such as &nbsp; is no word.
    const std::string plain = std::regex_replace(between, std::regex{R"(&[a-z]+;)"}, "");
    if (std::regex_search(plain, Word)) {
      words.push_back(between);
    }
  }
  return words;
}

// The script lines that hand set_text a quoted text with a word in it.
std::vector<std::string> literalTexts(const std::string& script) {
  static const std::regex Literal{R"(set_text\(\s*(["'])[^"']*[A-Za-z]{2,}[^"']*\1)"};
  std::vector<std::string> found;
  for (auto it = std::sregex_iterator{script.begin(), script.end(), Literal};
       it != std::sregex_iterator{}; ++it) {
    found.push_back(it->str());
  }
  return found;
}

} // namespace

TEST_CASE("the checks see words written into a document or a script") {
  CHECK(wordsInDocument("<title>Menu</title><p>Hello</p>") == std::vector<std::string>{"Hello"});
  CHECK(wordsInDocument("<!-- a note --><span text-key=\"hud.menu\"></span><b>+</b>").empty());
  CHECK(wordsInDocument("<p>&nbsp;</p>").empty());
  CHECK(literalTexts(R"(element:set_text("Save"))").size() == 1);
  CHECK(literalTexts(R"(element:set_text(t("save.title")))").empty());
  CHECK(literalTexts(R"(element:set_text("x" .. value))").empty());
}

TEST_CASE("the game pack's documents write no words of their own: they take them by text key") {
  const std::vector<std::filesystem::path> documents = filesWithExtension(".rml");
  REQUIRE_FALSE(documents.empty());
  for (const std::filesystem::path& document : documents) {
    for (const std::string& words : wordsInDocument(readFile(document))) {
      FAIL_CHECK(document.lexically_relative(DWE_GAME_PACK).generic_string(), " writes '", words,
                 "': give the element a text-key instead");
    }
  }
}

TEST_CASE("the game pack's scripts set no quoted words as text: they read them with t(key)") {
  const std::vector<std::filesystem::path> scripts = filesWithExtension(".lua");
  REQUIRE_FALSE(scripts.empty());
  for (const std::filesystem::path& script : scripts) {
    for (const std::string& line : literalTexts(readFile(script))) {
      FAIL_CHECK(script.lexically_relative(DWE_GAME_PACK).generic_string(), " has ", line,
                 ": read the words with t(key) instead");
    }
  }
}

} // namespace dwe::ui
