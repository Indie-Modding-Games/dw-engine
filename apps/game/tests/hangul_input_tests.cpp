#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "hangul_input.hpp"

namespace dwe::app {
namespace {

std::vector<std::string> typed(std::initializer_list<std::string_view> jamo) {
  std::vector<std::string> letters;
  for (const std::string_view letter : jamo) {
    if (!letters.empty()) {
      if (const auto join = joinHangul(letters.back(), letter)) {
        letters.back() = join->joined;
        if (join->next) {
          letters.push_back(*join->next);
        }
        continue;
      }
    }
    letters.emplace_back(letter);
  }
  return letters;
}

} // namespace

TEST_CASE("Korean letters join into syllables as they are written") {
  CHECK(isHangulJamo("ㄱ"));
  CHECK(isHangulJamo("ㅣ"));
  CHECK_FALSE(isHangulJamo("가"));
  CHECK_FALSE(isHangulJamo("a"));

  CHECK(typed({"ㅎ", "ㅏ", "ㄴ", "ㄱ", "ㅡ", "ㄹ"}) == std::vector<std::string>{"한", "글"});
  CHECK(typed({"ㅎ", "ㅏ", "ㄴ", "ㅏ"}) == std::vector<std::string>{"하", "나"});
  CHECK(typed({"ㄷ", "ㅏ", "ㄹ", "ㄱ"}) == std::vector<std::string>{"닭"});
  CHECK(typed({"ㄷ", "ㅏ", "ㄹ", "ㄱ", "ㅏ"}) == std::vector<std::string>{"달", "가"});
  CHECK(typed({"ㅇ", "ㅗ", "ㅏ"}) == std::vector<std::string>{"와"});
  CHECK(typed({"ㅡ", "ㅣ"}) == std::vector<std::string>{"ㅢ"});
  CHECK(typed({"ㄱ", "ㅏ", "ㄸ", "ㅏ"}) == std::vector<std::string>{"가", "따"});
  CHECK(typed({"ㄱ", "ㄴ"}) == std::vector<std::string>{"ㄱ", "ㄴ"});
  CHECK(typed({"A", "ㅏ"}) == std::vector<std::string>{"A", "ㅏ"});
}

TEST_CASE("an erase takes a syllable apart one letter at a time") {
  CHECK(unjoinHangul("닭") == "달");
  CHECK(unjoinHangul("달") == "다");
  CHECK(unjoinHangul("다") == "ㄷ");
  CHECK(unjoinHangul("와") == "오");
  CHECK(unjoinHangul("ㅢ") == "ㅡ");
  CHECK_FALSE(unjoinHangul("ㄷ").has_value());
  CHECK_FALSE(unjoinHangul("A").has_value());
  CHECK_FALSE(unjoinHangul("漢").has_value());
}

} // namespace dwe::app
