#include <dwe/game/randomizer/run_settings.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::game::randomizer {

namespace {

constexpr std::string_view Alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr std::uint32_t DigitBits = 5;
constexpr std::uint32_t DigitMask = 0x1F;
constexpr std::size_t OptionDigits = 2;
constexpr std::uint32_t LearnShift = 5;
constexpr std::uint32_t LearnMask = 0x7;
constexpr std::uint32_t OptionMask = 0xFF;

constexpr std::array<std::string_view, LearnChances.size()> LearnNames{"original", "25", "50", "75",
                                                                       "100"};
constexpr std::array<std::int32_t, LearnChances.size()> LearnPercents{0, 25, 50, 75, 100};

std::optional<std::uint32_t> digitOf(char character) {
  if (character >= 'a' && character <= 'z') {
    character = static_cast<char>(character - 'a' + 'A');
  }
  if (character == 'I' || character == 'L') {
    character = '1';
  } else if (character == 'O') {
    character = '0';
  }
  const std::size_t at = Alphabet.find(character);
  return at == std::string_view::npos ? std::nullopt
                                      : std::optional{static_cast<std::uint32_t>(at)};
}

std::string digits(std::uint64_t value, std::size_t count) {
  std::string text(count, '0');
  for (std::size_t at = count; at > 0; --at) {
    text[at - 1] = Alphabet[value & DigitMask];
    value >>= DigitBits;
  }
  return text;
}

std::uint32_t optionBits(const RunSettings& settings) {
  std::uint32_t bits = 0;
  const std::array flags{settings.evolutions, settings.starter, settings.enemies, settings.items,
                         settings.drops};
  for (std::size_t flag = 0; flag < flags.size(); ++flag) {
    if (flags[flag]) {
      bits |= 1U << flag;
    }
  }
  return bits | (static_cast<std::uint32_t>(settings.learnChance) << LearnShift);
}

std::optional<RunSettings> settingsOf(std::uint32_t bits) {
  const std::uint32_t learn = (bits >> LearnShift) & LearnMask;
  if ((bits & ~OptionMask) != 0 || learn >= LearnChances.size()) {
    return std::nullopt;
  }
  const auto on = [bits](std::uint32_t flag) { return (bits & (1U << flag)) != 0; };
  return RunSettings{.seed = 0,
                     .evolutions = on(0),
                     .learnChance = LearnChances.at(learn),
                     .starter = on(1),
                     .enemies = on(2),
                     .items = on(3),
                     .drops = on(4)};
}

} // namespace

std::optional<std::int32_t> percentOf(LearnChance chance) {
  if (chance == LearnChance::Original) {
    return std::nullopt;
  }
  return LearnPercents.at(static_cast<std::size_t>(chance));
}

std::string_view nameOf(LearnChance chance) {
  return LearnNames.at(static_cast<std::size_t>(chance));
}

std::optional<LearnChance> learnChanceNamed(std::string_view name) {
  const auto found = std::ranges::find(LearnNames, name);
  if (found == LearnNames.end()) {
    return std::nullopt;
  }
  return LearnChances.at(static_cast<std::size_t>(found - LearnNames.begin()));
}

bool RunSettings::active() const {
  return evolutions || starter || enemies || items || drops || learnChance != LearnChance::Original;
}

RunSettings chaos(std::uint64_t seed) {
  return {.seed = seed & SeedMask,
          .evolutions = true,
          .learnChance = LearnChance::Always,
          .starter = true,
          .enemies = true,
          .items = true,
          .drops = true};
}

std::string seedText(std::uint64_t seed) {
  return digits(seed & SeedMask, SeedLength);
}

std::string runCode(const RunSettings& settings) {
  return seedText(settings.seed) + "-" + digits(optionBits(settings), OptionDigits);
}

std::optional<ParsedCode> parseCode(std::string_view text) {
  std::uint64_t value = 0;
  std::size_t count = 0;
  for (const char character : text) {
    if (character == ' ' || character == '-') {
      continue;
    }
    const auto digit = digitOf(character);
    if (!digit || count == SeedLength + OptionDigits) {
      return std::nullopt;
    }
    value = (value << DigitBits) | *digit;
    ++count;
  }
  if (count == SeedLength) {
    return ParsedCode{.seed = value, .options = std::nullopt};
  }
  if (count != SeedLength + OptionDigits) {
    return std::nullopt;
  }
  const auto options = settingsOf(
      static_cast<std::uint32_t>(value & ((std::uint64_t{1} << (DigitBits * OptionDigits)) - 1U)));
  if (!options) {
    return std::nullopt;
  }
  return ParsedCode{.seed = value >> (DigitBits * OptionDigits), .options = options};
}

bool codeCharacter(char character) {
  return character == '-' || digitOf(character).has_value();
}

} // namespace dwe::game::randomizer
