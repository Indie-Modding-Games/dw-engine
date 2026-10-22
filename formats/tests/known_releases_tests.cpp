#include <dwe/formats/disc/known_releases.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <charconv>
#include <set>
#include <string_view>

using dwe::core::Sha1Digest;
using dwe::formats::disc::findKnownRelease;
using dwe::formats::disc::knownReleases;

namespace {

Sha1Digest digestOf(std::string_view hex) {
  Sha1Digest digest{};
  for (std::size_t i = 0; i < digest.size(); ++i) {
    unsigned value = 0;
    std::from_chars(hex.data() + (i * 2), hex.data() + (i * 2) + 2, value, 16);
    digest[i] = static_cast<std::byte>(value);
  }
  return digest;
}

} // namespace

TEST_CASE("every known digest is 40 lowercase hex digits and appears once") {
  std::set<std::string_view> seen;
  for (const auto& release : knownReleases()) {
    CHECK(release.sha1.size() == 40);
    CHECK(std::ranges::all_of(
        release.sha1, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }));
    CHECK(seen.insert(release.sha1).second);
    CHECK_FALSE(release.version.empty());
    CHECK_FALSE(release.description.empty());
  }
}

TEST_CASE("a known digest is found and an unknown one is not") {
  REQUIRE_FALSE(knownReleases().empty());
  const auto& first = knownReleases().front();
  CHECK(findKnownRelease(digestOf(first.sha1)) == first);

  CHECK_FALSE(findKnownRelease(Sha1Digest{}).has_value());
}
