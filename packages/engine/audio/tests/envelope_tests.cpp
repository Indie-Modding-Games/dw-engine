#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>

#include "envelope.hpp"

using dwe::audio::ChipEnvelope;
using dwe::audio::Envelope;

namespace {

// The first register: attack (exponential bit, shift, step), decay shift and sustain level.
constexpr std::uint16_t first(bool exponential, int attackShift, int attackStep, int decayShift,
                              int sustainLevel) {
  return static_cast<std::uint16_t>((exponential ? 0x8000 : 0) | (attackShift << 10) |
                                    (attackStep << 8) | (decayShift << 4) | sustainLevel);
}

// The second register's sustain half: exponential bit, direction, shift and step.
constexpr std::uint16_t second(bool exponential, bool decreasing, int shift, int step) {
  return static_cast<std::uint16_t>((exponential ? 0x8000 : 0) | (decreasing ? 0x4000 : 0) |
                                    (shift << 8) | (step << 6));
}

constexpr std::uint16_t HoldSustain = second(false, false, 31, 3);

float levelAfter(Envelope& envelope, std::size_t frames) {
  float level = 0.0F;
  for (std::size_t i = 0; i < frames; ++i) {
    level = envelope.advance();
  }
  return level;
}

} // namespace

TEST_CASE("a fast attack rises to full and decays to the sustain level") {
  Envelope envelope{{.adsr1 = first(false, 0, 0, 0, 7), .adsr2 = HoldSustain}};
  CHECK(envelope.advance() < 0.5F);
  CHECK(levelAfter(envelope, 3) == doctest::Approx(1.0F));
  CHECK(levelAfter(envelope, 2000) == doctest::Approx(0x4000 / 32767.0F).epsilon(0.01));
  CHECK_FALSE(envelope.silentForGood());
}

TEST_CASE("a slow attack takes its time, and slows down near the top when exponential") {
  Envelope linear{{.adsr1 = first(false, 11, 0, 0, 15), .adsr2 = HoldSustain}};
  Envelope exponential{{.adsr1 = first(true, 11, 0, 0, 15), .adsr2 = HoldSustain}};
  const float early = levelAfter(linear, 1000);
  CHECK(early > 0.0F);
  CHECK(early < 0.5F);
  CHECK(levelAfter(exponential, 1000) == doctest::Approx(early));
  // Past 0x6000 the exponential attack climbs a quarter as fast.
  const float linearLater = levelAfter(linear, 3500);
  CHECK(linearLater < 1.0F);
  CHECK(linearLater > levelAfter(exponential, 3500));
}

TEST_CASE("a falling sustain ends the envelope in silence, linear or exponential") {
  for (const bool exponential : {false, true}) {
    Envelope envelope{
        {.adsr1 = first(false, 0, 0, 0, 15), .adsr2 = second(exponential, true, 4, 0)}};
    std::size_t frames = 0;
    while (!envelope.silentForGood() && frames < 480000) {
      static_cast<void>(envelope.advance());
      ++frames;
    }
    CHECK(envelope.silentForGood());
    CHECK(envelope.advance() == 0.0F);
  }
}

TEST_CASE("the slowest attack steps once and then waits about a million ticks") {
  Envelope envelope{{.adsr1 = first(false, 0x1F, 3, 0, 15), .adsr2 = HoldSustain}};
  const float level = levelAfter(envelope, 48000);
  CHECK(level > 0.0F);
  CHECK(level == doctest::Approx(4.0F / 32767.0F));
}

TEST_CASE("an increasing sustain climbs back from the sustain level to full") {
  Envelope envelope{{.adsr1 = first(false, 0, 0, 0, 3), .adsr2 = second(false, false, 4, 0)}};
  CHECK(levelAfter(envelope, 6) < 0.5F);
  CHECK(levelAfter(envelope, 4800) == doctest::Approx(1.0F));
  CHECK_FALSE(envelope.silentForGood());
}

TEST_CASE("at the top sustain level the decay is skipped and the level stays full") {
  Envelope envelope{{.adsr1 = first(false, 0, 0, 0, 15), .adsr2 = HoldSustain}};
  CHECK(levelAfter(envelope, 4) == doctest::Approx(1.0F));
  CHECK(levelAfter(envelope, 4800) == doctest::Approx(1.0F).epsilon(0.001));
}
