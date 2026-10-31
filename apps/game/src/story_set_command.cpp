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
// battle_ui.c and script_interp.c (ADD_MONEY): the bits never pass 999999.
constexpr std::int32_t MoneyLimit = 999999;

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
        std::format("'{}': expected 'trigger <n> on|off', 'var <n> <0-255>' or 'money <0-{}>'",
                    text, MoneyLimit));
  };
  if (words.size() == 2 && words[0] == "money") {
    const auto bits = integerOf(words[1]);
    if (!bits || *bits < 0 || *bits > MoneyLimit) {
      return usage();
    }
    return StoryWrite{.kind = StoryWrite::Kind::Money, .number = 0, .value = *bits};
  }
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
  if (write.kind == StoryWrite::Kind::Money) {
    return core::fail(std::string{"money goes to the bag, not to the story's state"});
  }
  return state.setVariable(write.number, static_cast<std::uint8_t>(write.value));
}

} // namespace dwe::app
