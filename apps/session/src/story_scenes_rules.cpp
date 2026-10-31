#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/session/story_scenes.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace dwe::session {

namespace {

constexpr std::string_view SectionText = "story_scenes";
constexpr std::string_view OriginalChain = "original";
constexpr std::string_view SeededChain = "seeded";
// main_menu.c: a registry record keeps three moves.
constexpr std::size_t RegisteredTechniques = 3;

std::int64_t integerAt(assets::YamlReader& reader, const core::YamlNode& mapping,
                       std::string_view key) {
  const core::YamlNode& node = reader.field(mapping, key);
  const auto value = reader.integer<std::int64_t>(node);
  if (reader.ok() && value < 0) {
    reader.fail(node, "the number cannot be negative");
  }
  return value;
}

void readBattle(assets::YamlReader& reader, const core::YamlNode& node,
                game::story::StoryBattleRules& battle) {
  if (!reader.expectKeys(node, "story_scenes.battle",
                         {"outcome_variable", "predefined_variable", "first_enemy_variable",
                          "enemy_count", "no_enemy", "defeat_section", "counter_max",
                          "fixed_fight_trigger", "show_name_variable", "show_name_never"})) {
    return;
  }
  battle.outcomeVariable = integerAt(reader, node, "outcome_variable");
  battle.predefinedVariable = integerAt(reader, node, "predefined_variable");
  battle.firstEnemyVariable = integerAt(reader, node, "first_enemy_variable");
  battle.enemyCount = integerAt(reader, node, "enemy_count");
  battle.noEnemy = integerAt(reader, node, "no_enemy");
  battle.defeatSection = reader.integer<std::uint16_t>(reader.field(node, "defeat_section"));
  battle.counterMax = integerAt(reader, node, "counter_max");
  battle.fixedFightTrigger = integerAt(reader, node, "fixed_fight_trigger");
  battle.showNameVariable = integerAt(reader, node, "show_name_variable");
  battle.showNameNever = integerAt(reader, node, "show_name_never");
}

void readPartner(assets::YamlReader& reader, const core::YamlNode& node,
                 game::story::PartnerRoutineRules& partner) {
  if (!reader.expectKeys(node, "story_scenes.partner",
                         {"virus_backup_variable", "target_variable"})) {
    return;
  }
  partner.virusBackupVariable = integerAt(reader, node, "virus_backup_variable");
  partner.targetVariable = integerAt(reader, node, "target_variable");
}

void readRoutines(assets::YamlReader& reader, const core::YamlNode& node, StoryScenesRules& rules) {
  if (!reader.expectKeys(node, "story_scenes.routines",
                         {"operand_variable", "tally_high_variable", "tally_low_variable",
                          "no_item", "remove_one", "remove_all", "selling_trigger",
                          "buying_cards_trigger"})) {
    return;
  }
  rules.operandVariable = integerAt(reader, node, "operand_variable");
  rules.tallyHighVariable = integerAt(reader, node, "tally_high_variable");
  rules.tallyLowVariable = integerAt(reader, node, "tally_low_variable");
  rules.noItem = integerAt(reader, node, "no_item");
  rules.removeOneCount = reader.integer<std::int32_t>(reader.field(node, "remove_one"));
  rules.removeAllCount = reader.integer<std::int32_t>(reader.field(node, "remove_all"));
  rules.sellingTrigger = integerAt(reader, node, "selling_trigger");
  rules.buyingCardsTrigger = integerAt(reader, node, "buying_cards_trigger");
}

void readRecruits(assets::YamlReader& reader, const core::YamlNode& node, StoryScenesRules& rules) {
  if (!reader.expectKeys(node, "story_scenes.recruits", {"first_trigger", "count"})) {
    return;
  }
  rules.firstRecruitTrigger = integerAt(reader, node, "first_trigger");
  rules.recruitCount = integerAt(reader, node, "count");
}

void readNewGame(assets::YamlReader& reader, const core::YamlNode& node, StoryScenesRules& rules) {
  if (!reader.expectKeys(node, "story_scenes.new_game", {"chain", "map", "exit"})) {
    return;
  }
  const core::YamlNode& chainNode = reader.field(node, "chain");
  const std::string chain = reader.text(chainNode);
  if (chain == OriginalChain) {
    rules.newGameChain = NewGameChain::Original;
  } else if (chain == SeededChain) {
    rules.newGameChain = NewGameChain::Seeded;
  } else if (reader.ok()) {
    reader.fail(chainNode, "the chain is original or seeded");
  }
  rules.newGameMap = reader.integer<std::int32_t>(reader.field(node, "map"));
  rules.newGameExit = reader.integer<std::int32_t>(reader.field(node, "exit"));
}

void readOverrides(assets::YamlReader& reader, const core::YamlNode& node,
                   game::story::ScreenHistoryRules& screens) {
  for (const core::YamlNode& item : reader.sequence(node, "story_scenes.previous_screen")) {
    if (!reader.expectKeys(item, "story_scenes.previous_screen[]", {"to", "previous"},
                           {"from", "previous_exit"})) {
      return;
    }
    screens.overrides.push_back({
        .from = reader.optionalInteger<std::int32_t>(item, "from"),
        .to = reader.integer<std::int32_t>(reader.field(item, "to")),
        .previous = reader.integer<std::int32_t>(reader.field(item, "previous")),
        .previousExit = reader.optionalInteger<std::int32_t>(item, "previous_exit"),
    });
  }
}

StorySceneSound readSound(assets::YamlReader& reader, const core::YamlNode& node,
                          std::string_view key) {
  const core::YamlNode& sound = reader.field(node, key);
  if (!reader.expectKeys(sound, "story_scenes.death.egg_box sound", {"slot", "sound"})) {
    return {};
  }
  return {.slot = integerAt(reader, sound, "slot"), .sound = integerAt(reader, sound, "sound")};
}

void readDeath(assets::YamlReader& reader, const core::YamlNode& node, StoryScenesRules& rules) {
  if (!reader.expectKeys(node, "story_scenes.death", {"section", "cleared_variable", "egg_box"})) {
    return;
  }
  const core::YamlNode& eggBox = reader.field(node, "egg_box");
  if (!reader.expectKeys(eggBox, "story_scenes.death.egg_box", {"open", "move", "choose"})) {
    return;
  }
  rules.death = StoryDeathRules{
      .section = reader.integer<std::uint16_t>(reader.field(node, "section")),
      .clearedVariable = integerAt(reader, node, "cleared_variable"),
      .eggBox = {.open = readSound(reader, eggBox, "open"),
                 .move = readSound(reader, eggBox, "move"),
                 .choose = readSound(reader, eggBox, "choose")},
  };
}

void readEnding(assets::YamlReader& reader, const core::YamlNode& node, StoryScenesRules& rules) {
  if (!reader.expectKeys(node, "story_scenes.ending",
                         {"gift_slot", "gift_species", "gift_techniques"})) {
    return;
  }
  EndingGift gift{.slot = reader.integer<std::size_t>(reader.field(node, "gift_slot")),
                  .species = reader.text(reader.field(node, "gift_species")),
                  .techniques = {}};
  const core::YamlNode& techniques = reader.field(node, "gift_techniques");
  if (!techniques.isSequence()) {
    reader.fail(techniques, "the techniques must be a list");
    return;
  }
  for (const core::YamlNode& technique : techniques.items()) {
    gift.techniques.push_back(reader.text(technique));
  }
  if (gift.techniques.size() > RegisteredTechniques) {
    reader.fail(techniques, "a record keeps three techniques at most");
  }
  rules.ending = std::move(gift);
}

} // namespace

core::Result<StoryScenesRules> readStoryScenesRules(const ruleset::Section& section) {
  assets::YamlReader reader;
  const core::YamlNode& root = section.node;
  StoryScenesRules rules;
  if (!reader.expectKeys(
          root, SectionText,
          {"battle", "partner", "routines", "recruits", "new_game", "previous_screen"},
          {"death", "ending"})) {
    return std::unexpected{reader.error(section.file)};
  }
  readBattle(reader, reader.field(root, "battle"), rules.battle);
  readPartner(reader, reader.field(root, "partner"), rules.partner);
  readRoutines(reader, reader.field(root, "routines"), rules);
  readRecruits(reader, reader.field(root, "recruits"), rules);
  readNewGame(reader, reader.field(root, "new_game"), rules);
  readOverrides(reader, reader.field(root, "previous_screen"), rules.screens);
  if (const core::YamlNode* death = root.find("death")) {
    readDeath(reader, *death, rules);
  }
  if (const core::YamlNode* ending = root.find("ending")) {
    readEnding(reader, *ending, rules);
  }
  return reader.finish(std::move(rules), section.file);
}

} // namespace dwe::session
