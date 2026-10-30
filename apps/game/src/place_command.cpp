#include "place_command.hpp"

#include <dwe/assets/map_document.hpp>

#include <array>
#include <charconv>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <system_error>
#include <vector>

namespace dwe::app {

namespace {

constexpr std::string_view NpcPrefix = "npc:";

std::optional<std::int32_t> integerOf(std::string_view text) {
  std::int32_t value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

} // namespace

core::Result<Placement> placementFromText(std::string_view text) {
  std::vector<std::string_view> words;
  for (const auto word : std::views::split(text, ' ')) {
    if (!std::ranges::empty(word)) {
      words.emplace_back(std::ranges::begin(word), std::ranges::end(word));
    }
  }
  const auto usage = [&] {
    return core::fail(
        std::format("'{}': expected '<player|partner|npc:<id>> <x> <z> <rotation>'", text));
  };
  if (words.size() != 4) {
    return usage();
  }
  Placement placement;
  if (words[0] == "player") {
    placement.who = Placement::Who::Player;
  } else if (words[0] == "partner") {
    placement.who = Placement::Who::Partner;
  } else if (words[0].starts_with(NpcPrefix)) {
    const auto npc = integerOf(words[0].substr(NpcPrefix.size()));
    if (!npc) {
      return usage();
    }
    placement.who = Placement::Who::Npc;
    placement.npc = *npc;
  } else {
    return usage();
  }
  std::array<std::int32_t, 3> numbers{};
  for (std::size_t index = 0; index < numbers.size(); ++index) {
    const auto number = integerOf(words[index + 1]);
    if (!number) {
      return usage();
    }
    numbers[index] = *number;
  }
  // The original's Z points into the screen, the world's out of it (core::fromOriginalAxes).
  placement.ground = {static_cast<float>(numbers[0]), -static_cast<float>(numbers[1])};
  placement.yaw = assets::yawFromOriginal(numbers[2]);
  return placement;
}

} // namespace dwe::app
