#include <dwe/game/save/migration.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/game/save/sections.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <ostream>
#include <string>
#include <string_view>

#include "fixture.hpp"

namespace {

using namespace dwe::game::save;
using dwe::core::YamlNode;
using testing::Partner;
using testing::PartnerSection;
using testing::readFixture;

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

std::string replaced(std::string_view text, std::string_view from, std::string_view to) {
  std::string result{text};
  result.replace(result.find(from), from.size(), to);
  return result;
}

SaveFile sampleFile() {
  using namespace std::chrono;
  SaveFile file;
  file.header = {.gameVersion = "0.1.0",
                 .rulesetId = "original",
                 .created = sys_days{2026y / October / 1} + 8h,
                 .updated = sys_days{2026y / October / 2} + 21h + 30min + 15s,
                 .playSeconds = 5400,
                 .label = "File City",
                 .autosaveReasons = {}};
  return file;
}

} // namespace

TEST_CASE("a save file is written and read back byte-identical") {
  SaveFile file = sampleFile();
  YamlNode state = YamlNode::mapping();
  state.set("hp", YamlNode::scalar("950"));
  state.set("name", YamlNode::scalar("yes", YamlNode::ScalarStyle::Quoted));
  file.sections.emplace("partner", SavedSection{.version = 3, .state = state});
  const auto text = writeSaveFile(file);
  REQUIRE(text);
  CHECK(text->starts_with("format: dwe-save\nformat_version: 2\n"));
  CHECK(text->ends_with("complete: true\n"));

  const auto read = parseSaveFile(*text);
  REQUIRE_MESSAGE(read, errorOf(read));
  CHECK(read->header == file.header);
  REQUIRE(read->sections.contains("partner"));
  CHECK(read->sections.at("partner").version == 3);
  CHECK(read->sections.at("partner").state == state);
  CHECK(writeSaveFile(*read).value() == *text);

  // A file written by hand in the same layout too.
  const std::string fixture = readFixture("partner_v1.yaml");
  const auto parsed = parseSaveFile(fixture);
  REQUIRE_MESSAGE(parsed, errorOf(parsed));
  CHECK(parsed->header == sampleFile().header);
  CHECK(writeSaveFile(*parsed).value() == fixture);
}

TEST_CASE("a version 1 section is migrated to version 2 on load") {
  SaveSections sections;
  Partner partner;
  PartnerSection serializer{partner};
  REQUIRE(sections.add("partner", serializer, PartnerSection::migrations()));
  CHECK(sections.version("partner") == 2);

  const auto file = parseSaveFile(readFixture("partner_v1.yaml"));
  REQUIRE_MESSAGE(file, errorOf(file));
  CHECK(file->sections.at("partner").version == 1);

  const auto loaded = sections.read(*file);
  REQUIRE_MESSAGE(loaded, errorOf(loaded));
  CHECK(partner.nickname == "Pal");
  CHECK(partner.hp == 950);
  CHECK(partner.maxHp == 950);

  // The migrated file is the version 2 fixture, and so is the file the systems write back.
  const auto migrated = sections.migrate(*file);
  REQUIRE(migrated);
  CHECK(writeSaveFile(*migrated).value() == readFixture("partner_v2.yaml"));
  SaveFile rewritten = *file;
  REQUIRE(sections.write(rewritten));
  CHECK(writeSaveFile(rewritten).value() == readFixture("partner_v2.yaml"));
}

TEST_CASE("unknown sections are kept and written back untouched") {
  SaveSections sections;
  Partner partner;
  PartnerSection serializer{partner};
  REQUIRE(sections.add("partner", serializer, PartnerSection::migrations()));
  auto file = parseSaveFile(readFixture("partner_v1.yaml"));
  REQUIRE(file);
  const SavedSection fishing = file->sections.at("mod_fishing");
  REQUIRE(sections.read(*file));
  partner.hp = 10;
  REQUIRE(sections.write(*file));
  CHECK(file->sections.at("mod_fishing").version == 4);
  CHECK(file->sections.at("mod_fishing").state == fishing.state);
  const std::string text = writeSaveFile(*file).value();
  CHECK(text.contains("  mod_fishing:\n    version: 4\n    state:\n      best_catch: 37\n"));
  CHECK(text.contains("      - \"river\"\n      - lake\n"));
}

