#include <dwe/core/yaml.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/game/partner/state_yaml.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/session/game_session.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>

#include "session_fixture.hpp"

using dwe::core::YamlNode;
using dwe::game::partner::Stat;
using dwe::game::save::SaveFile;
using dwe::session::testing::errorOf;
using dwe::session::testing::newGame;
using dwe::session::testing::TestPacks;

namespace {

constexpr std::int32_t KeptOffense = 321;

// The partner section as version 1 wrote it: the virus backup's stats straight under virus_backup.
void writeAsVersion1(SaveFile& file) {
  auto found = file.sections.find("partner");
  REQUIRE(found != file.sections.end());
  YamlNode& state = found->second.state;
  const YamlNode* backup = state.find("virus_backup");
  REQUIRE(backup != nullptr);
  const YamlNode* stats = backup->find("stats");
  REQUIRE(stats != nullptr);
  const YamlNode oldBackup = *stats;
  state.set("virus_backup", oldBackup);
  state.set("version", YamlNode::scalar("1"));
  found->second.version = 1;
}

} // namespace

TEST_CASE("a save whose partner section is version 1 loads, its virus backup without a species") {
  const TestPacks packs;
  auto saved = newGame(packs, nullptr);
  dwe::game::partner::PartnerState state = saved->partner().state();
  state.virusBackup = {.species = "agum", .stats = {}};
  state.virusBackup.stats[Stat::Offense] = KeptOffense;
  REQUIRE(saved->partner().restore(state).has_value());
  auto file = saved->saveFile();
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));
  CHECK(file->sections.at("partner").version == dwe::game::partner::PartnerStateVersion);
  writeAsVersion1(*file);

  auto loaded = newGame(packs, nullptr);
  const auto read = loaded->loadFile(*file);
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  CHECK(loaded->partner().state().virusBackup.species.empty());
  CHECK(loaded->partner().state().virusBackup.stats[Stat::Offense] == KeptOffense);
  CHECK(loaded->partner().state().species == saved->partner().state().species);
}

TEST_CASE(
    "a save whose random section is version 1 loads, the systems' numbers going on from the partner's") {
  const TestPacks packs;
  auto saved = newGame(packs, nullptr);
  auto file = saved->saveFile();
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));
  auto found = file->sections.find("random");
  REQUIRE(found != file->sections.end());
  CHECK(found->second.version == 2);
  YamlNode& state = found->second.state;
  const YamlNode* partner = state.find("partner");
  REQUIRE(partner != nullptr);
  YamlNode version1 = YamlNode::mapping();
  version1.set("partner", *partner);
  state = version1;
  found->second.version = 1;

  auto loaded = newGame(packs, nullptr);
  const auto read = loaded->loadFile(*file);
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  CHECK(loaded->random().state() == saved->partner().randomState());
}
