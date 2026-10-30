#include "save_sections.hpp"

#include <dwe/assets/yaml_reader.hpp>
#include <dwe/game/partner/state_yaml.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/slot_names.hpp>

#include <charconv>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace dwe::session::sections {

namespace {

using assets::YamlReader;
using core::YamlNode;
using messages::formatMessage;

constexpr std::string_view ElapsedFramesKey = "elapsed_frames";
constexpr std::string_view PendingFramesKey = "pending_frames";
constexpr std::string_view LevelKey = "level";
constexpr std::string_view RaisedKey = "raised";
constexpr std::string_view MeritKey = "merit";
constexpr std::string_view StartedBattlesKey = "started_battles";
constexpr std::string_view FledBattlesKey = "fled_battles";
constexpr std::string_view TournamentsWonKey = "tournaments_won";
constexpr std::string_view TournamentWinsKey = "tournament_wins";
constexpr std::string_view TournamentsLostKey = "tournaments_lost";
constexpr std::string_view MapKey = "map";
constexpr std::string_view PositionKey = "position";
constexpr std::string_view YawKey = "yaw";
constexpr std::string_view PartnerKey = "partner";
constexpr std::string_view SystemsKey = "session";
constexpr std::size_t PositionAxes = 3;
// The partner section's species key, as game/partner writes it (state_yaml.cpp).
constexpr std::string_view SpeciesKey = "species";

// Numbers are written in their shortest form that reads back exactly.
template <typename T>
YamlNode scalarOf(T value) {
  return YamlNode::scalar(std::format("{}", value));
}

YamlNode positionOf(const glm::vec3& position) {
  YamlNode axes = YamlNode::sequence();
  axes.append(scalarOf(position.x));
  axes.append(scalarOf(position.y));
  axes.append(scalarOf(position.z));
  return axes;
}

// A number parsed whole by std::from_chars, or the reader fails at the node.
template <typename T>
T parsed(YamlReader& reader, const YamlNode& node, std::string_view problem) {
  T value{};
  if (!reader.ok()) {
    return value;
  }
  const std::string text = reader.text(node);
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || text.empty()) {
    reader.fail(node, formatMessage(problem, text));
  }
  return value;
}

// The partner section's versions: the chain reaches game::partner::PartnerStateVersion.
game::save::MigrationChain partnerMigrations() {
  game::save::MigrationChain chain;
  chain.add(game::partner::addVirusBackupSpecies);
  return chain;
}

// Version 1 kept only the partner's random state; the systems' numbers go on from the same point.
core::Result<YamlNode> addSystemsRandom(YamlNode state) {
  const YamlNode* partner = state.find(PartnerKey);
  if (partner == nullptr) {
    return core::fail(formatMessage(messages::MissingRandomKey, PartnerKey));
  }
  const YamlNode systems = *partner;
  state.set(std::string{SystemsKey}, systems);
  return state;
}

// The random section's versions: 2 adds the systems' random state.
game::save::MigrationChain randomMigrations() {
  game::save::MigrationChain chain;
  chain.add(addSystemsRandom);
  return chain;
}

// A text of a section's state, or empty if the save has no such section or key: a summary is shown,
// never trusted.
std::string sectionText(const game::save::SaveFile& file, std::string_view section,
                        std::string_view key) {
  const auto found = file.sections.find(section);
  if (found == file.sections.end() || !found->second.state.isMapping()) {
    return {};
  }
  const YamlNode* value = found->second.state.find(key);
  return value != nullptr && value->isScalar() ? value->text() : std::string{};
}

} // namespace

core::Result<YamlNode> ClockSave::write() const {
  YamlNode node = YamlNode::mapping();
  node.set(std::string{ElapsedFramesKey}, scalarOf(state_->clock.elapsedFrames()));
  node.set(std::string{PendingFramesKey}, scalarOf(state_->clock.pendingFrames()));
  return node;
}

core::Result<void> ClockSave::read(const YamlNode& state) {
  YamlReader reader;
  if (!reader.expectKeys(state, messages::ClockStateText, {ElapsedFramesKey, PendingFramesKey})) {
    return std::unexpected{reader.error(messages::ClockStateText)};
  }
  const auto elapsed = reader.integer<std::int64_t>(reader.field(state, ElapsedFramesKey));
  const auto pending =
      parsed<double>(reader, reader.field(state, PendingFramesKey), messages::NotAFraction);
  if (auto done = reader.finish(true, messages::ClockStateText); !done) {
    return std::unexpected{done.error()};
  }
  state_->clock.restore(elapsed, pending);
  return {};
}

core::Result<YamlNode> PartnerSave::write() const {
  return game::partner::toYaml(state_->partner.state());
}

core::Result<void> PartnerSave::read(const YamlNode& state) {
  auto partner = game::partner::fromYaml(state);
  if (!partner) {
    return std::unexpected{partner.error()};
  }
  return state_->partner.restore(std::move(*partner));
}

core::Result<YamlNode> TamerSave::write() const {
  YamlNode node = YamlNode::mapping();
  node.set(std::string{LevelKey}, scalarOf(state_->tamer.level));
  node.set(std::string{RaisedKey}, scalarOf(state_->tamer.raised));
  const TamerRecord& record = state_->tamer.record;
  node.set(std::string{MeritKey}, scalarOf(record.merit));
  node.set(std::string{StartedBattlesKey}, scalarOf(record.startedBattles));
  node.set(std::string{FledBattlesKey}, scalarOf(record.fledBattles));
  node.set(std::string{TournamentsWonKey}, scalarOf(record.tournamentsWon));
  node.set(std::string{TournamentWinsKey}, scalarOf(record.tournamentWins));
  node.set(std::string{TournamentsLostKey}, scalarOf(record.tournamentsLost));
  return node;
}

