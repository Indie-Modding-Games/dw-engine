#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// A bank of sound effects as dwx describes it in sound/<bank>/bank_meta.yaml: the programs, their
// tones, the sample files and which tones each effect plays (docs/formats/audio.md).
namespace dwe::assets {

// A layer of a program: which sample it plays, at what root key, how loud, where and with which
// envelope (the chip's two ADSR registers).
struct SoundBankTone {
  std::uint32_t tone = 0;
  std::uint8_t root = 60;
  // Raises the pitch, in 128ths of a semitone; the chip keeps it to 16ths.
  std::uint8_t fineTune = 0;
  std::uint8_t volume = 127;
  std::uint8_t pan = 64;
  std::uint32_t sample = 0;
  std::uint16_t adsr1 = 0;
  std::uint16_t adsr2 = 0;
  // The chip's tone mode; bit 2 (ReverbMode) sends the tone to the reverb.
  std::uint8_t mode = 0;

  static constexpr std::uint8_t ReverbMode = 0x04;
  [[nodiscard]] bool reverb() const { return (mode & ReverbMode) != 0; }
};

struct SoundBankProgram {
  std::uint32_t program = 0;
  std::uint8_t volume = 127;
  std::uint8_t pan = 64;
  std::vector<SoundBankTone> tones;
};

struct SoundBankSample {
  std::uint32_t sample = 0;
  // The file name in the bank's folder, as dwx wrote it (sample_<n>.wav).
  std::string file;
  // The rate the sample was recorded at, which sets its pitch whatever its file was encoded at.
  std::uint32_t rate = 0;
};

// An effect the game asks for by value: a key of a program, and the tones of that program it plays.
struct SoundBankSound {
  std::uint32_t value = 0;
  std::uint32_t program = 0;
  std::uint8_t note = 60;
  std::vector<std::uint32_t> tones;
};

struct SoundBank {
  std::uint8_t volume = 127;
  std::vector<SoundBankProgram> programs;
  std::vector<SoundBankSample> samples;
  std::vector<SoundBankSound> sounds;

  [[nodiscard]] const SoundBankSound* sound(std::uint32_t value) const;
  [[nodiscard]] const SoundBankProgram* program(std::uint32_t number) const;
  [[nodiscard]] const SoundBankSample* sample(std::uint32_t number) const;
};

// Reads a bank_meta.yaml. Keys the player does not need are left alone. Every program, tone and
// sample an effect names must be there, so a bank that reads plays every one of its effects.
core::Result<SoundBank> readSoundBank(std::string_view yaml);

} // namespace dwe::assets
