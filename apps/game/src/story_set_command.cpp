#include "story_set_command.hpp"

#include <dwe/game/story/story_state.hpp>

#include <charconv>
#include <format>
#include <optional>
#include <ranges>
#include <system_error>
#include <vector>

namespace dwe::app {

namespace {

constexpr std::int32_t ByteLimit = 255;

std::optional<std::int32_t> integerOf(std::string_view text) {
  std::int32_t value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

} // namespace

core::Result<StoryWrite> storyWriteFromText(std::string_view text) {
  std::vector<std::string_view> words;
  for (const auto word : std::views::split(text, ' ')) {
    if (!std::ranges::empty(word)) {
      words.emplace_back(std::ranges::begin(word), std::ranges::end(word));
    }
  }
  const auto usage = [&] {
    return core::fail(
        std::format("'{}': expected 'trigger <n> on|off' or 'var <n> <0-255>'", text));
  };
  if (words.size() != 3) {
    return usage();
  }
  const auto number = integerOf(words[1]);
  if (!number) {
    return usage();
  }
  if (words[0] == "trigger" && (words[2] == "on" || words[2] == "off")) {
    return StoryWrite{
        .kind = StoryWrite::Kind::Trigger, .number = *number, .value = words[2] == "on" ? 1 : 0};
  }
  if (words[0] == "var") {
    const auto value = integerOf(words[2]);
    if (!value || *value < 0 || *value > ByteLimit) {
      return usage();
    }
    return StoryWrite{.kind = StoryWrite::Kind::Variable, .number = *number, .value = *value};
  }
  return usage();
}

core::Result<void> applyStoryWrite(game::story::StoryState& state, const StoryWrite& write) {
  if (write.kind == StoryWrite::Kind::Trigger) {
    return state.setTrigger(write.number, write.value != 0);
  }
  return state.setVariable(write.number, static_cast<std::uint8_t>(write.value));
}

} // namespace dwe::app
