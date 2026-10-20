#include "save_sections.hpp"

#include <dwe/assets/yaml_reader.hpp>
#include <dwe/game/partner/state_yaml.hpp>
#include <dwe/session/messages.hpp>

#include <charconv>
#include <cstdint>
#include <format>
#include <string>
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
constexpr std::string_view MapKey = "map";
constexpr std::string_view PositionKey = "position";
constexpr std::string_view YawKey = "yaw";
constexpr std::string_view PartnerKey = "partner";
constexpr std::size_t PositionAxes = 3;

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
    const auto pending = parsed<double>(reader, reader.field(state, PendingFramesKey), messages::NotAFraction);
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
    return node;
}

core::Result<void> TamerSave::read(const YamlNode& state) {
    YamlReader reader;
    if (!reader.expectKeys(state, messages::TamerStateText, {LevelKey})) {
        return std::unexpected{reader.error(messages::TamerStateText)};
    }
    const TamerState tamer{.level = reader.integer<std::int32_t>(reader.field(state, LevelKey))};
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
    return node;
}

core::Result<void> RandomSave::read(const YamlNode& state) {
    YamlReader reader;
    if (!reader.expectKeys(state, messages::RandomStateText, {PartnerKey})) {
        return std::unexpected{reader.error(messages::RandomStateText)};
    }
    const auto partner = parsed<std::uint64_t>(reader, reader.field(state, PartnerKey), messages::NotARandomState);
    if (auto done = reader.finish(true, messages::RandomStateText); !done) {
        return std::unexpected{done.error()};
    }
    state_->partner.restoreRandomState(partner);
    return {};
}

core::Result<void> SessionSerializers::addTo(game::save::SaveSections& sections) {
    for (auto [name, serializer] : {std::pair<std::string_view, game::save::SectionSerializer*>{ClockName, &clock},
                                    {PartnerName, &partner},
                                    {TamerName, &tamer},
                                    {FieldName, &field},
                                    {RandomName, &random}}) {
        if (auto added = sections.add(std::string{name}, *serializer); !added) {
            return added;
        }
    }
    return {};
}

} // namespace dwe::session::sections
