#pragma once

#include <dwe/audio/audio.hpp>
#include <dwe/ui/ui.hpp>

#include <filesystem>
#include <optional>
#include <string_view>

#include "user_settings.hpp"

namespace dwe::app {

// The music's and the effects' volumes (an engine addition; docs/modulos/apps/game.md, «Volumen»):
// the original has no sound option and plays everything at the sound library's full level
// (sound.c: initializeMusic, SsSetMVol(127, 127)). Each is a UI setting and the command, with the
// same name, that the options page sends: a step from 0 (silent) to VolumeSteps (the full level,
// the default), saved with the user's settings and applied to the audio's Music and Effects
// controls at once. The movies' sound plays on the Music bus, so it follows the music's volume.
inline constexpr std::string_view MusicVolumeSetting = "music_volume";
inline constexpr std::string_view EffectsVolumeSetting = "effects_volume";
inline constexpr int VolumeSteps = 10;
// The sound chip's reverb (docs/architecture/audio.md, "Reverberación"): on by default, as the
// original turns it on at boot (sound.c: initializeMusic); off plays every voice dry. A UI setting
// and its command, saved with the user's settings and applied to the audio at once.
inline constexpr std::string_view ReverbSetting = "reverb";

// A step's gain, from 0 to 1: the square of its share of VolumeSteps, so the steps sound evenly
// spaced (half the steps is about 12 dB down) instead of crowding at the top. Steps outside
// [0, VolumeSteps] are clamped.
[[nodiscard]] float volumeGain(int step);

// A saved step, clamped to [0, VolumeSteps], or VolumeSteps when none is saved.
[[nodiscard]] int volumeStep(std::optional<int> saved);

// Applies the volumes and the reverb to the audio, changes them while the game runs and saves them
// with the other settings (settings.yaml).
class VolumeChanger {
public:
  // Applies the saved volumes to the audio. settingsFile is where the choices are saved; nothing
  // for runs that must not touch the user's settings (autotests). The settings and the audio must
  // outlive the changer.
  VolumeChanger(UserSettings& settings, std::optional<std::filesystem::path> settingsFile,
                audio::Audio& audio);

  [[nodiscard]] int musicStep() const { return volumeStep(settings_->musicVolume); }
  [[nodiscard]] int effectsStep() const { return volumeStep(settings_->effectsVolume); }
  [[nodiscard]] bool reverb() const { return settings_->reverb.value_or(true); }

  // Takes a command; returns whether it was one of the two volumes or the reverb. A volume's number
  // is rounded to a step and clamped; the reverb takes a boolean; a value of another kind changes
  // nothing but is still taken. Publishes what is in use either way.
  bool handle(const ui::UiCommand& command, ui::Ui& ui);
  // Publishes both steps and the reverb.
  void publish(ui::Ui& ui) const;

private:
  void apply();

  UserSettings* settings_;
  std::optional<std::filesystem::path> settingsFile_;
  audio::Audio* audio_;
};

} // namespace dwe::app
