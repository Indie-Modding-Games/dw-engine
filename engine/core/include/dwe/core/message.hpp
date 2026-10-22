#pragma once

#include <format>
#include <string>
#include <string_view>

namespace dwe::core {

// Fills a message template: the error and log messages of a module are named std::format templates
// in its messages.hpp, never literals in its logic (docs/conventions/code.md). Being constants, the
// templates are checked by the tests rather than by the compiler.
template <typename... Args>
std::string formatMessage(std::string_view message, const Args&... args) {
  return std::vformat(message, std::make_format_args(args...));
}

} // namespace dwe::core
