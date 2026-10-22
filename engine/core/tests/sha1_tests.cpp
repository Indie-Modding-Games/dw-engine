#include <dwe/core/sha1.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using dwe::core::Sha1;
using dwe::core::sha1;
using dwe::core::toHex;

namespace {

std::span<const std::byte> bytesOf(std::string_view text) {
  return std::as_bytes(std::span{text});
}

std::string hexOf(std::string_view text) {
  return toHex(sha1(bytesOf(text)));
}

} // namespace

// The examples of FIPS 180-2 and the long-message vectors of the NIST suite.
TEST_CASE("sha1 matches the standard test vectors") {
  CHECK(hexOf("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  CHECK(hexOf("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d");
  CHECK(hexOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
  CHECK(
      hexOf(
          "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrst"
          "nopqrstu") == "a49b2446a02c645bf419f995b67091253a04a259");
  CHECK(hexOf(std::string(1'000'000, 'a')) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

TEST_CASE("sha1 gives the same digest whatever the chunk sizes") {
  std::string message(1000, '\0');
  std::ranges::generate(message, [next = 0U]() mutable { return static_cast<char>(next++ * 7U); });
  const auto expected = sha1(bytesOf(message));

  for (const std::size_t chunkSize : {1U, 3U, 55U, 56U, 63U, 64U, 65U, 999U}) {
    Sha1 hasher;
    auto rest = bytesOf(message);
    while (!rest.empty()) {
      const std::size_t count = std::min(chunkSize, rest.size());
      hasher.update(rest.first(count));
      rest = rest.subspan(count);
    }
    CHECK(hasher.finish() == expected);
  }
}

TEST_CASE("sha1 pads messages that end next to a block boundary") {
  // From 56 bytes on, the length no longer fits in the last block and padding adds a block. Digests
  // from sha1sum.
  const std::vector<std::pair<std::size_t, std::string_view>> vectors = {
      {55, "c1c8bbdc22796e28c0e15163d20899b65621d65a"},
      {56, "c2db330f6083854c99d4b5bfb6e8f29f201be699"},
      {64, "0098ba824b5c16427bd7a1122a5a442a25ec644d"},
  };
  for (const auto& [size, hex] : vectors) {
    CHECK(hexOf(std::string(size, 'a')) == hex);
  }
}

TEST_CASE("the hasher starts over after finishing") {
  Sha1 hasher;
  hasher.update(bytesOf("something else"));
  static_cast<void>(hasher.finish());
  hasher.update(bytesOf("abc"));
  CHECK(toHex(hasher.finish()) == "a9993e364706816aba3e25717850c26c9cd0d89d");
}
