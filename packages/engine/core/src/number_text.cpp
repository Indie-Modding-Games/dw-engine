#include <dwe/core/number_text.hpp>

// newlocale and _create_locale are not in <clocale>.
// NOLINTNEXTLINE(modernize-deprecated-headers)
#include <locale.h>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#if defined(__APPLE__)
#include <xlocale.h>
#endif

namespace dwe::core::detail {
namespace {

bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

char lower(char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool startsWithWord(const char* at, const char* last, std::string_view word) {
  if (last - at < static_cast<std::ptrdiff_t>(word.size())) {
    return false;
  }
  for (std::size_t i = 0; i < word.size(); ++i) {
    if (lower(at[i]) != word[i]) {
      return false;
    }
  }
  return true;
}

enum class Kind : std::uint8_t { None, Number, Infinity, NotANumber };

struct Scan {
  const char* end;
  Kind kind;
  bool negative;
};

// The longest prefix std::from_chars reads in the general format: an optional '-', then digits with
// an optional fraction (at least one digit) and an optional exponent that is used only when it has
// digits, or inf, infinity, nan or nan(letters, digits and '_').
Scan scan(const char* first, const char* last) {
  const char* at = first;
  const bool negative = at != last && *at == '-';
  if (negative) {
    ++at;
  }
  if (startsWithWord(at, last, "infinity")) {
    return {at + 8, Kind::Infinity, negative};
  }
  if (startsWithWord(at, last, "inf")) {
    return {at + 3, Kind::Infinity, negative};
  }
  if (startsWithWord(at, last, "nan")) {
    at += 3;
    if (at != last && *at == '(') {
      const char* close = at + 1;
      while (close != last &&
             (isDigit(*close) || (lower(*close) >= 'a' && lower(*close) <= 'z') || *close == '_')) {
        ++close;
      }
      if (close != last && *close == ')') {
        at = close + 1;
      }
    }
    return {at, Kind::NotANumber, negative};
  }
  const char* digits = at;
  while (at != last && isDigit(*at)) {
    ++at;
  }
  bool any = at != digits;
  if (at != last && *at == '.') {
    ++at;
    const char* fraction = at;
    while (at != last && isDigit(*at)) {
      ++at;
    }
    any = any || at != fraction;
  }
  if (!any) {
    return {first, Kind::None, negative};
  }
  if (at != last && (*at == 'e' || *at == 'E')) {
    const char* exponent = at + 1;
    if (exponent != last && (*exponent == '+' || *exponent == '-')) {
      ++exponent;
    }
    const char* exponentDigits = exponent;
    while (exponent != last && isDigit(*exponent)) {
      ++exponent;
    }
    if (exponent != exponentDigits) {
      at = exponent;
    }
  }
  return {at, Kind::Number, negative};
}

// The "C" locale, made once: its decimal point is always '.', whatever locale the program runs in.
#if defined(_WIN32)
_locale_t cLocale() {
  static const _locale_t locale = _create_locale(LC_ALL, "C");
  return locale;
}
double toDouble(const char* text, char** end) {
  return _strtod_l(text, end, cLocale());
}
float toFloat(const char* text, char** end) {
  return _strtof_l(text, end, cLocale());
}
#else
locale_t cLocale() {
  static const locale_t locale = newlocale(LC_ALL_MASK, "C", static_cast<locale_t>(nullptr));
  return locale;
}
double toDouble(const char* text, char** end) {
  return strtod_l(text, end, cLocale());
}
float toFloat(const char* text, char** end) {
  return strtof_l(text, end, cLocale());
}
#endif

template <typename T>
std::from_chars_result parse(const char* first, const char* last, T& value) {
  const Scan found = scan(first, last);
  switch (found.kind) {
  case Kind::None:
    return {first, std::errc::invalid_argument};
  case Kind::Infinity:
    value =
        found.negative ? -std::numeric_limits<T>::infinity() : std::numeric_limits<T>::infinity();
    return {found.end, std::errc{}};
  case Kind::NotANumber:
    value = std::copysign(std::numeric_limits<T>::quiet_NaN(), found.negative ? T{-1} : T{1});
    return {found.end, std::errc{}};
  case Kind::Number:
    break;
  }
  // Only the scanned digits, so strtod reads exactly them (it would also take a '+', spaces and
  // hexadecimal).
  const std::string text(first, found.end);
  char* end = nullptr;
  errno = 0;
  const T parsed = std::is_same_v<T, float> ? static_cast<T>(toFloat(text.c_str(), &end))
                                            : static_cast<T>(toDouble(text.c_str(), &end));
  const int error = errno;
  if (end != text.c_str() + text.size()) {
    return {first, std::errc::invalid_argument};
  }
  value = parsed;
  // strtod saturates to infinity past the largest value and to zero below the smallest, with
  // ERANGE, and that value is kept, as libc++ and the MSVC library keep it; a subnormal is a value
  // (some C libraries flag it with ERANGE too).
  if (error == ERANGE && (std::isinf(parsed) || parsed == T{0})) {
    return {found.end, std::errc::result_out_of_range};
  }
  return {found.end, std::errc{}};
}

} // namespace

std::from_chars_result fromCharsPortable(const char* first, const char* last, float& value) {
  return parse(first, last, value);
}

std::from_chars_result fromCharsPortable(const char* first, const char* last, double& value) {
  return parse(first, last, value);
}

#if defined(__APPLE__)
std::from_chars_result fromCharsFloating(const char* first, const char* last, float& value) {
  return fromCharsPortable(first, last, value);
}

std::from_chars_result fromCharsFloating(const char* first, const char* last, double& value) {
  return fromCharsPortable(first, last, value);
}
#else
std::from_chars_result fromCharsFloating(const char* first, const char* last, float& value) {
  return std::from_chars(first, last, value);
}

std::from_chars_result fromCharsFloating(const char* first, const char* last, double& value) {
  return std::from_chars(first, last, value);
}
#endif

} // namespace dwe::core::detail
