#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/cards_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/medals_adapter.hpp>
#include <dwe/session/story_adapter.hpp>

#include <string>
#include <utility>

#include "progress_messages.hpp"

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;
using game::partner::Stat;
using game::story::MedalRules;

constexpr std::string_view FirstTriggerKey = "first_trigger";
constexpr std::string_view MedalLimitKey = "medal_limit";
constexpr std::string_view ConditionsKey = "conditions";
constexpr std::string_view AwardSoundKey = "award_sound";
constexpr std::string_view MedalKey = "medal";
constexpr std::string_view CountKey = "count";
constexpr std::string_view AllMovesKey = "all_moves";
constexpr std::string_view MaxStatsKey = "max_stats";
constexpr std::string_view MoneyKey = "money";
constexpr std::string_view YearKey = "year";
constexpr std::string_view AllRaisedKey = "all_raised";
constexpr std::string_view FishKey = "fish";
constexpr std::string_view AllCardsKey = "all_cards";
constexpr std::string_view AmountKey = "amount";
constexpr std::string_view FirstKey = "first";
constexpr std::string_view EndKey = "end";
constexpr std::string_view TamerLevelsKey = "tamer_levels";
constexpr std::string_view HpKey = "hp";
constexpr std::string_view MpKey = "mp";
constexpr std::string_view OffenseKey = "offense";
constexpr std::string_view DefenseKey = "defense";
constexpr std::string_view SpeedKey = "speed";
constexpr std::string_view BrainKey = "brain";
constexpr std::string_view SlotKey = "slot";
constexpr std::string_view SoundKey = "sound";

// The condition at `key` with exactly these keys, or nullptr when the section leaves it out.
const YamlNode* condition(YamlReader& reader, const YamlNode& conditions, std::string_view key,
                          std::initializer_list<std::string_view> keys) {
  const YamlNode* node = conditions.find(key);
  if (node == nullptr ||
      !reader.expectKeys(*node, core::formatMessage(progress_messages::ConditionText, key), keys)) {
    return nullptr;
  }
  return node;
}

std::int32_t integer(YamlReader& reader, const YamlNode& node, std::string_view key) {
  return reader.integer<std::int32_t>(reader.field(node, key));
}

void readConditions(YamlReader& reader, const YamlNode& node, MedalRules& rules) {
  if (!reader.expectKeys(
          node, progress_messages::ConditionsText, {},
          {AllMovesKey, MaxStatsKey, MoneyKey, YearKey, AllRaisedKey, FishKey, AllCardsKey})) {
    return;
  }
  if (const YamlNode* moves = condition(reader, node, AllMovesKey, {MedalKey, CountKey})) {
    rules.allMoves = MedalRules::AllMoves{.medal = integer(reader, *moves, MedalKey),
                                          .count = integer(reader, *moves, CountKey)};
  }
  if (const YamlNode* stats =
          condition(reader, node, MaxStatsKey,
                    {MedalKey, HpKey, MpKey, OffenseKey, DefenseKey, SpeedKey, BrainKey})) {
    rules.maxStats = MedalRules::MaxStats{.medal = integer(reader, *stats, MedalKey),
                                          .stats = {.hp = integer(reader, *stats, HpKey),
                                                    .mp = integer(reader, *stats, MpKey),
                                                    .offense = integer(reader, *stats, OffenseKey),
                                                    .defense = integer(reader, *stats, DefenseKey),
                                                    .speed = integer(reader, *stats, SpeedKey),
                                                    .brain = integer(reader, *stats, BrainKey)}};
  }
  if (const YamlNode* money = condition(reader, node, MoneyKey, {MedalKey, AmountKey})) {
    rules.money =
        MedalRules::Money{.medal = integer(reader, *money, MedalKey),
                          .amount = reader.integer<std::int64_t>(reader.field(*money, AmountKey))};
  }
  if (const YamlNode* year = condition(reader, node, YearKey, {MedalKey, YearKey})) {
    rules.year = MedalRules::Year{.medal = integer(reader, *year, MedalKey),
                                  .year = integer(reader, *year, YearKey)};
  }
  if (const YamlNode* raised =
          condition(reader, node, AllRaisedKey, {MedalKey, FirstKey, EndKey, TamerLevelsKey})) {
    rules.allRaised =
        MedalRules::AllRaised{.medal = integer(reader, *raised, MedalKey),
                              .first = integer(reader, *raised, FirstKey),
                              .end = integer(reader, *raised, EndKey),
                              .tamerLevels = integer(reader, *raised, TamerLevelsKey)};
  }
  if (const YamlNode* fish = condition(reader, node, FishKey, {MedalKey, CountKey})) {
    rules.fish = MedalRules::Fish{.medal = integer(reader, *fish, MedalKey),
                                  .count = integer(reader, *fish, CountKey)};
  }
  if (const YamlNode* cards = condition(reader, node, AllCardsKey, {MedalKey, CountKey})) {
    rules.allCards = MedalRules::AllCards{.medal = integer(reader, *cards, MedalKey),
                                          .count = integer(reader, *cards, CountKey)};
  }
}

} // namespace