TEST_CASE("a section newer than the game, or too old, is an error") {
  SaveSections sections;
  Partner partner;
  PartnerSection serializer{partner};
  REQUIRE(sections.add("partner", serializer, PartnerSection::migrations()));
  const std::string v1 = readFixture("partner_v1.yaml");

  auto newer = parseSaveFile(replaced(v1, "    version: 1\n", "    version: 3\n"));
  REQUIRE(newer);
  const auto loaded = sections.read(*newer);
  CHECK(errorOf(loaded).contains("section 'partner' is version 3, newer than this game reads (2)"));
  CHECK(partner.nickname.empty());

  MigrationChain recent{2};
  CHECK(recent.current() == 2);
  CHECK(errorOf(recent.migrate(YamlNode::mapping(), 1, "section 'x'"))
            .contains("older than this game reads (2)"));
}

TEST_CASE("a failing migration names the section and the versions") {
  SaveSections sections;
  Partner partner;
  PartnerSection serializer{partner};
  REQUIRE(sections.add("partner", serializer, PartnerSection::migrations()));
  auto file = parseSaveFile(replaced(readFixture("partner_v1.yaml"), "      hp: 950\n", ""));
  REQUIRE(file);
  CHECK(errorOf(sections.read(*file)) ==
        "section 'partner': migrating version 1 to 2: hp is missing");
}

TEST_CASE("a missing section is an error unless its system starts fresh") {
  struct Fresh final : SectionSerializer {
    bool started = false;
    [[nodiscard]] dwe::core::Result<YamlNode> write() const override { return YamlNode::mapping(); }
    dwe::core::Result<void> read(const YamlNode& /*state*/) override { return {}; }
    dwe::core::Result<void> readMissing() override {
      started = true;
      return {};
    }
  };
  struct Strict final : SectionSerializer {
    [[nodiscard]] dwe::core::Result<YamlNode> write() const override { return YamlNode::mapping(); }
    dwe::core::Result<void> read(const YamlNode& /*state*/) override { return {}; }
  };
  Fresh fresh;
  Strict strict;
  const SaveFile file = sampleFile();

  SaveSections freshOnly;
  REQUIRE(freshOnly.add("inventory", fresh));
  CHECK(freshOnly.read(file));
  CHECK(fresh.started);

  SaveSections both;
  REQUIRE(both.add("inventory", fresh));
  REQUIRE(both.add("battle", strict));
  CHECK(errorOf(both.read(file)) == "section 'battle': the save has no data for it");
  CHECK(errorOf(both.add("battle", strict)) == "section 'battle' is registered twice");
  CHECK_FALSE(both.add("Battle", strict));
}

TEST_CASE("the header is migrated by the format's chain") {
  // A made-up format 1 that called the label `title` and had no autosave reasons; format 2 is the
  // current layout.
  MigrationChain format;
  format.add([](const YamlNode& root) -> dwe::core::Result<YamlNode> {
    YamlNode renamed = renamedKey(root, "title", "label");
    renamed.set("autosave_reasons", YamlNode::sequence());
    return renamed;
  });
  const std::string old = replaced(readFixture("format_v1.yaml"), "label:", "title:");
  CHECK_FALSE(parseSaveFile(old));
  const auto read = parseSaveFile(old, format);
  REQUIRE_MESSAGE(read, errorOf(read));
  CHECK(read->header.label == "File City");

  const auto newer = parseSaveFile(
      replaced(readFixture("partner_v1.yaml"), "format_version: 2", "format_version: 3"));
  CHECK(errorOf(newer).contains("the save format is version 3, newer than this game reads (2)"));
}

