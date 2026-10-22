#include <dwe/audio/clip.hpp>

#include <doctest/doctest.h>

#include "clip_fixtures.hpp"

using namespace dwe::audio;
using namespace dwe::audio::tests;

TEST_CASE("a clip counts its frames across its channels") {
  CHECK(clipOf({1, 2, 3, 4}, 2)->frameCount() == 2);
  CHECK(clipOf({1, 2, 3, 4}, 1)->frameCount() == 4);
}

TEST_CASE("a playable clip has one or two channels, a rate, whole frames and a loop inside it") {
  CHECK(checkClip(*clipOf({1, 2}, 1)));
  CHECK(checkClip(*clipOf({1, 2, 3, 4}, 2, 22050, ClipLoop{.start = 1, .length = 1})));

  CHECK_FALSE(checkClip(*clipOf({1, 2, 3}, 3)));
  CHECK_FALSE(checkClip(*clipOf({1, 2}, 1, 0)));
  CHECK_FALSE(checkClip(*clipOf({1, 2, 3}, 2)));
  CHECK_FALSE(checkClip(*clipOf({}, 1)));
  CHECK_FALSE(checkClip(*clipOf({1, 2}, 1, 48000, ClipLoop{.start = 1, .length = 0})));
  const auto pastTheEnd = checkClip(*clipOf({1, 2}, 1, 48000, ClipLoop{.start = 1, .length = 2}));
  REQUIRE_FALSE(pastTheEnd);
  CHECK(pastTheEnd.error().message.contains("ends past its 2 frames"));
}
