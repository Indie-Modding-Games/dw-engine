#include <dwe/screens/damage_numbers.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using dwe::screens::DamageNumbers;
using dwe::screens::NumberColour;
using dwe::screens::NumberIcon;
using dwe::screens::ShownNumber;
using dwe::screens::WindowFraction;

namespace {

constexpr WindowFraction Feet{.x = 0.5, .y = 0.45};

// The y of the first number shown on each frame until none is left.
std::vector<std::int32_t> heights(DamageNumbers& numbers) {
  std::vector<std::int32_t> ys;
  for (int frame = 0; frame < 100; ++frame) {
    numbers.step();
    if (numbers.shown().empty()) {
      break;
    }
    ys.push_back(numbers.shown().front().y);
  }
  return ys;
}

} // namespace

TEST_CASE("a damage number shows 8 pixels over the feet on its first frame, bounces, and goes "
          "after 31 frames") {
  DamageNumbers numbers;
  numbers.add(1, 91, NumberColour::Plain, Feet);
  numbers.step();
  REQUIRE(numbers.shown().size() == 1);
  CHECK(numbers.shown().front() ==
        ShownNumber{.anchor = Feet, .x = 0, .y = -8, .value = 91, .colour = NumberColour::Plain});

  DamageNumbers again;
  again.add(0, 46, NumberColour::Plain, Feet);
  // entity_text.c: ENTITY_TEXT_Y_OFFSETS, measured in the original as -20, -27, -32, -37, -39
  // relative to the screen's centre for a hit 12 pixels over it.
  const std::vector<std::int32_t> expected{-8,  -15, -20, -25, -27, -28, -27, -25, -20, -15, -8,
                                           -12, -15, -17, -17, -18, -17, -17, -15, -12, -12, -12,
                                           -12, -12, -12, -12, -12, -12, -12, -12, -12};
  CHECK(heights(again) == expected);
  CHECK(again.empty());
}

TEST_CASE("the next number of a fighter shows once the one before has shown 11 frames, 4 pixels "
          "right and down") {
  DamageNumbers numbers;
  numbers.add(1, 10, NumberColour::Plain, Feet);
  numbers.add(1, 20, NumberColour::Plain, Feet);
  for (int frame = 0; frame < 10; ++frame) {
    numbers.step();
    REQUIRE(numbers.shown().size() == 1);
  }
  numbers.step();
  REQUIRE(numbers.shown().size() == 2);
  CHECK(numbers.shown()[1].value == 20);
  CHECK(numbers.shown()[1].x == 4);
  CHECK(numbers.shown()[1].y == -4);
  // Another fighter's queue does not wait.
  numbers.add(0, 5, NumberColour::Poison, Feet);
  numbers.step();
  CHECK(numbers.shown().size() == 3);
  CHECK(numbers.shown().front().colour == NumberColour::Poison);
}

TEST_CASE("a fighter takes 8 numbers until its queue empties") {
  DamageNumbers numbers;
  for (int index = 0; index < 9; ++index) {
    numbers.add(2, index + 1, NumberColour::Plain, Feet);
  }
  std::vector<std::int32_t> seen;
  for (int frame = 0; frame < 400 && !numbers.empty(); ++frame) {
    numbers.step();
    for (const ShownNumber& shown : numbers.shown()) {
      if (std::ranges::find(seen, shown.value) == seen.end()) {
        seen.push_back(shown.value);
      }
    }
  }
  CHECK(seen == std::vector<std::int32_t>{1, 2, 3, 4, 5, 6, 7, 8});
  // Empty again, it takes new ones at the first place.
  numbers.add(2, 77, NumberColour::Plain, Feet);
  numbers.step();
  REQUIRE(numbers.shown().size() == 1);
  CHECK(numbers.shown().front().x == 0);
}

TEST_CASE("a number whose fighter was off the camera takes its place in the queue unseen") {
  DamageNumbers numbers;
  numbers.add(3, 12, NumberColour::Plain, std::nullopt);
  numbers.add(3, 34, NumberColour::Plain, Feet);
  numbers.step();
  CHECK(numbers.shown().empty());
  CHECK_FALSE(numbers.empty());
  numbers.clear();
  CHECK(numbers.empty());
}

TEST_CASE("the shown numbers pack as anchor percent, offset, value, colour and icon") {
  const std::vector<ShownNumber> shown{
      {.anchor = Feet, .x = 4, .y = -12, .value = 123, .colour = NumberColour::Poison},
      {.anchor = Feet,
       .x = 0,
       .y = -8,
       .value = 500,
       .colour = NumberColour::Gain,
       .icon = NumberIcon::Mp}};
  const auto settings = dwe::screens::settingsOf(shown);
  REQUIRE(settings.size() == 1);
  CHECK(settings.front().name == "battle.damage.numbers");
  CHECK(std::get<std::string>(settings.front().value) ==
        "50.0 45.0 4 -12 123 12 0 50.0 45.0 0 -8 500 11 2 ");
}

TEST_CASE("a healing or raise number keeps its icon and green tint while it bounces in its queue") {
  DamageNumbers numbers;
  numbers.add(0, 1500, NumberColour::Gain, Feet, NumberIcon::Hp);
  numbers.add(0, 1500, NumberColour::Gain, Feet, NumberIcon::Mp);
  numbers.step();
  REQUIRE(numbers.shown().size() == 1);
  CHECK(numbers.shown().front().icon == NumberIcon::Hp);
  CHECK(numbers.shown().front().colour == NumberColour::Gain);
  for (int frame = 0; frame < 11; ++frame) {
    numbers.step();
  }
  REQUIRE(numbers.shown().size() == 2);
  // handleDoubleFloppy: the MP comes second, 4 pixels further right and down.
  CHECK(numbers.shown().back().icon == NumberIcon::Mp);
  CHECK(numbers.shown().back().x == 4);
}
