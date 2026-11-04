#include "volume_setting.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <variant>

namespace dwe::app {

float volumeGain(int step) {
  const float share = static_cast<float>(std::clamp(step, 0, VolumeSteps)) / VolumeSteps;
  return share * share;
}

int volumeStep(std::optional<int> saved) {
  return std::clamp(saved.value_or(VolumeSteps), 0, VolumeSteps);
}

VolumeChanger::VolumeChanger(UserSettings& settings,
                             std::optional<std::filesystem::path> settingsFile, audio::Audio& audio)
    : settings_{&settings}, settingsFile_{std::move(settingsFile)}, audio_{&audio} {
  apply();
}

bool VolumeChanger::handle(const ui::UiCommand& command, ui::Ui& ui) {
  std::optional<int>* saved = nullptr;
  if (command.name == MusicVolumeSetting) {
    saved = &settings_->musicVolume;
  } else if (command.name == EffectsVolumeSetting) {
    saved = &settings_->effectsVolume;
  } else {
    return false;
  }
  if (const double* number = std::get_if<double>(&command.value);
      number != nullptr && std::isfinite(*number)) {
    const int step =
        static_cast<int>(std::clamp(std::round(*number), 0.0, static_cast<double>(VolumeSteps)));
    if (step != volumeStep(*saved)) {
      *saved = step;
      apply();
      if (settingsFile_) {
        saveUserSettings(*settingsFile_, *settings_);
      }
    }
  }
  publish(ui);
  return true;
}

void VolumeChanger::publish(ui::Ui& ui) const {
  ui.setSetting(MusicVolumeSetting, static_cast<double>(musicStep()));
  ui.setSetting(EffectsVolumeSetting, static_cast<double>(effectsStep()));
}

void VolumeChanger::apply() {
  audio_->setVolume(audio::VolumeControl::Music, volumeGain(musicStep()));
  audio_->setVolume(audio::VolumeControl::Effects, volumeGain(effectsStep()));
}

} // namespace dwe::app
