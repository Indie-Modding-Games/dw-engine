#include <doctest/doctest.h>

#include <cstdint>
#include <set>

#include "partner_clip_fallbacks.hpp"

namespace {

// The partner's animations as models/partner.yaml names them.
dwe::assets::CharacterModel partnerCharacter() {
  const auto model = dwe::assets::AssetId::parse("models/test");
  REQUIRE(model.has_value());
  return {.model = *model,
          .animations = {{"idle", 0},
                         {"idle_tired", 1},
                         {"walk", 2},
                         {"walk_tired", 3},
                         {"run", 4},
                         {"happy", 5},
                         {"unhappy", 7},
                         {"hurt", 14},
                         {"sleepy", 17}}};
}

} // namespace

TEST_CASE("a partner model without some field clips plays its walk or its idle instead") {
  const dwe::assets::CharacterModel character = partnerCharacter();
  // An enemy's model: the idle, the walk and the run, and battle clips.
  std::set<std::uint32_t> clips{0, 1, 2, 4, 19, 33};
  const auto has = [&](std::uint32_t clip) { return clips.contains(clip); };
  auto fallbacks = dwe::app::partnerClipFallbacks(character, has);
  CHECK(fallbacks.size() == 5);
  CHECK(fallbacks.at(3) == 2);
  CHECK(fallbacks.at(5) == 0);
  CHECK(fallbacks.at(7) == 0);
  CHECK(fallbacks.at(14) == 0);
  CHECK(fallbacks.at(17) == 0);

  // A character's model with no walk: the moving ones stand idle.
  clips = {0, 29};
  fallbacks = dwe::app::partnerClipFallbacks(character, has);
  CHECK(fallbacks.at(2) == 0);
  CHECK(fallbacks.at(3) == 0);
  CHECK(fallbacks.at(4) == 0);
  CHECK(fallbacks.at(1) == 0);

  // A partner form has them all; a model without an idle gets none (and is refused).
  clips = {0, 1, 2, 3, 4, 5, 7, 14, 17};
  CHECK(dwe::app::partnerClipFallbacks(character, has).empty());
  clips = {2, 4};
  CHECK(dwe::app::partnerClipFallbacks(character, has).empty());
}
