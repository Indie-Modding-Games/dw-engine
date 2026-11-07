// The music's and the effects' volumes (docs/modulos/apps/game.md, «Volumen»): the options page's
// rows reach the audio's Music and Effects controls and the user's settings.

#include <dwe/audio/audio.hpp>
#include <dwe/audio/output.hpp>
#include <dwe/test_support/temp_folder.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "pause_menu_ui.hpp"
#include "user_settings.hpp"
#include "volume_setting.hpp"

namespace dwe::app {
namespace {

using audio::VolumeControl;

audio::Audio nullAudio() {
  return audio::Audio{std::make_unique<audio::NullOutput>()};
}

} // namespace

TEST_CASE("a volume step's gain is its share squared, the top step the original's level") {
  CHECK(volumeGain(VolumeSteps) == doctest::Approx(1.0F));
  CHECK(volumeGain(5) == doctest::Approx(0.25F));
  CHECK(volumeGain(1) == doctest::Approx(0.01F));
  CHECK(volumeGain(0) == 0.0F);
  CHECK(volumeGain(-3) == 0.0F);
  CHECK(volumeGain(42) == doctest::Approx(1.0F));
  CHECK(volumeStep(std::nullopt) == VolumeSteps);
  CHECK(volumeStep(-1) == 0);
  CHECK(volumeStep(99) == VolumeSteps);
}

TEST_CASE("the saved volumes reach the audio at start, the full level when none is saved") {
  audio::Audio sound = nullAudio();
  UserSettings defaults;
  const VolumeChanger full{defaults, std::nullopt, sound};
  CHECK(sound.volume(VolumeControl::Music) == doctest::Approx(1.0F));
  CHECK(sound.volume(VolumeControl::Effects) == doctest::Approx(1.0F));

  UserSettings saved;
  saved.musicVolume = 5;
  saved.effectsVolume = 0;
  const VolumeChanger changer{saved, std::nullopt, sound};
  CHECK(sound.volume(VolumeControl::Music) == doctest::Approx(0.25F));
  CHECK(sound.volume(VolumeControl::Effects) == 0.0F);
  // The master stays the original's full level.
  CHECK(sound.volume(VolumeControl::Master) == doctest::Approx(1.0F));
}

TEST_CASE("the options page's volume rows change the audio at once and save the steps") {
  const test_support::TempFolder folder;
  const std::filesystem::path file = folder.path() / "settings.yaml";
  audio::Audio sound = nullAudio();
  UserSettings settings;
  settings.language = "es";
  VolumeChanger changer{settings, file, sound};
  test::MenuUi menu;
  changer.publish(*menu.ui);
  menu.ui->setSetting("pause_menu.start_page", std::string{"options"});
  menu.ui->show("pause_menu", true);
  REQUIRE(menu.ui->callScript("pause_menu", "show_tab", std::vector<std::string>{"", "sound"}));
  menu.ui->update();
  CHECK(menu.ui->elementText("pause_menu", "music-volume-value") == "10");
  std::ignore = menu.ui->takeCommands();
  // Music one step down, effects confirmed from the top: back to silence.
  REQUIRE(menu.ui->callScript("pause_menu", "adjust",
                              std::vector<std::string>{"", "music-volume", "-1"}));
  REQUIRE(menu.ui->callScript("pause_menu", "adjust",
                              std::vector<std::string>{"", "effects-volume", "1"}));
  for (const ui::UiCommand& command : menu.ui->takeCommands()) {
    CHECK(changer.handle(command, *menu.ui));
  }
  CHECK(changer.musicStep() == 9);
  CHECK(changer.effectsStep() == 0);
  CHECK(sound.volume(VolumeControl::Music) == doctest::Approx(0.81F));
  CHECK(sound.volume(VolumeControl::Effects) == 0.0F);
  CHECK(menu.ui->elementText("pause_menu", "music-volume-value") == "9");
  CHECK(menu.ui->elementText("pause_menu", "effects-volume-value") == "0");
  CHECK(loadUserSettings(file).musicVolume == std::optional<int>{9});
  CHECK(loadUserSettings(file).effectsVolume == std::optional<int>{0});

  // Out of range is clamped, another kind of value changes nothing, and other commands are not
  // the changer's.
  CHECK(changer.handle(ui::UiCommand{.name = std::string{MusicVolumeSetting}, .value = 25.0},
                       *menu.ui));
  CHECK(changer.musicStep() == VolumeSteps);
  CHECK(changer.handle(
      ui::UiCommand{.name = std::string{EffectsVolumeSetting}, .value = std::string{"loud"}},
      *menu.ui));
  CHECK(changer.effectsStep() == 0);
  CHECK_FALSE(changer.handle(ui::UiCommand{.name = "shadows", .value = 3.0}, *menu.ui));
}

TEST_CASE("the reverb starts on, as the original, and its row turns it off and saves it") {
  const test_support::TempFolder folder;
  const std::filesystem::path file = folder.path() / "settings.yaml";
  audio::Audio sound = nullAudio();
  UserSettings settings;
  VolumeChanger changer{settings, file, sound};
  CHECK(changer.reverb());
  CHECK(sound.reverb());
  test::MenuUi menu;
  changer.publish(*menu.ui);
  CHECK(
      changer.handle(ui::UiCommand{.name = std::string{ReverbSetting}, .value = false}, *menu.ui));
  CHECK_FALSE(changer.reverb());
  CHECK_FALSE(sound.reverb());
  CHECK(loadUserSettings(file).reverb == std::optional<bool>{false});
  // A value of another kind changes nothing.
  CHECK(changer.handle(ui::UiCommand{.name = std::string{ReverbSetting}, .value = 1.0}, *menu.ui));
  CHECK_FALSE(sound.reverb());

  // A saved choice reaches the audio at start.
  audio::Audio other = nullAudio();
  const VolumeChanger restored{settings, std::nullopt, other};
  CHECK_FALSE(other.reverb());
}

} // namespace dwe::app
