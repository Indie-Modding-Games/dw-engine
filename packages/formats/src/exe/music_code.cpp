#include <dwe/formats/exe/constant_switch.hpp>
#include <dwe/formats/exe/music_code.hpp>

#include <array>
#include <string_view>
#include <utility>

#include "messages.hpp"

namespace dwe::formats::exe {

namespace {

constexpr std::size_t FontValue = 0;
constexpr std::size_t TrackValue = 1;
constexpr std::int32_t ByteLimit = 0xFF;

// One located constant as a byte, or why it is not one.
core::Result<std::uint8_t> constantByte(const MemoryImage& image, std::uint32_t address,
                                        std::string_view name) {
  auto value = readCodeConstant(image, address);
  if (!value) {
    return core::fail(messages::formatMessage(messages::InCode, name, value.error().message));
  }
  if (*value < 0 || *value > ByteLimit) {
    return core::fail(messages::formatMessage(messages::CodeConstantRange, name, *value));
  }
  return static_cast<std::uint8_t>(*value);
}

core::Result<std::vector<FieldMusicOverride>> readFieldOverrides(const MemoryImage& image,
                                                                 const MusicCodeLayout& layout) {
  std::vector<FieldMusicOverride> overrides;
  if (!layout.fieldOverrides.located()) {
    return overrides;
  }
  for (std::uint32_t mode = layout.firstOverrideMode; mode <= layout.lastOverrideMode; ++mode) {
    auto values = evaluateConstantSwitch(image, layout.fieldOverrides, mode);
    if (!values) {
      return core::fail(
          messages::formatMessage(messages::MusicModeUnread, mode, values.error().message));
    }
    overrides.push_back({.mode = static_cast<std::uint8_t>(mode),
                         .font = static_cast<std::uint8_t>((*values)[FontValue]),
                         .track = static_cast<std::uint8_t>((*values)[TrackValue])});
  }
  return overrides;
}

core::Result<std::optional<BattleMusicCode>> readBattleMusic(const MemoryImage& image,
                                                             const BattleMusicCodeLayout& layout) {
  if (!layout.located()) {
    return std::optional<BattleMusicCode>{};
  }
  const std::array<std::pair<std::uint32_t, std::string_view>, 7> constants{{
      {layout.font, "battle font"},
      {layout.bossType, "boss type"},
      {layout.bossTrack, "boss track"},
      {layout.scriptedTrack, "scripted battle track"},
      {layout.defaultTrack, "default battle track"},
      {layout.firstEnemyType, "first enemy music type"},
      {layout.endEnemyType, "end enemy music type"},
  }};
  std::array<std::uint8_t, 7> read{};
  for (std::size_t i = 0; i < constants.size(); ++i) {
    auto value = constantByte(image, constants[i].first, constants[i].second);
    if (!value) {
      return std::unexpected{value.error()};
    }
    read[i] = *value;
  }
  return BattleMusicCode{.font = read[0],
                         .bossType = read[1],
                         .bossTrack = read[2],
                         .scriptedTrack = read[3],
                         .defaultTrack = read[4],
                         .firstEnemyType = read[5],
                         .endEnemyType = read[6]};
}

} // namespace

core::Result<MusicCode> readMusicCode(const MemoryImage& image, const ExecutableLayout& layout) {
  MusicCode code;
  auto overrides = readFieldOverrides(image, layout.musicCode);
  if (!overrides) {
    return std::unexpected{overrides.error()};
  }
  code.fieldOverrides = std::move(*overrides);
  auto battle = readBattleMusic(image, layout.musicCode.battle);
  if (!battle) {
    return std::unexpected{battle.error()};
  }
  code.battle = *battle;
  return code;
}

} // namespace dwe::formats::exe
