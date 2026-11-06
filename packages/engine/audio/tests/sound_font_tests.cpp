#include <dwe/audio/sound_font.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <span>
#include <vector>

#include "media_fixtures.hpp"

using dwe::audio::SoundFont;

TEST_CASE("a SoundFont loads from memory through the synthesizer's stream") {
  const std::vector<std::byte> sf2 = dwe::assets::testing::soundFontFile();
  const auto font = SoundFont::load(sf2);
  CHECK((font.has_value() && font.value()->synth() != nullptr));
}

TEST_CASE("a SoundFont cut short is refused before the synthesizer reads it") {
  const std::vector<std::byte> sf2 = dwe::assets::testing::soundFontFile();
  CHECK_FALSE(SoundFont::load(std::span{sf2}.first(sf2.size() / 2)).has_value());
}
