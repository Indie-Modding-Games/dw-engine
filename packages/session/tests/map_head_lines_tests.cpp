#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/session/map_head_lines.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using namespace dwe;

namespace {

// Section 5 is a run of two jumps to texts; section 6 one jump.
constexpr std::string_view TwoSections = R"(sections:
- {id: "0x5", at: l_0100}
- {id: "0x6", at: l_0108}
instructions:
- {at: "0x100", op: jump_to, target: l_0200}
- {at: "0x104", op: jump_to, target: l_0210}
- {at: "0x108", op: jump_to, target: l_0220}
- {at: "0x200", op: show_textbox}
- {at: "0x210", op: show_textbox}
- {at: "0x220", op: show_textbox}
)";

// The same listing with section 5 down to its first line.
constexpr std::string_view OneLine = R"(sections:
- {id: "0x5", at: l_0100}
instructions:
- {at: "0x100", op: jump_to, target: l_0200}
- {at: "0x200", op: show_textbox}
)";

session::MapHeadLineTables readListing(std::string_view listing) {
  assets::MemoryFileSystem memory;
  memory.addFile("pack/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  memory.addFile("pack/script/maphead.yaml", std::string{listing});
  const std::vector<std::filesystem::path> roots{"pack"};
  const auto files = assets::VirtualFileSystem::mount(memory, roots);
  REQUIRE(files);
  auto tables = session::readMapHeadLineTables(*files);
  REQUIRE(tables);
  return *tables;
}

} // namespace

TEST_CASE("the map head's line tables are read again only when the listing changes") {
  const session::MapHeadLineTables first = readListing(TwoSections);
  REQUIRE(first.size() == 2);
  CHECK(first.at(5) == std::vector<std::string>{"story_000.text.0200", "story_000.text.0210"});
  CHECK(first.at(6) == std::vector<std::string>{"story_000.text.0220"});
  // The same listing again (the next shop's first visit): the same tables.
  CHECK(readListing(TwoSections) == first);
  // Another content's listing is its own, and the first comes back whole after it.
  const session::MapHeadLineTables other = readListing(OneLine);
  REQUIRE(other.size() == 1);
  CHECK(other.at(5) == std::vector<std::string>{"story_000.text.0200"});
  CHECK(readListing(TwoSections) == first);
}
