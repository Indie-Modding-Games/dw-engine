#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "dwx.hpp"
#include "story_conversion.hpp"

using dwe::test_support::TempFolder;

namespace {

// A listing as dwx writes it: section 0x51 shows a text and ends.
constexpr std::string_view Listing = R"(code_start: 0x0008
code_end: 0x0010
sections:
  - id: 0x0051
    at: l_0008
unreachable_instructions: 0
instructions:
  - at: 0x0008
    op: show_textbox
    text: "Hello"
  - at: 0x000c
    op: end_section
)";

std::string readText(const std::filesystem::path& path) {
  std::ifstream file{path, std::ios::binary};
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

} // namespace

TEST_CASE("dwx convert-script writes the module of a listing with its line map and its texts") {
  const TempFolder temp;
  temp.write("script/dg/049.yaml", Listing);
  REQUIRE(dwe::dwx::convertScript(temp.path() / "script/dg/049.yaml", temp.path() / "out") ==
          dwe::dwx::ExitOk);
  const std::string code = readText(temp.path() / "out/049.lua");
  CHECK(code.starts_with("-- dwx lua 2: script 49, from script/dg/049\n"));
  CHECK(code.contains("story.show_textbox(\"story_049.text.0008\")"));
  CHECK(code.contains("[0x0051] = routines.at_0008,"));
  CHECK(readText(temp.path() / "out/049_lines.yaml").contains(": 0x0008"));
  CHECK(readText(temp.path() / "out/049_texts.yaml") == "text.0008: \"Hello\"\n");
}

TEST_CASE("dwx convert-script writes the module of the map header listing as script 0") {
  const TempFolder temp;
  temp.write("script/maphead.yaml", Listing);
  REQUIRE(dwe::dwx::convertScript(temp.path() / "script/maphead.yaml", temp.path() / "out") ==
          dwe::dwx::ExitOk);
  const std::string code = readText(temp.path() / "out/000.lua");
  CHECK(code.starts_with("-- dwx lua 2: script 0, from script/maphead\n"));
  CHECK(code.contains("story.show_textbox(\"story_000.text.0008\")"));
  CHECK(std::filesystem::exists(temp.path() / "out/000_lines.yaml"));
}

TEST_CASE("dwx convert-script refuses a listing that is not named after its script") {
  const TempFolder temp;
  temp.write("edited.yaml", Listing);
  CHECK(dwe::dwx::convertScript(temp.path() / "edited.yaml", temp.path() / "out") ==
        dwe::dwx::ExitFailed);
  CHECK_FALSE(std::filesystem::exists(temp.path() / "out"));
}

TEST_CASE("dwx convert-script refuses a script number past what a script id holds") {
  const TempFolder temp;
  temp.write("script/dg/70000.yaml", Listing);
  CHECK(dwe::dwx::convertScript(temp.path() / "script/dg/70000.yaml", temp.path() / "out") ==
        dwe::dwx::ExitFailed);
  CHECK_FALSE(std::filesystem::exists(temp.path() / "out"));
}
