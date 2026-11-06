#include <dwe/render/slot_map.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>

using dwe::render::SlotKey;
using dwe::render::SlotMap;

// ModelRenderer keeps its models in a SlotMap and hands out its keys as ModelHandle. The renderer
// itself needs a GPU, which the test machines lack, so its add, remove and draw lifetime is tested
// through the map that holds it.

TEST_CASE("a key finds its value until the value is erased") {
  SlotMap<std::string> map;
  const SlotKey key = map.insert("boy");
  REQUIRE(map.find(key) != nullptr);
  CHECK(*map.find(key) == "boy");

  CHECK(map.erase(key).value_or("") == "boy");
  CHECK(map.find(key) == nullptr);
  CHECK_FALSE(map.erase(key).has_value());
}

TEST_CASE("a stale key is rejected once its slot holds a new value") {
  SlotMap<std::string> map;
  const SlotKey stale = map.insert("boy");
  map.erase(stale);
  const SlotKey fresh = map.insert("partner");

  CHECK(fresh.index == stale.index);
  CHECK(fresh.generation != stale.generation);
  CHECK(map.find(stale) == nullptr);
  CHECK_FALSE(map.erase(stale).has_value());
  REQUIRE(map.find(fresh) != nullptr);
  CHECK(*map.find(fresh) == "partner");
}

TEST_CASE("keys that were never handed out find nothing") {
  SlotMap<int> map;
  CHECK(map.find(SlotKey{}) == nullptr);
  CHECK_FALSE(SlotKey{}.valid());
  const SlotKey key = map.insert(7);
  CHECK(key.valid());
  CHECK(map.find({.index = key.index + 1, .generation = 0}) == nullptr);
  CHECK(map.find({.index = key.index, .generation = key.generation + 1}) == nullptr);
}

TEST_CASE("forEach visits the live values only") {
  SlotMap<int> map;
  const SlotKey first = map.insert(1);
  const SlotKey second = map.insert(2);
  const SlotKey third = map.insert(3);
  map.erase(second);

  std::vector<int> seen;
  std::vector<SlotKey> keys;
  map.forEach([&](SlotKey key, int value) {
    keys.push_back(key);
    seen.push_back(value);
  });
  CHECK(seen == std::vector<int>{1, 3});
  CHECK(keys == std::vector<SlotKey>{first, third});
}
