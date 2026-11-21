#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace dwe::app {

[[nodiscard]] bool isHangulJamo(std::string_view letter);

struct HangulJoin {
  std::string joined;
  std::optional<std::string> next;
};
[[nodiscard]] std::optional<HangulJoin> joinHangul(std::string_view last, std::string_view jamo);

[[nodiscard]] std::optional<std::string> unjoinHangul(std::string_view letter);

} // namespace dwe::app
