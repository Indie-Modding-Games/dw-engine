#include <dwe/assets/map_document.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/screens/field_hud_view.hpp>
#include <dwe/session/partner_view.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

#include "danger_alert.hpp"
#include "minimap_enemies.hpp"

namespace dwe::app {
namespace {

using game::partner::Stat;

assets::MapNpc npcOf(std::int32_t scriptId, std::int32_t hp, std::int32_t off, std::int32_t def) {
  assets::MapNpc npc;
  npc.scriptId = scriptId;
  npc.hp = hp;
  npc.off = off;
  npc.def = def;
  return npc;
}

DangerAnchor anchorAt(screens::WindowFraction at) {
  return [at](std::size_t, const assets::MapNpc&) { return std::optional{at}; };
}

// Section 1 fights wild; 2 sets the fixed-fight trigger first, 3 names its enemies in the
// predefined variable, 4 does both only on one branch of a check and fights wild on the other, and
// 5 clears the trigger it set before its battle.
constexpr std::string_view Listing = R"yaml(
sections:
- id: 0x0001
  at: l_0010
- id: 0x0002
  at: l_0020
- id: 0x0003
  at: l_0030
- id: 0x0004
  at: l_0040
- id: 0x0005
  at: l_0050
instructions:
- at: 0x0010
  label: l_0010
  op: start_battle
- at: 0x0012
  op: end_section
- at: 0x0020
  label: l_0020
  op: set_trigger
  trigger: 1
- at: 0x0022
  op: start_battle
- at: 0x0024
  op: end_section
- at: 0x0030
  label: l_0030
  op: set_pstat
  pstat: 250
  value: 3
- at: 0x0032
  op: start_battle
- at: 0x0034
  op: end_section
- at: 0x0040
  label: l_0040
  op: jump_if_trigger
  trigger: 9
  target: l_0046
- at: 0x0042
  op: set_trigger
  trigger: 1
- at: 0x0044
  op: start_battle
- at: 0x0046
  label: l_0046
  op: start_battle
- at: 0x0048
  op: end_section
- at: 0x0050
  label: l_0050
  op: set_trigger
  trigger: 1
- at: 0x0052
  op: unset_trigger
  trigger: 1
- at: 0x0054
  op: start_battle
- at: 0x0056
  op: end_section
)yaml";

} // namespace

TEST_CASE("danger alert: a wild monster crushes the partner from exactly 2.5 times its power") {
  const FightStrength partner{.hp = 100, .offense = 30, .defense = 10};
  // 100 × 40 = 4000; 2.5 times is 10000.
  CHECK(crushes({.hp = 250, .offense = 30, .defense = 10}, partner));
  CHECK(crushes({.hp = 100, .offense = 60, .defense = 40}, partner));
  CHECK_FALSE(crushes({.hp = 249, .offense = 30, .defense = 10}, partner));
  CHECK_FALSE(crushes({.hp = 100, .offense = 59, .defense = 40}, partner));
}

TEST_CASE(
    "danger alert: a partner with no HP or no battle stats is crushed by anything that fights") {
  CHECK(crushes({.hp = 1, .offense = 1, .defense = 0}, {.hp = 0, .offense = 50, .defense = 50}));
  CHECK(crushes({.hp = 1, .offense = 0, .defense = 1}, {.hp = 500, .offense = 0, .defense = 0}));
  // Nothing against nothing still meets the rule: 0 ≥ 2.5 × 0.
  CHECK(crushes({}, {}));
}

TEST_CASE("danger alert: the rule holds at the largest stats the game holds") {
  const FightStrength top{.hp = 9999, .offense = 999, .defense = 999};
  CHECK(crushes(top, {.hp = 3999, .offense = 999, .defense = 999}));
  CHECK_FALSE(crushes(top, {.hp = 4000, .offense = 999, .defense = 999}));
}

