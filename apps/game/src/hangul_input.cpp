#include "hangul_input.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>

namespace dwe::app {
namespace {

constexpr char32_t SyllableFirst = 0xAC00;
constexpr char32_t SyllableLast = 0xD7A3;
constexpr std::size_t Vowels = 21;
constexpr std::size_t Finals = 28;
constexpr char32_t JamoFirst = 0x3131;
constexpr char32_t VowelFirst = 0x314F;
constexpr char32_t JamoLast = 0x3163;

constexpr std::array<char32_t, 19> Initials{U'ㄱ', U'ㄲ', U'ㄴ', U'ㄷ', U'ㄸ', U'ㄹ', U'ㅁ',
                                            U'ㅂ', U'ㅃ', U'ㅅ', U'ㅆ', U'ㅇ', U'ㅈ', U'ㅉ',
                                            U'ㅊ', U'ㅋ', U'ㅌ', U'ㅍ', U'ㅎ'};
constexpr std::array<char32_t, Finals> FinalJamo{0,     U'ㄱ', U'ㄲ', U'ㄳ', U'ㄴ', U'ㄵ', U'ㄶ',
                                                 U'ㄷ', U'ㄹ', U'ㄺ', U'ㄻ', U'ㄼ', U'ㄽ', U'ㄾ',
                                                 U'ㄿ', U'ㅀ', U'ㅁ', U'ㅂ', U'ㅄ', U'ㅅ', U'ㅆ',
                                                 U'ㅇ', U'ㅈ', U'ㅊ', U'ㅋ', U'ㅌ', U'ㅍ', U'ㅎ'};

struct Pair {
  char32_t first;
  char32_t second;
  char32_t joined;
};
constexpr std::array<Pair, 7> VowelPairs{{{U'ㅗ', U'ㅏ', U'ㅘ'},
                                          {U'ㅗ', U'ㅐ', U'ㅙ'},
                                          {U'ㅗ', U'ㅣ', U'ㅚ'},
                                          {U'ㅜ', U'ㅓ', U'ㅝ'},
                                          {U'ㅜ', U'ㅔ', U'ㅞ'},
                                          {U'ㅜ', U'ㅣ', U'ㅟ'},
                                          {U'ㅡ', U'ㅣ', U'ㅢ'}}};
constexpr std::array<Pair, 11> FinalPairs{{{U'ㄱ', U'ㅅ', U'ㄳ'},
                                           {U'ㄴ', U'ㅈ', U'ㄵ'},
                                           {U'ㄴ', U'ㅎ', U'ㄶ'},
                                           {U'ㄹ', U'ㄱ', U'ㄺ'},
                                           {U'ㄹ', U'ㅁ', U'ㄻ'},
                                           {U'ㄹ', U'ㅂ', U'ㄼ'},
                                           {U'ㄹ', U'ㅅ', U'ㄽ'},
                                           {U'ㄹ', U'ㅌ', U'ㄾ'},
                                           {U'ㄹ', U'ㅍ', U'ㄿ'},
                                           {U'ㄹ', U'ㅎ', U'ㅀ'},
                                           {U'ㅂ', U'ㅅ', U'ㅄ'}}};

std::optional<char32_t> onlyCharacter(std::string_view letter) {
  if (letter.size() != 3) {
    return std::nullopt;
  }
  const auto byte = [&](std::size_t at) { return static_cast<std::uint8_t>(letter[at]); };
  if ((byte(0) & 0xF0U) != 0xE0U || (byte(1) & 0xC0U) != 0x80U || (byte(2) & 0xC0U) != 0x80U) {
    return std::nullopt;
  }
  return static_cast<char32_t>(((byte(0) & 0x0FU) << 12U) | ((byte(1) & 0x3FU) << 6U) |
                               (byte(2) & 0x3FU));
}

std::string utf8(char32_t c) {
  return {static_cast<char>(0xE0U | (c >> 12U)), static_cast<char>(0x80U | ((c >> 6U) & 0x3FU)),
          static_cast<char>(0x80U | (c & 0x3FU))};
}

bool isVowel(char32_t c) {
  return c >= VowelFirst && c <= JamoLast;
}

template <std::size_t N>
std::optional<std::size_t> indexOf(const std::array<char32_t, N>& table, char32_t c) {
  const auto found = std::ranges::find(table, c);
  if (found == table.end() || c == 0) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(std::distance(table.begin(), found));
}

template <std::size_t N>
std::optional<char32_t> joined(const std::array<Pair, N>& pairs, char32_t first, char32_t second) {
  for (const Pair& pair : pairs) {
    if (pair.first == first && pair.second == second) {
      return pair.joined;
    }
  }
  return std::nullopt;
}

template <std::size_t N>
std::optional<Pair> split(const std::array<Pair, N>& pairs, char32_t joinedJamo) {
  for (const Pair& pair : pairs) {
    if (pair.joined == joinedJamo) {
      return pair;
    }
  }
  return std::nullopt;
}

struct Syllable {
  std::size_t initial = 0;
  std::size_t vowel = 0;
  std::size_t final = 0;
};

std::optional<Syllable> syllableOf(char32_t c) {
  if (c < SyllableFirst || c > SyllableLast) {
    return std::nullopt;
  }
  const std::size_t index = c - SyllableFirst;
  return Syllable{.initial = index / (Vowels * Finals),
                  .vowel = (index / Finals) % Vowels,
                  .final = index % Finals};
}

char32_t syllable(const Syllable& parts) {
  return SyllableFirst +
         static_cast<char32_t>((((parts.initial * Vowels) + parts.vowel) * Finals) + parts.final);
}

char32_t vowelJamo(std::size_t vowel) {
  return VowelFirst + static_cast<char32_t>(vowel);
}

} // namespace

bool isHangulJamo(std::string_view letter) {
  const auto c = onlyCharacter(letter);
  return c && *c >= JamoFirst && *c <= JamoLast;
}

std::optional<HangulJoin> joinHangul(std::string_view last, std::string_view jamo) {
  const auto before = onlyCharacter(last);
  const auto added = onlyCharacter(jamo);
  if (!before || !added || !isHangulJamo(jamo)) {
    return std::nullopt;
  }
  if (isVowel(*added)) {
    if (const auto initial = indexOf(Initials, *before)) {
      return HangulJoin{.joined =
                            utf8(syllable({.initial = *initial, .vowel = *added - VowelFirst})),
                        .next = {}};
    }
    if (isVowel(*before)) {
      if (const auto both = joined(VowelPairs, *before, *added)) {
        return HangulJoin{.joined = utf8(*both), .next = {}};
      }
      return std::nullopt;
    }
  }
  const auto parts = syllableOf(*before);
  if (!parts) {
    return std::nullopt;
  }
  if (isVowel(*added)) {
    if (parts->final == 0) {
      const auto both = joined(VowelPairs, vowelJamo(parts->vowel), *added);
      if (!both) {
        return std::nullopt;
      }
      Syllable widened = *parts;
      widened.vowel = *both - VowelFirst;
      return HangulJoin{.joined = utf8(syllable(widened)), .next = {}};
    }
    Syllable kept = *parts;
    char32_t moved = FinalJamo.at(parts->final);
    if (const auto pair = split(FinalPairs, moved)) {
      kept.final = *indexOf(FinalJamo, pair->first);
      moved = pair->second;
    } else {
      kept.final = 0;
    }
    const auto initial = indexOf(Initials, moved);
    if (!initial) {
      return std::nullopt;
    }
    return HangulJoin{.joined = utf8(syllable(kept)),
                      .next = utf8(syllable({.initial = *initial, .vowel = *added - VowelFirst}))};
  }
  Syllable closed = *parts;
  if (parts->final == 0) {
    const auto final = indexOf(FinalJamo, *added);
    if (!final) {
      return std::nullopt; // ㄸ, ㅃ and ㅉ never close a syllable
    }
    closed.final = *final;
  } else {
    const auto both = joined(FinalPairs, FinalJamo.at(parts->final), *added);
    if (!both) {
      return std::nullopt;
    }
    closed.final = *indexOf(FinalJamo, *both);
  }
  return HangulJoin{.joined = utf8(syllable(closed)), .next = {}};
}

std::optional<std::string> unjoinHangul(std::string_view letter) {
  const auto c = onlyCharacter(letter);
  if (!c) {
    return std::nullopt;
  }
  if (const auto pair = split(VowelPairs, *c)) {
    return utf8(pair->first);
  }
  const auto parts = syllableOf(*c);
  if (!parts) {
    return std::nullopt;
  }
  Syllable fewer = *parts;
  if (parts->final != 0) {
    const auto pair = split(FinalPairs, FinalJamo.at(parts->final));
    fewer.final = pair ? *indexOf(FinalJamo, pair->first) : 0;
    return utf8(syllable(fewer));
  }
  if (const auto pair = split(VowelPairs, vowelJamo(parts->vowel))) {
    fewer.vowel = pair->first - VowelFirst;
    return utf8(syllable(fewer));
  }
  return utf8(Initials.at(parts->initial));
}

} // namespace dwe::app
