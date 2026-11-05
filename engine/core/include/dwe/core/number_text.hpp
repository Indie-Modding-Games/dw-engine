#pragma once

#include <charconv>
#include <concepts>
#include <type_traits>

namespace dwe::core {

namespace detail {

// std::from_chars for floating point (the general format), written on the C library's strtod in the
// "C" locale: the form it reads ends where std::from_chars would stop, so the result, the end and
// the error are the same. Built on every platform, so its tests run everywhere; fromChars uses it
// where the standard library cannot.
std::from_chars_result fromCharsPortable(const char* first, const char* last, float& value);
std::from_chars_result fromCharsPortable(const char* first, const char* last, double& value);

std::from_chars_result fromCharsFloating(const char* first, const char* last, float& value);
std::from_chars_result fromCharsFloating(const char* first, const char* last, double& value);

} // namespace detail

// std::from_chars for any number type, the same on every platform: an integer goes to the standard
// library; a float or a double too, except where Apple's libc++ offers floating-point
// std::from_chars only from macOS 26, and there it is detail::fromCharsPortable. Both round
// correctly, so a text reads to the same value everywhere. Like std::from_chars: no leading
// whitespace or '+', "inf", "infinity", "nan" and "nan(...)" in any case; the value is left as it
// was when the text is not a number, and out of range it is +-infinity or +-0, as libc++ and the
// MSVC library give it.
template <typename T>
  requires std::is_arithmetic_v<T> && (!std::same_as<T, bool>)
std::from_chars_result fromChars(const char* first, const char* last, T& value) {
  if constexpr (std::is_floating_point_v<T>) {
    return detail::fromCharsFloating(first, last, value);
  } else {
    return std::from_chars(first, last, value);
  }
}

} // namespace dwe::core