core::Result<MedalsRules> readMedalsRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, progress_messages::MedalsSectionText,
                         {FirstTriggerKey, MedalLimitKey, ConditionsKey, AwardSoundKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  MedalsRules rules;
  rules.medals.firstTrigger = integer(reader, root, FirstTriggerKey);
  rules.medals.medalLimit = integer(reader, root, MedalLimitKey);
  readConditions(reader, reader.field(root, ConditionsKey), rules.medals);
  const YamlNode& sound = reader.field(root, AwardSoundKey);
  if (reader.expectKeys(sound, progress_messages::SoundText, {SlotKey, SoundKey})) {
    rules.awardSound = {.slot = integer(reader, sound, SlotKey),
                        .sound = integer(reader, sound, SoundKey)};
  }
  return reader.finish(std::move(rules), section.file);
}

MedalsAdapter::MedalsAdapter(MedalsAdapterDesc desc) : desc_{desc} {}

ruleset::SectionChecks MedalsAdapter::sectionChecks() const {
  return {{std::string{MedalsSection}, [](const ruleset::Section& section) -> core::Result<void> {
             auto rules = readMedalsRules(section);
             if (!rules) {
               return std::unexpected{rules.error()};
             }
             return {};
           }}};
}

core::Result<void> MedalsAdapter::readRules(const ruleset::Ruleset& rules) {
  rules_.reset();
  const ruleset::Section* section = rules.section(MedalsSection);
  if (section == nullptr) {
    core::logDebug("[port:medals] the ruleset has no medals section: no medal is checked");
    return {};
  }
  auto read = readMedalsRules(*section);
  if (!read) {
    return std::unexpected{read.error()};
  }
  rules_ = std::move(*read);
  return {};
}

core::Result<void> MedalsAdapter::startNewGame(GameSession& /*session*/) {
  awardPending_ = false;
  return {};
}

void MedalsAdapter::afterLoad(GameSession& /*session*/) {
  awardPending_ = false;
}

void MedalsAdapter::check(GameSession& session) {
  if (!rules_ || desc_.story == nullptr) {
    return;
  }
  const game::partner::PartnerState& partner = session.partner().state();
  const game::partner::StatBlock& base = partner.stats.base;
  const game::story::MedalFacts facts{
      .knowsMove =
          [this](std::int32_t move) {
            return desc_.battle != nullptr && desc_.battle->knowsMoveNumber(move).value_or(false);
          },
      .stats = {.hp = base[Stat::Hp],
                .mp = base[Stat::Mp],
                .offense = base[Stat::Offense],
                .defense = base[Stat::Defense],
                .speed = base[Stat::Speed],
                .brain = base[Stat::Brain]},
      .money = desc_.inventory != nullptr ? desc_.inventory->inventory().money() : 0,
      .year = session.clock().time().year,
      .raised = [this](std::int32_t species) { return desc_.story->raised(species); },
      .fishCaught = partner.evolution.fishCaught,
      .cardCopies =
          [this](std::int32_t card) {
            return desc_.cards != nullptr ? desc_.cards->copies(card) : 0;
          },
  };
  auto earned = game::story::checkMedalConditions(desc_.story->state(), rules_->medals, facts);
  if (!earned) {
    core::logWarning(
        "{}", core::formatMessage(progress_messages::MedalCheckFailed, earned.error().message));
    return;
  }
  if (earned->medals.empty()) {
    return;
  }
  awardPending_ = true;
  if (earned->tamerLevels != 0) {
    // tamer.c:769: the raised medal adds a level with no notice of its own, up to the limit.
    session.setTamerLevel(session.tamer().level + earned->tamerLevels);
  }
}

} // namespace dwe::session
