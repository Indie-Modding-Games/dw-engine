#include <dwe/game/field/npc_spawns.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

using dwe::game::field::GuardFacts;
using dwe::game::field::npcPresenceChange;
using dwe::game::field::NpcSpawnRule;
using dwe::game::field::parseNpcSpawns;
using dwe::game::field::spawnsInSlot;
using dwe::game::field::Truth;

namespace {

// MAPHEAD's section of a map with a recruit (the shape of section 1): slot 1 stands there while
// trigger 232 is clear; once it is set, slots 0 and 2 come by day and slot 3 by night (variable
// 107, the hour). Slot 4 waits on a routine of the game that may write any trigger.
constexpr std::string_view Spawns = R"(spawns:
- map: town
  slot: 1
  species: 152
  autotalk: false
  guard:
    terms:
    - if:
      - test: trigger
        clear: true
        trigger: 232
      holds: true
  source:
    script: 0
    section: 1
    offset: 1228
- map: town
  slot: 0
  species: 77
  autotalk: true
  guard:
    terms:
    - if:
      - test: pstat
        compare: ge
        pstat: 107
        value: 7
      - test: pstat
        compare: lt
        combine: and
        pstat: 107
        value: 19
      - test: trigger
        combine: and
        trigger: 232
      holds: true
  source:
    script: 0
    section: 1
- map: town
  slot: 3
  species: 74
  autotalk: true
  guard:
    terms:
    - if:
      - test: pstat
        compare: lt
        pstat: 107
        value: 7
      - test: pstat
        compare: ge
        combine: or
        pstat: 107
        value: 19
      - test: trigger
        combine: and
        trigger: 232
      holds: true
  source:
    script: 0
    section: 1
- map: town
  slot: 4
  species: 80
  autotalk: true
  guard:
    terms:
    - forget:
        script: 0
    - if:
      - test: trigger
        trigger: 232
      holds: true
  source:
    script: 0
    section: 1
- map: elsewhere
  slot: 1
  species: 9
  autotalk: false
  source:
    script: 0
    section: 2
)";

struct Facts {
  std::map<std::int32_t, bool> triggers;
  std::map<std::int32_t, std::int64_t> variables;

  [[nodiscard]] GuardFacts guard() const {
    GuardFacts facts{[this](std::int32_t trigger) -> std::optional<bool> {
      const auto found = triggers.find(trigger);
      return found != triggers.end() ? found->second : false;
    }};
    facts.variables = [this](std::int32_t variable) -> std::optional<std::int64_t> {
      const auto found = variables.find(variable);
      return found != variables.end() ? std::optional{found->second} : std::nullopt;
    };
    return facts;
  }
};

std::vector<NpcSpawnRule> rules() {
  auto parsed = parseNpcSpawns(Spawns);
  REQUIRE(parsed);
  return *parsed;
}

} // namespace

TEST_CASE("npc spawns read each spawn of a map with its guard and source") {
  const auto read = rules();
  REQUIRE(read.size() == 5);
  CHECK(read[0].map == "town");
  CHECK(read[0].slot == 1);
  CHECK(read[0].species == 152);
  CHECK_FALSE(read[0].autotalk);
  CHECK(read[0].source == "script 0, section 1, offset 0x04cc");
  CHECK(read[1].autotalk);
  CHECK(read[4].guard.terms.empty());

  CHECK_FALSE(parseNpcSpawns("spawns:\n- map: town\n  slot: 1\n"));
  CHECK_FALSE(parseNpcSpawns("warps: []\n"));
}

TEST_CASE("npc spawns decide a slot by its spawns' guards, variables included") {
  const auto read = rules();
  Facts day{.triggers = {{232, true}}, .variables = {{107, 12}}};
  CHECK(spawnsInSlot(read, "town", 1, day.guard()) == Truth::False);
  CHECK(spawnsInSlot(read, "town", 0, day.guard()) == Truth::True);
  CHECK(spawnsInSlot(read, "town", 3, day.guard()) == Truth::False);
  // After a routine that may write any trigger, the trigger is not known.
  CHECK(spawnsInSlot(read, "town", 4, day.guard()) == Truth::Unknown);
  CHECK(spawnsInSlot(read, "elsewhere", 1, day.guard()) == Truth::True);
  CHECK(spawnsInSlot(read, "town", 9, day.guard()) == Truth::False);

  // Without a variable reader a variable test decides nothing (the field's own warps).
  GuardFacts triggersOnly{[](std::int32_t) -> std::optional<bool> { return true; }};
  CHECK(spawnsInSlot(read, "town", 0, triggersOnly) == Truth::Unknown);
  // The original compares the variable's byte.
  Facts wrapped{.triggers = {{232, true}}, .variables = {{107, 256 + 12}}};
  CHECK(spawnsInSlot(read, "town", 0, wrapped.guard()) == Truth::True);
}

TEST_CASE("a recruit's trigger takes its NPC off and brings the ones the map head adds") {
  const auto read = rules();
  Facts before{.triggers = {}, .variables = {{107, 12}}};
  Facts after{.triggers = {{232, true}}, .variables = {{107, 12}}};
  const auto change = npcPresenceChange(read, "town", before.guard(), after.guard());
  CHECK(change.leave == std::vector<std::size_t>{1});
  REQUIRE(change.arrive.size() == 1);
  CHECK(change.arrive[0]->slot == 0);
  CHECK(change.arrive[0]->species == 77);

  // Back the other way, and nothing at all for an unchanged story or another map.
  const auto undone = npcPresenceChange(read, "town", after.guard(), before.guard());
  CHECK(undone.leave == std::vector<std::size_t>{0});
  REQUIRE(undone.arrive.size() == 1);
  CHECK(undone.arrive[0]->slot == 1);
  CHECK(npcPresenceChange(read, "town", after.guard(), after.guard()).empty());
  CHECK(npcPresenceChange(read, "elsewhere", before.guard(), after.guard()).empty());
}

TEST_CASE("a variable the map head writes itself leaves its spawns undecided") {
  const auto read = parseNpcSpawns(R"(spawns:
- map: town
  slot: 5
  species: 30
  autotalk: false
  guard:
    terms:
    - if:
      - test: pstat
        compare: eq
        pstat: 110
        value: 3
      holds: true
  section_writes:
  - 110
  source:
    script: 0
    section: 1
)");
  REQUIRE(read);
  REQUIRE(read->size() == 1);
  CHECK((*read)[0].sectionWrites == std::vector<std::int32_t>{110});
  // The section rolled the variable before testing it: what the state holds now says nothing.
  Facts rolled{.triggers = {}, .variables = {{110, 3}}};
  CHECK(spawnsInSlot(*read, "town", 5, rolled.guard()) == Truth::Unknown);
}