TEST_CASE("format 1 saves load with no autosave reasons, and autosaves list theirs") {
  // format_v1.yaml is partner_v1.yaml as format 1 wrote it.
  const auto old = parseSaveFile(readFixture("format_v1.yaml"));
  REQUIRE_MESSAGE(old, errorOf(old));
  CHECK(old->header.autosaveReasons.empty());
  CHECK(writeSaveFile(*old).value() == readFixture("partner_v1.yaml"));

  SaveFile autosaved = sampleFile();
  autosaved.header.autosaveReasons = {"partner_sleep", "new_day", "from_a_newer_game"};
  const auto text = writeSaveFile(autosaved);
  REQUIRE(text);
  CHECK(text->contains("autosave_reasons:\n- partner_sleep\n- new_day\n- from_a_newer_game\n"));
  const auto read = parseSaveFile(*text);
  REQUIRE_MESSAGE(read, errorOf(read));
  CHECK(read->header == autosaved.header);

  CHECK(errorOf(parseSaveFile(replaced(*text, "- new_day", "- New Day")))
            .contains("'New Day' is not an autosave"));
  CHECK(errorOf(parseSaveFile(replaced(*text, "- new_day\n", "- [new_day]\n")))
            .starts_with("save file: "));
  autosaved.header.autosaveReasons = {"Bad Name"};
  CHECK(errorOf(writeSaveFile(autosaved)).contains("'Bad Name' is not an autosave reason"));
}

TEST_CASE("corrupt and truncated files are errors") {
  CHECK(errorOf(parseSaveFile("")).contains("not a save"));
  CHECK(errorOf(parseSaveFile("format: other\n")).contains("not a save"));
  CHECK(errorOf(parseSaveFile("format: dwe-save\nformat_version: [\n")).starts_with("save file: "));
  CHECK(errorOf(parseSaveFile("\x01\x02\xff garbage")).starts_with("save file: "));

  const std::string v1 = readFixture("partner_v1.yaml");
  CHECK(errorOf(parseSaveFile(replaced(v1, "complete: true", "complete: false")))
            .contains("not complete"));
  CHECK(errorOf(parseSaveFile(replaced(v1, "2026-10-01T08:00:00Z", "2026-02-30T08:00:00Z")))
            .contains("YYYY-MM-DDTHH:MM:SSZ"));
  CHECK(errorOf(parseSaveFile(replaced(v1, "play_seconds: 5400", "play_seconds: -1")))
            .contains("negative"));
  CHECK(errorOf(parseSaveFile(replaced(v1, "  partner:", "  Partner:")))
            .contains("not a section name"));
  CHECK(errorOf(parseSaveFile(replaced(v1, "    version: 1\n", "    version: 0\n")))
            .contains("1 or more"));

  // Cut anywhere before its final line end, the file fails: `complete` is its last key.
  for (std::size_t size = 0; size + 1 < v1.size(); ++size) {
    CAPTURE(size);
    CHECK_FALSE(parseSaveFile(std::string_view{v1}.substr(0, size)));
  }
}

TEST_CASE("migration helpers remove and rename keys in place") {
  YamlNode node = YamlNode::mapping();
  node.set("a", YamlNode::scalar("1"));
  node.set("b", YamlNode::scalar("2"));
  node.set("c", YamlNode::scalar("3"));
  const YamlNode without = withoutKey(node, "b");
  REQUIRE(without.entries().size() == 2);
  CHECK(without.entries()[1].key == "c");
  const YamlNode renamed = renamedKey(node, "b", "z");
  REQUIRE(renamed.entries().size() == 3);
  CHECK(renamed.entries()[1].key == "z");
  CHECK(renamed.entries()[1].value.text() == "2");
  CHECK(renamedKey(node, "missing", "z") == node);
}