core::Result<void> TamerSave::read(const YamlNode& state) {
  YamlReader reader;
  // The record and the raised count came after the level: a save without the record starts it at
  // 0, one without the count at the first partner's 1.
  if (!reader.expectKeys(state, messages::TamerStateText, {LevelKey},
                         {RaisedKey, MeritKey, StartedBattlesKey, FledBattlesKey, TournamentsWonKey,
                          TournamentWinsKey, TournamentsLostKey})) {
    return std::unexpected{reader.error(messages::TamerStateText)};
  }
  const auto count = [&reader, &state](std::string_view key) {
    return reader.optionalInteger<std::int32_t>(state, key).value_or(0);
  };
  const TamerState tamer{.level = reader.integer<std::int32_t>(reader.field(state, LevelKey)),
                         .raised =
                             reader.optionalInteger<std::int32_t>(state, RaisedKey).value_or(1),
                         .record = {.merit = count(MeritKey),
                                    .startedBattles = count(StartedBattlesKey),
                                    .fledBattles = count(FledBattlesKey),
                                    .tournamentsWon = count(TournamentsWonKey),
                                    .tournamentWins = count(TournamentWinsKey),
                                    .tournamentsLost = count(TournamentsLostKey)}};
  auto done = reader.finish(tamer, messages::TamerStateText);
  if (!done) {
    return std::unexpected{done.error()};
  }
  state_->tamer = *done;
  return {};
}

core::Result<YamlNode> FieldSave::write() const {
  YamlNode node = YamlNode::mapping();
  if (const std::optional<FieldPlace> place = state_->currentPlace()) {
    node.set(std::string{MapKey}, YamlNode::scalar(place->map));
    node.set(std::string{PositionKey}, positionOf(place->spawn.position));
    node.set(std::string{YawKey}, scalarOf(place->spawn.yaw));
  }
  return node;
}

core::Result<void> FieldSave::read(const YamlNode& state) {
  YamlReader reader;
  if (state.isMapping() && state.entries().empty()) {
    state_->savedPlace.reset();
    return {};
  }
  if (!reader.expectKeys(state, messages::FieldStateText, {MapKey, PositionKey, YawKey})) {
    return std::unexpected{reader.error(messages::FieldStateText)};
  }
  FieldPlace place;
  const YamlNode& map = reader.field(state, MapKey);
  place.map = reader.ok() ? reader.text(map) : std::string{};
  const auto axes = reader.sequence(reader.field(state, PositionKey), PositionKey, PositionAxes);
  if (reader.ok()) {
    place.spawn.position = {reader.number(axes[0]), reader.number(axes[1]), reader.number(axes[2])};
  }
  place.spawn.yaw = reader.number(reader.field(state, YawKey));
  auto done = reader.finish(std::move(place), messages::FieldStateText);
  if (!done) {
    return std::unexpected{done.error()};
  }
  state_->savedPlace = std::move(*done);
  return {};
}

core::Result<YamlNode> RandomSave::write() const {
  YamlNode node = YamlNode::mapping();
  node.set(std::string{PartnerKey}, scalarOf(state_->partner.randomState()));
  node.set(std::string{SystemsKey}, scalarOf(state_->random.state()));
  return node;
}

core::Result<void> RandomSave::read(const YamlNode& state) {
  YamlReader reader;
  if (!reader.expectKeys(state, messages::RandomStateText, {PartnerKey, SystemsKey})) {
    return std::unexpected{reader.error(messages::RandomStateText)};
  }
  const auto partner =
      parsed<std::uint64_t>(reader, reader.field(state, PartnerKey), messages::NotARandomState);
  const auto systems =
      parsed<std::uint64_t>(reader, reader.field(state, SystemsKey), messages::NotARandomState);
  if (auto done = reader.finish(true, messages::RandomStateText); !done) {
    return std::unexpected{done.error()};
  }
  state_->partner.restoreRandomState(partner);
  state_->random.restoreState(systems);
  return {};
}

core::Result<void> SessionSerializers::addTo(game::save::SaveSections& sections) {
  for (auto& [name, serializer, migrations] :
       {SectionEntry{.name = ClockName, .serializer = &clock, .migrations = {}},
        SectionEntry{
            .name = PartnerName, .serializer = &partner, .migrations = partnerMigrations()},
        SectionEntry{.name = TamerName, .serializer = &tamer, .migrations = {}},
        SectionEntry{.name = FieldName, .serializer = &field, .migrations = {}},
        SectionEntry{
            .name = RandomName, .serializer = &random, .migrations = randomMigrations()}}) {
    if (auto added = sections.add(std::string{name}, *serializer, migrations); !added) {
      return added;
    }
  }
  return {};
}

} // namespace dwe::session::sections

namespace dwe::session {

game::save::SaveSummary summaryFromSections(const game::save::SaveFile& file) {
  return {.slot = {},
          .tamer = {},
          .species = sections::sectionText(file, sections::PartnerName, sections::SpeciesKey),
          .nickname = {},
          .map = sections::sectionText(file, sections::FieldName, sections::MapKey)};
}

} // namespace dwe::session
