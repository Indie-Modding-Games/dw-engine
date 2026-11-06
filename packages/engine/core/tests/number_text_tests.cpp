#include <dwe/core/number_text.hpp>

#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <random>
#include <string_view>
#include <system_error>
#include <type_traits>

using dwe::core::fromChars;
using dwe::core::detail::fromCharsPortable;

namespace {

// Apple's libc++ has floating-point std::from_chars only from macOS 26: the comparison runs
// elsewhere.
#if !defined(__APPLE__)
// The texts the portable parse must read as the standard library does: forms, ends, errors and
// limits.
// clang-format off
constexpr std::array Texts = {
    "0", "-0", "1", "1.5", "-2.25", ".5", "5.", "-.5", "00012.50", "1e10", "1E-3", "1e+2", "2.5e",
    "2.5e+", "2.5e-x", "1.5x", "1.5 ", "1..5", "--1", "+1", " 1", "", ".", "-", "e5", "x", "inf",
    "-INF", "infinity", "Infinity", "infx", "nan", "-NaN", "nan(123)", "nan(abc_9)", "nan(",
    "nan(1", "0x10", "1e400", "-1e400", "1e-400", "3.4e39", "3.4028234e38", "1e-46", "1e-40", "0.1",
    "0.30000000000000004", "123456789012345678901234567890", "4.9406564584124654e-324",
    "2.2250738585072014e-308", "1.7976931348623157e308", "1.7976931348623159e308"};
// clang-format on

template <typename T>
void checkSame(std::string_view text) {
  CAPTURE(text);
  const char* first = text.data();
  const char* last = text.data() + text.size();
  T expected = T{7};
  T actual = T{7};
  const auto standard = std::from_chars(first, last, expected);
  const auto portable = fromCharsPortable(first, last, actual);
  CHECK(portable.ec == standard.ec);
  CHECK(portable.ptr - first == standard.ptr - first);
  if (std::isnan(expected)) {
    CHECK(std::isnan(actual));
    CHECK(std::signbit(actual) == std::signbit(expected));
  } else {
    CHECK(
        std::bit_cast<std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>(actual) ==
        std::bit_cast<std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>(expected));
  }
}

#endif

template <typename T, typename Bits>
void checkRoundTrips(std::uint64_t seed) {
  std::mt19937_64 random{seed};
  std::array<char, 64> buffer{};
  for (int i = 0; i < 20000; ++i) {
    const T number = std::bit_cast<T>(static_cast<Bits>(random()));
    if (!std::isfinite(number)) {
      continue;
    }
    const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number);
    REQUIRE(written.ec == std::errc{});
    T read = T{};
    const auto parsed = fromCharsPortable(buffer.data(), written.ptr, read);
    CAPTURE(std::string_view(buffer.data(), static_cast<std::size_t>(written.ptr - buffer.data())));
    REQUIRE(parsed.ec == std::errc{});
    REQUIRE(parsed.ptr == written.ptr);
    REQUIRE(std::bit_cast<Bits>(read) == std::bit_cast<Bits>(number));
  }
}

} // namespace

#if !defined(__APPLE__)
TEST_CASE("the portable floating-point parse reads every form as std::from_chars") {
  for (const std::string_view text : Texts) {
    checkSame<float>(text);
    checkSame<double>(text);
  }
}
#endif

TEST_CASE("the portable floating-point parse gives back every float and double exactly") {
  checkRoundTrips<float, std::uint32_t>(1);
  checkRoundTrips<double, std::uint64_t>(2);
}

TEST_CASE("fromChars reads integers and floating point like std::from_chars") {
  constexpr std::string_view Text = "42.5";
  int whole = 0;
  const auto integer = fromChars(Text.data(), Text.data() + Text.size(), whole);
  CHECK(whole == 42);
  CHECK(integer.ptr == Text.data() + 2);
  float number = 0.0F;
  const auto floating = fromChars(Text.data(), Text.data() + Text.size(), number);
  CHECK(number == 42.5F);
  CHECK(floating.ptr == Text.data() + Text.size());
  double untouched = 3.0;
  CHECK(fromChars(Text.data(), Text.data(), untouched).ec == std::errc::invalid_argument);
  CHECK(untouched == 3.0);
}
