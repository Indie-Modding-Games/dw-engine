#include <dwe/game/partner/area_response.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <ostream>

// map.c: loadMapSetup, with synthetic region lists: the first slot where either list holds the
// favored region decides.
namespace dwe::game::partner {

namespace {

constexpr std::int32_t NoRegion = assets::MapRegions::NoRegion;
constexpr std::int32_t Favored = 3;
constexpr std::int32_t Other = 5;

} // namespace

TEST_CASE(
    "area response: a map that lists none of its regions as the favored one leaves the partner neutral") {
  const assets::MapRegions regions{.a = {Other, NoRegion, NoRegion, NoRegion},
                                   .b = {NoRegion, NoRegion, NoRegion, NoRegion}};
  CHECK(areaResponseOf(regions, Favored) == AreaResponse::Neutral);
}

TEST_CASE("area response: the favored region in the first list is liked, in the second disliked") {
  CHECK(areaResponseOf(
            {.a = {Other, Other, Favored, NoRegion}, .b = {NoRegion, NoRegion, NoRegion, NoRegion}},
            Favored) == AreaResponse::Liked);
  CHECK(areaResponseOf({.a = {Other, NoRegion, NoRegion, NoRegion},
                        .b = {NoRegion, NoRegion, NoRegion, Favored}},
                       Favored) == AreaResponse::Disliked);
}

TEST_CASE(
    "area response: the lists are read slot by slot, so the earlier slot wins whichever list it is in") {
  // Second list, first slot, before the first list's second slot.
  CHECK(areaResponseOf({.a = {Other, Favored, NoRegion, NoRegion},
                        .b = {Favored, NoRegion, NoRegion, NoRegion}},
                       Favored) == AreaResponse::Disliked);
  // The same slot in both lists: the first list is read first.
  CHECK(areaResponseOf(
            {.a = {Other, Favored, NoRegion, NoRegion}, .b = {Other, Favored, NoRegion, NoRegion}},
            Favored) == AreaResponse::Liked);
}

TEST_CASE("area response: region 0 is a region like any other, and an unused slot never matches") {
  constexpr std::int32_t FirstRegion = 0;
  CHECK(areaResponseOf({.a = {NoRegion, NoRegion, NoRegion, NoRegion},
                        .b = {FirstRegion, NoRegion, NoRegion, NoRegion}},
                       FirstRegion) == AreaResponse::Disliked);
  CHECK(areaResponseOf({.a = {NoRegion, NoRegion, NoRegion, NoRegion},
                        .b = {NoRegion, NoRegion, NoRegion, NoRegion}},
                       FirstRegion) == AreaResponse::Neutral);
}

TEST_CASE(
    "area response: as the original's bytes, only the low byte counts and a region of 128 or more never matches") {
  // A region is a signed byte and the favored region an unsigned one: 255 is -1 on the map's side.
  constexpr std::int32_t HighRegion = 255;
  CHECK(areaResponseOf({.a = {HighRegion, NoRegion, NoRegion, NoRegion},
                        .b = {NoRegion, NoRegion, NoRegion, NoRegion}},
                       HighRegion) == AreaResponse::Neutral);
  // An unused slot is not the favored region 255 either.
  CHECK(areaResponseOf({.a = {NoRegion, NoRegion, NoRegion, NoRegion},
                        .b = {NoRegion, NoRegion, NoRegion, NoRegion}},
                       HighRegion) == AreaResponse::Neutral);
  // Above a byte, the low byte is the region.
  constexpr std::int32_t ByteSpan = 256;
  CHECK(areaResponseOf({.a = {NoRegion, NoRegion, NoRegion, NoRegion},
                        .b = {ByteSpan + Favored, NoRegion, NoRegion, NoRegion}},
                       Favored) == AreaResponse::Disliked);
  CHECK(areaResponseOf({.a = {Favored, NoRegion, NoRegion, NoRegion},
                        .b = {NoRegion, NoRegion, NoRegion, NoRegion}},
                       ByteSpan + Favored) == AreaResponse::Liked);
}

} // namespace dwe::game::partner