TEST_CASE("danger alert: each side fights with the HP and stats its battle would use") {
  assets::MapNpc wild = npcOf(7, 2000, 510, 260);
  CHECK(strengthOf(wild).hp == 2000);
  CHECK(strengthOf(wild).offense == 510);
  CHECK(strengthOf(wild).defense == 260);
  // A record that keeps the HP it has left enters with that, never above its maximum.
  wild.currentHp = 700;
  CHECK(strengthOf(wild).hp == 700);
  wild.currentHp = 2600;
  CHECK(strengthOf(wild).hp == 2000);

  session::PartnerView partner;
  partner.currentHp = 340;
  partner.stats[Stat::Hp] = 900;
  partner.stats[Stat::Offense] = 45;
  partner.stats[Stat::Defense] = 38;
  const FightStrength strength = strengthOf(partner);
  CHECK(strength.hp == 340);
  CHECK(strength.offense == 45);
  CHECK(strength.defense == 38);
}

TEST_CASE("danger alert: marks only the wild fighters on the camera that crush the partner") {
  const FightStrength partner{.hp = 100, .offense = 10, .defense = 10};
  const std::vector<assets::MapNpc> records{
      npcOf(1, 1000, 50, 50), // wild and strong
      npcOf(2, 1000, 50, 50), // strong, but a story fight or a town NPC
      npcOf(1, 50, 5, 5),     // wild and weak
      npcOf(1, 0, 50, 50),    // no record of a fighter
  };
  const std::set<std::int32_t> wild{1};
  const DangerMarks marks =
      dangerMarksOf(true, records, wild, partner, anchorAt({.x = 0.25, .y = 0.5}));
  CHECK(marks.at(0) == DangerMark{.visible = true, .x = 25.0, .y = 50.0});
  CHECK_FALSE(marks.at(1).visible);
  CHECK_FALSE(marks.at(2).visible);
  CHECK_FALSE(marks.at(3).visible);

  CHECK_FALSE(
      dangerMarksOf(false, records, wild, partner, anchorAt({.x = 0.25, .y = 0.5})).at(0).visible);
  CHECK_FALSE(
      dangerMarksOf(true, records, wild, partner, anchorAt({.x = 1.2, .y = 0.5})).at(0).visible);
  const DangerAnchor gone = [](std::size_t, const assets::MapNpc&) {
    return std::optional<screens::WindowFraction>{};
  };
  CHECK_FALSE(dangerMarksOf(true, records, wild, partner, gone).at(0).visible);
}

TEST_CASE("danger alert: the marks follow the partner's stats as they grow") {
  const std::vector<assets::MapNpc> records{npcOf(1, 1000, 50, 50)};
  const std::set<std::int32_t> wild{1};
  const DangerAnchor anchor = anchorAt({.x = 0.5, .y = 0.5});
  CHECK(dangerMarksOf(true, records, wild, {.hp = 400, .offense = 50, .defense = 50}, anchor)
            .at(0)
            .visible);
  CHECK_FALSE(dangerMarksOf(true, records, wild, {.hp = 401, .offense = 50, .defense = 50}, anchor)
                  .at(0)
                  .visible);
}

TEST_CASE("danger alert: the marks are published per slot, to tenths of a percent") {
  DangerMarks marks{};
  marks.at(2) = {.visible = true, .x = 12.3, .y = 45.6};
  const screens::ScreenSettings settings = settingsOf(marks);
  CHECK(settings.size() == DangerMarkCount * 3);
  bool found = false;
  for (const screens::ScreenSetting& setting : settings) {
    if (setting.name == "hud.danger.2.x") {
      found = true;
      CHECK(setting.value == ui::UiValue{12.3});
    }
  }
  CHECK(found);
}

TEST_CASE("danger alert: a battle the story fixes or names its enemies for is not a wild one") {
  auto listing = core::parseYaml(Listing);
  REQUIRE(listing.has_value());
  const StoryFightMarks marks{.fixedFightTrigger = 1, .predefinedVariable = 250};
  CHECK(fightingSections(*listing) == std::set<std::int32_t>{1, 2, 3, 4, 5});
  CHECK(wildFightingSections(*listing, marks) == std::set<std::int32_t>{1, 4, 5});
}

} // namespace dwe::app
