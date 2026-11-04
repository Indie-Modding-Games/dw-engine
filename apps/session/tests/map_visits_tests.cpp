#include <dwe/game/save/sections.hpp>
#include <dwe/session/map_visits.hpp>

#include <doctest/doctest.h>

using dwe::session::MapVisits;

TEST_CASE("a save keeps the maps the player has stood on") {
  MapVisits saved;
  dwe::game::save::SaveSections out;
  REQUIRE(saved.addSaveSections(out).has_value());
  saved.visit("twna01");
  saved.visit("mayo01");
  saved.visit("mayo01");
  saved.visit("");
  CHECK(saved.maps().size() == 2);
  dwe::game::save::SaveFile file;
  REQUIRE(out.write(file).has_value());

  MapVisits loaded;
  dwe::game::save::SaveSections in;
  REQUIRE(loaded.addSaveSections(in).has_value());
  REQUIRE(in.read(file).has_value());
  CHECK(loaded.visited("twna01"));
  CHECK(loaded.visited("mayo01"));
  CHECK_FALSE(loaded.visited("gias01"));
}

TEST_CASE("a save from before the island map loads with no visited map") {
  MapVisits loaded;
  loaded.visit("twna01");
  dwe::game::save::SaveSections in;
  REQUIRE(loaded.addSaveSections(in).has_value());
  const dwe::game::save::SaveFile old;
  REQUIRE(in.read(old).has_value());
  CHECK(loaded.maps().empty());
}
