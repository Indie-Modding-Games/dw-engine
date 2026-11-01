#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/story/medals.hpp>
#include <dwe/game/story/story_runner.hpp>
#include <dwe/ruleset/species_extra.hpp>
#include <dwe/session/cards_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/medals_adapter.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_game_orders.hpp>
#include <dwe/session/tournament_adapter.hpp>

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::session {

namespace {

using game::tournament::TournamentSectionName;

constexpr std::string_view StadiumData = "data/stadium";
constexpr std::string_view TournamentData = "data/tournaments";
// The cup variable's value with no cup picked (initTournamentSchedule: writePStat(3, 0xFF)).
constexpr std::uint8_t NoCup = 0xFF;

core::Result<core::YamlNode> readYaml(const assets::VirtualFileSystem& files, std::string_view id) {
  auto parsed = assets::AssetId::parse(id);
  if (!parsed) {
    return std::unexpected{parsed.error()};
  }
  auto bytes = files.read(*parsed);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  return core::parseYaml(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
}

std::int32_t limitOf(const StoryValueRules& values, ScriptStat stat) {
  return values.statLimits.at(static_cast<std::size_t>(stat));
}

// script_value.c: enforceStatsLimits.
std::int32_t capped(std::int64_t value, std::int32_t limit) {
  return static_cast<std::int32_t>(std::clamp<std::int64_t>(value, 0, limit));
}

} // namespace

core::Result<void> TournamentAdapter::readRules(const ruleset::Ruleset& rules) {
  rules_.reset();
  bracket_.reset();
  bracketLayout_.reset();
  bracketRules_.reset();
  stadiumGrades_.clear();
  extraPrizes_.clear();
  medalRules_.reset();
  const ruleset::Section* section = rules.section(TournamentSectionName);
  if (section == nullptr || desc_.files == nullptr) {
    return {};
  }
  auto stadium = readYaml(*desc_.files, StadiumData);
  if (!stadium) {
    core::logWarning("[port:tournament] {} is not read ({}): no tournaments", StadiumData,
                     stadium.error().message);
    return {};
  }
  auto read = game::tournament::parseTournamentRules(*section, *stadium);
  if (!read) {
    return std::unexpected{read.error()};
  }
  rules_ = std::move(*read);
  auto arena = game::tournament::parseArenaRules(*section);
  if (!arena) {
    return std::unexpected{arena.error()};
  }
  arena_ = std::move(*arena);
  auto bracketRules = game::tournament::parseBracketRules(*section);
  if (!bracketRules) {
    return std::unexpected{bracketRules.error()};
  }
  auto prizes = game::tournament::parseExtraPrizes(*section);
  if (!prizes) {
    return std::unexpected{prizes.error()};
  }
  extraPrizes_ = std::move(*prizes);
  // A ruleset's species patches add theirs (the hidden content's), so it need not copy this
  // section.
  if (const ruleset::Section* extra = rules.section(ruleset::SpeciesExtraSection)) {
    auto more = game::tournament::parseExtraPrizes(*extra);
    if (!more) {
      return std::unexpected{more.error()};
    }
    extraPrizes_.insert(extraPrizes_.end(), more->begin(), more->end());
  }
  auto medal = game::tournament::parseTournamentMedalRules(*section);
  if (!medal) {
    return std::unexpected{medal.error()};
  }
  medalRules_ = std::move(*medal);
  // An extraction made before dwx read the bracket's tables has none: the cups are fought without
  // the board.
  if (auto layout = game::tournament::parseBracketLayout(*stadium); !layout) {
    core::logWarning("[port:tournament] STD_initializeBracket (std_main.c): {}; no bracket",
                     layout.error().message);
  } else if (*layout && *bracketRules) {
    bracketLayout_ = std::move(**layout);
    bracketRules_ = std::move(**bracketRules);
    stadiumGrades_ = game::tournament::parseStadiumGrades(*stadium);
  }
  schedule_.reset();
  table_.reset();
  auto schedule = game::tournament::parseScheduleRules(*section);
  if (!schedule) {
    return std::unexpected{schedule.error()};
  }
  auto tournaments = readYaml(*desc_.files, TournamentData);
  auto table =
      tournaments
          ? game::tournament::parseTournamentTable(*tournaments)
          : core::Result<game::tournament::TournamentTable>{std::unexpected{tournaments.error()}};
  if (table) {
    schedule_ = std::move(*schedule);
    table_ = std::move(*table);
  } else {
    core::logWarning("[port:tournament] {} is not read ({}): no registration", TournamentData,
                     table.error().message);
  }
  if (const ruleset::Section* values = rules.section(StoryValuesSection); values != nullptr) {
    auto limits = readStoryValueRules(*values);
    if (!limits) {
      return std::unexpected{limits.error()};
    }
    wonLimit_ = limitOf(*limits, ScriptStat::TournamentsWon);
    winsLimit_ = limitOf(*limits, ScriptStat::TournamentWins);
    lostLimit_ = limitOf(*limits, ScriptStat::TournamentsLost);
  }
  return {};
}

// tournament.c: getCupDataJumpTable reads the active map's script.
core::Result<std::pair<std::uint16_t, core::YamlNode>> TournamentAdapter::mapListing() const {
  const game::story::StoryRunner* runner = desc_.story != nullptr ? desc_.story->runner() : nullptr;
  const std::optional<std::uint16_t> script =
      runner != nullptr ? runner->mapScript() : std::nullopt;
  if (!script) {
    return core::fail("no map script is active");
  }
  auto listing = readYaml(*desc_.files, std::format("script/dg/{}", *script));
  if (!listing) {
    return std::unexpected{listing.error()};
  }
  return std::pair{*script, std::move(*listing)};
}

const assets::DigimonRow* TournamentAdapter::rowOfNumber(std::int32_t number) const {
  const assets::DigimonTable* table =
      desc_.battle != nullptr ? desc_.battle->digimonTable() : nullptr;
  if (table == nullptr) {
    return nullptr;
  }
  const auto found = std::ranges::find(table->digimon, number, &assets::DigimonRow::index);
  return found != table->digimon.end() ? &*found : nullptr;
}

core::Result<void> TournamentAdapter::begin(GameSession& session) {
  bracket_.reset();
  if (!rules_ || desc_.story == nullptr || desc_.battle == nullptr ||
      desc_.battle->digimonTable() == nullptr) {
    return core::fail("there are no tournament rules");
  }
  const game::tournament::TournamentRules& rules = *rules_;
  game::story::StoryState& story = desc_.story->state();
  auto cup = story.variable(rules.cupVariable);
  if (!cup) {
    return std::unexpected{cup.error()};
  }
  auto listing = mapListing();
  if (!listing) {
    return std::unexpected{listing.error()};
  }
  auto data = game::tournament::cupDataOf(listing->second, *cup);
  if (!data) {
    return std::unexpected{data.error()};
  }
  const assets::DigimonRow* partner =
      desc_.battle->digimonTable()->find(session.partner().state().species);
  if (partner == nullptr) {
    return core::fail(
        std::format("the partner's species {} is unknown", session.partner().state().species));
  }
  const auto resident = [&](std::int32_t species) {
    const auto set = story.trigger(rules.recruitTriggerBase + species);
    return set && *set;
  };
  ruleset::Random& random = session.random();
  auto rivals = game::tournament::drawRivals(rules, *cup, *data, resident, partner->index, random);
  if (!rivals) {
    return std::unexpected{rivals.error()};
  }
  const auto level = [this](std::int32_t species) {
    const assets::DigimonRow* row = rowOfNumber(species);
    return row != nullptr ? row->level : 0;
  };
  bracket_ = game::tournament::drawBracket(rules, *rivals, partner->index, level, random);
  cup_ = *cup;
  round_ = 0;
  wins_ = 0;
  lost_ = false;
  core::logInfo("[port:tournament] startTournament (utils2.c:577): cup {} in slot {}, rivals "
                "{}, {} and {}",
                cup_, bracket_->playerSlot, bracket_->opponents[0], bracket_->opponents[1],
                bracket_->opponents[2]);
  return {};
}

core::Result<TournamentAdapter::ArenaStage>
TournamentAdapter::arenaStage(std::string_view map) const {
  if (!arena_ || desc_.files == nullptr) {
    return core::fail("no arena rules");
  }
  const std::size_t index = game::tournament::arenaFor(*arena_, map);
  const game::tournament::Arena& arena = arena_->arenas.at(index);
  auto id = assets::AssetId::parse(arena.collision);
  if (!id) {
    return std::unexpected{id.error()};
  }
  auto bytes = desc_.files->read(*id);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  auto collision = assets::parseMapCollision(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  if (!collision) {
    return std::unexpected{collision.error()};
  }
  core::logDebug("[port:tournament] STD_initializeTournament (std_setup.c:243): arena {} ({}) "
                 "for map {}",
                 index, arena.model, map);
  return ArenaStage{.model = arena.model,
                    .collision = std::move(*collision),
                    .start = game::tournament::arenaStart(arena),
                    .walkIn = arena.walkIn,
                    .partnerYaw = arena_->partnerYaw,
                    .rivalYaw = arena_->rivalYaw,
                    .camera = arena_->camera};
}

core::Result<BattleRequest> TournamentAdapter::matchRequest(GameSession& session,
                                                            game::battle::Point partner,
                                                            game::battle::Point tamer,
                                                            std::string map,
                                                            game::battle::Point rival) {
  if (!rules_ || !bracket_ || round_ >= game::tournament::Rounds || lost_) {
    return core::fail("no tournament match is due");
  }
  const std::int32_t number = bracket_->opponents.at(round_);
  const assets::DigimonRow* row = rowOfNumber(number);
  const assets::MoveTable* moves = desc_.battle->moveTable();
  if (row == nullptr || moves == nullptr) {
    return core::fail(std::format("the rival species {} is unknown", number));
  }
  game::tournament::RivalSpecies species{.level = row->level, .moves = {}};
  for (std::size_t slot = 0; slot < row->moves.size(); ++slot) {
    if (const std::optional<std::string>& id = row->moves.at(slot)) {
      if (const assets::MoveEntry* move = moves->find(*id)) {
        species.moves.at(slot) = game::tournament::SlotMove{
            .number = move->index, .mpCost = move->mpCost, .range = move->range};
      }
    }
  }
  ruleset::Random& random = session.random();
  const game::tournament::Rival made = game::tournament::makeRival(*rules_, cup_, species, random);
  game::battle::Combatant enemy = game::battle::combatantOf(*row);
  enemy.stats = {.maxHp = made.hp,
                 .maxMp = made.mp,
                 .offense = made.offense,
                 .defense = made.defense,
                 .speed = made.speed,
                 .brain = made.brain};
  enemy.hp = made.hp;
  enemy.mp = made.mp;
  for (std::size_t place = 0; place < made.slots.size(); ++place) {
    if (const std::optional<std::size_t> slot = made.slots.at(place)) {
      enemy.moves.at(place) = row->moves.at(*slot);
    }
  }
  enemy.movePriorities = rules_->movePriorities;
  enemy.focusesPartner = true;
  enemy.position = rival;
  BattleRequest request;
  request.enemies.push_back(std::move(enemy));
  request.scripted = true;
  request.map = std::move(map);
  request.tamer = tamer;
  request.partner = partner;
  request.kind = game::battle::BattleKind::Tournament;
  request.timeLimit = rules_->timeLimit;
  return request;
}

bool TournamentAdapter::matchOver(GameSession& session, game::battle::Outcome outcome) {
  if (!rules_ || !bracket_) {
    return false;
  }
  // STD_deinitializeCombat: a fifth of the maximums back, whatever the outcome.
  game::partner::Partner& partner = session.partner();
  game::partner::PartnerStats stats = partner.state().stats;
  const std::int32_t maxHp = stats.base[game::partner::Stat::Hp];
  const std::int32_t maxMp = stats.base[game::partner::Stat::Mp];
  stats.currentHp =
      std::min(std::max(stats.currentHp, 0) + (maxHp / rules_->recoveryDivisor), maxHp);
  stats.currentMp =
      std::min(std::max(stats.currentMp, 0) + (maxMp / rules_->recoveryDivisor), maxMp);
  partner.setStats(stats);
  ++round_;
  if (outcome == game::battle::Outcome::Victory) {
    ++wins_;
  } else {
    lost_ = true;
  }
  core::logInfo("[port:tournament] STD_tournamentMain (std_setup.c:561): round {} {}", round_,
                outcome == game::battle::Outcome::Victory ? "won" : "lost");
  return !lost_ && round_ < game::tournament::Rounds;
}

core::Result<std::int32_t> TournamentAdapter::finish(GameSession& session) {
  if (!rules_ || !bracket_) {
    return core::fail("no tournament runs");
  }
  const game::tournament::TournamentRules& rules = *rules_;
  game::story::StoryState& story = desc_.story->state();
  if (auto cleared = story.setTrigger(rules.registeredTrigger, false); !cleared) {
    return std::unexpected{cleared.error()};
  }
  TamerRecord record = session.tamer().record;
  if (wins_ == game::tournament::winsNeeded(rules, cup_)) {
    record.tournamentsWon = capped(std::int64_t{record.tournamentsWon} + 1, wonLimit_);
    if (auto won = story.setTrigger(rules.wonTriggerBase + cup_, true); !won) {
      return std::unexpected{won.error()};
    }
    giveExtraPrizes(cup_);
  } else {
    ++record.tournamentsLost;
  }
  record.tournamentWins = capped(std::int64_t{record.tournamentWins} + wins_, winsLimit_);
  record.tournamentsLost = capped(record.tournamentsLost, lostLimit_);
  session.setTamerRecord(record);
  if (auto written = story.setVariable(rules.resultVariable, static_cast<std::uint8_t>(wins_));
      !written) {
    return std::unexpected{written.error()};
  }
  core::logInfo("[port:tournament] startTournament (utils2.c:671): cup {} over with {} wins", cup_,
                wins_);
  bracket_.reset();
  return wins_;
}

bool TournamentAdapter::registered() const {
  if (!rules_ || desc_.story == nullptr) {
    return false;
  }
  const auto set = desc_.story->state().trigger(rules_->registeredTrigger);
  return set && *set;
}

core::Result<game::tournament::Schedule> TournamentAdapter::openSchedule(GameSession& session) {
  if (!rules_ || !schedule_ || !table_ || desc_.story == nullptr || desc_.battle == nullptr ||
      desc_.battle->digimonTable() == nullptr) {
    return core::fail("there are no registration rules");
  }
  const game::tournament::TournamentRules& rules = *rules_;
  const game::tournament::ScheduleRules& schedule = *schedule_;
  game::story::StoryState& story = desc_.story->state();
  if (auto cleared = story.setVariable(rules.cupVariable, NoCup); !cleared) {
    return std::unexpected{cleared.error()};
  }
  auto listing = mapListing();
  if (!listing) {
    return std::unexpected{listing.error()};
  }
  const assets::DigimonRow* partner =
      desc_.battle->digimonTable()->find(session.partner().state().species);
  if (partner == nullptr) {
    return core::fail(
        std::format("the partner's species {} is unknown", session.partner().state().species));
  }
  // Every cup the schedule names, read once.
  std::map<std::int32_t, std::optional<game::tournament::CupData>> cups;
  cupTexts_.clear();
  const auto cupOf = [&](std::int32_t cup) -> const game::tournament::CupData* {
    auto [found, added] = cups.try_emplace(cup);
    if (added) {
      auto data = game::tournament::cupDataOf(listing->second, cup);
      if (data) {
        if (data->textAt) {
          cupTexts_[cup] = std::format("story_{:03}.text.{:04x}", listing->first, *data->textAt);
        }
        found->second = std::move(*data);
      } else {
        core::logWarning("[port:tournament] getCupDataJumpTable (tournament.c:146): {}",
                         data.error().message);
      }
    }
    return found->second ? &*found->second : nullptr;
  };
  const std::vector<std::int32_t> local =
      game::tournament::sectionList(listing->second, schedule.localListSection);
  const auto trigger = [&story](std::int32_t number) {
    const auto set = story.trigger(number);
    return set && *set;
  };
  const game::tournament::CupOpening opening{.trigger = trigger, .localList = local};
  const auto lists = [&](std::int32_t cup, auto member) {
    const game::tournament::CupData* data = cupOf(cup);
    return data != nullptr && std::ranges::contains(data->*member, partner->index);
  };
  const ruleset::GameTime now = session.clock().time();
  const game::tournament::ScheduleFacts facts{
      .day = now.day,
      .minuteOfDay = (now.hour * 60) + now.minute,
      .open =
          [&](std::int32_t cup) {
            const game::tournament::CupData* data = cupOf(cup);
            return data != nullptr && game::tournament::cupOpen(rules, schedule, *data, opening);
          },
      .eligible =
          [&](std::int32_t cup) { return lists(cup, &game::tournament::CupData::eligible); },
      .overlevel =
          [&](std::int32_t cup) { return lists(cup, &game::tournament::CupData::overlevel); }};
  game::tournament::Schedule built = game::tournament::buildSchedule(schedule, *table_, facts);
  core::logDebug("[port:tournament] initTournamentSchedule (dget.c:664): day {}, {:02}:{:02}",
                 now.day, now.hour, now.minute);
  return built;
}

std::string TournamentAdapter::grade(std::int32_t cup) const {
  if (!table_ || cup < 0 || static_cast<std::size_t>(cup) >= table_->grades.size()) {
    return {};
  }
  return table_->grades[static_cast<std::size_t>(cup)];
}

std::string TournamentAdapter::cupTextKey(std::int32_t cup) const {
  const auto found = cupTexts_.find(cup);
  return found != cupTexts_.end() ? found->second : std::string{};
}

std::string TournamentAdapter::lineKey(std::int32_t line) {
  return schedule_ ? sectionLineKey(schedule_->linesSection, line) : std::string{};
}

std::string TournamentAdapter::sectionLineKey(std::int32_t section, std::int32_t line) {
  if (desc_.files == nullptr) {
    return {};
  }
  if (!lineTables_) {
    auto tables = readMapHeadLineTables(*desc_.files);
    if (!tables) {
      core::logWarning("[port:tournament] showMapHeadTextbox (dget.c:704): {}",
                       tables.error().message);
      return {};
    }
    lineTables_ = std::move(*tables);
  }
  const auto table = lineTables_->find(section);
  const auto at = static_cast<std::size_t>(line);
  if (table == lineTables_->end() || line < 0 || at >= table->second.size()) {
    return {};
  }
  return table->second[at];
}

core::Result<void> TournamentAdapter::chooseCup(std::int32_t cup) {
  if (!rules_ || desc_.story == nullptr) {
    return core::fail("there are no tournament rules");
  }
  return desc_.story->state().setVariable(rules_->cupVariable, static_cast<std::uint8_t>(cup));
}

core::Result<void> TournamentAdapter::markOverlevel(bool overlevel) {
  if (!schedule_ || desc_.story == nullptr) {
    return core::fail("there are no registration rules");
  }
  return desc_.story->state().setTrigger(schedule_->overlevelTrigger, overlevel);
}

core::Result<void> TournamentAdapter::enter(GameSession& session, std::size_t column) {
  if (!rules_ || !schedule_ || desc_.story == nullptr || desc_.battle == nullptr ||
      desc_.battle->digimonTable() == nullptr) {
    return core::fail("there are no registration rules");
  }
  const game::tournament::ScheduleRules& schedule = *schedule_;
  game::story::StoryState& story = desc_.story->state();
  if (auto set = story.setTrigger(rules_->registeredTrigger, true); !set) {
    return set;
  }
  for (const std::int32_t trigger : schedule.registeredTriggers) {
    if (auto set = story.setTrigger(trigger, true); !set) {
      return set;
    }
  }
  const std::int32_t day = game::tournament::entryDay(schedule, session.clock().time().day, column);
  if (auto written = story.setVariable(schedule.dayVariable, static_cast<std::uint8_t>(day));
      !written) {
    return written;
  }
  const assets::DigimonRow* partner =
      desc_.battle->digimonTable()->find(session.partner().state().species);
  if (partner == nullptr) {
    return core::fail(
        std::format("the partner's species {} is unknown", session.partner().state().species));
  }
  if (auto written =
          story.setVariable(schedule.speciesVariable, static_cast<std::uint8_t>(partner->index));
      !written) {
    return written;
  }
  core::logInfo("[port:tournament] initTournamentSchedule (dget.c:722): entered for day {:#x}, "
                "species {}",
                day, partner->index);
  return {};
}

std::string TournamentAdapter::cardImage() const {
  return bracketRules_ ? game::tournament::bracketCardImage(*bracketRules_, stadiumGrades_, cup_)
                       : std::string{};
}

std::string TournamentAdapter::speciesId(std::int32_t number) const {
  const assets::DigimonRow* row = rowOfNumber(number);
  return row != nullptr ? row->id : std::string{};
}

bool TournamentAdapter::cupWon(std::int32_t cup) const {
  if (!rules_ || desc_.story == nullptr) {
    return false;
  }
  const auto set = desc_.story->state().trigger(rules_->wonTriggerBase + cup);
  return set && *set;
}

void TournamentAdapter::giveExtraPrizes(std::int32_t cup) {
  const std::vector<game::tournament::ExtraPrize> prizes = extraPrizes(cup);
  if (prizes.empty()) {
    return;
  }
  if (desc_.inventory == nullptr) {
    core::logWarning("[port:tournament] extra prizes of cup {}: the game has no bag", cup);
    return;
  }
  game::inventory::Inventory& bag = desc_.inventory->inventory();
  for (const game::tournament::ExtraPrize& prize : prizes) {
    if (prize.money != 0) {
      bag.addMoney(prize.money);
    }
    if (!prize.item) {
      continue;
    }
    const assets::ItemEntry* entry = desc_.inventory->items().findByOriginalIndex(*prize.item);
    if (entry == nullptr) {
      core::logWarning("[port:tournament] extra prize of cup {}: no item {}", cup, *prize.item);
      continue;
    }
    const game::inventory::AddResult added =
        bag.add(desc_.inventory->items().handleOf(*entry), prize.amount);
    core::logInfo("[port:tournament] extra prize of cup {}: item {} x{} (added {})", cup,
                  *prize.item, prize.amount, added.added);
  }
}

core::Result<std::optional<std::string>> TournamentAdapter::awardMedal(GameSession& session) {
  const std::optional<MedalsRules>* medalRules = medals_ != nullptr ? &medals_->rules() : nullptr;
  if (!medalRules_ || !table_ || desc_.story == nullptr || medalRules == nullptr ||
      !medalRules->has_value()) {
    return core::fail("there are no tournament medal rules");
  }
  game::story::StoryState& story = desc_.story->state();
  const game::story::MedalRules& rules = (*medalRules)->medals;
  const game::tournament::TournamentMedalFacts facts{
      .trigger =
          [&story](std::int32_t trigger) {
            const auto set = story.trigger(trigger);
            return set && *set;
          },
      .tournamentWins = session.tamer().record.tournamentWins,
      .cardCopies =
          [this](std::int32_t card) {
            return desc_.cards != nullptr ? desc_.cards->copies(card) : 0;
          },
      .hasMedal =
          [&story, &rules](std::int32_t medal) {
            const auto held = game::story::hasMedal(story, rules, medal);
            return held && *held;
          }};
  const std::optional<std::int32_t> result =
      game::tournament::tournamentMedal(*medalRules_, table_->medalIds, facts);
  if (!result) {
    return std::optional<std::string>{};
  }
  const std::int32_t medal = table_->medalIds.at(static_cast<std::size_t>(*result));
  if (auto unlocked = game::story::unlockMedal(story, rules, medal); !unlocked) {
    return std::unexpected{unlocked.error()};
  }
  core::logInfo("[port:tournament] scriptCheckTournamentMedal (tournament.c:254): result {}, "
                "medal {}",
                *result, medal);
  return std::optional<std::string>{sectionLineKey(medalRules_->linesSection, *result)};
}

} // namespace dwe::session
