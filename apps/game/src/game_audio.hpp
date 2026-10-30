#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/audio/audio.hpp>
#include <dwe/audio/effect_bank.hpp>
#include <dwe/ui/ui.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::app {

// The game's audio on the default sound device, or on the null output, with a warning, when there
// is none (a machine without sound, the routes' virtual display): the game plays the same either
// way.
[[nodiscard]] audio::Audio openGameAudio();

// The UI's cues, which ui/ui_sounds.yaml names cursor, confirm, cancel, box_open and box_close (in
// ui::UiCue's order).
inline constexpr std::size_t MenuCueCount = 5;
inline constexpr std::string_view MenuSoundsId = "ui/ui_sounds";

// A sound of a bank of effects: the bank's folder under sound/ and the sound's value in it.
struct MenuSound {
  std::string bank;
  std::uint32_t sound = 0;
};

// ui/ui_sounds.yaml read cue by cue: a cue that is missing or wrong has no sound and a problem, and
// the others still sound.
struct MenuSoundTable {
  std::array<std::optional<MenuSound>, MenuCueCount> cues;
  std::vector<std::string> problems;
};

[[nodiscard]] MenuSoundTable parseMenuSounds(std::string_view yaml);

// Plays the menus' sounds (docs/architecture/audio.md#64-el-tramo-2-en-detalle). It reads
// ui/ui_sounds.yaml and loads its banks when made, and logs every problem then, once: a cue that
// cannot play stays silent and the game goes on.
class MenuSounds {
public:
  // Both must outlive it.
  MenuSounds(audio::Audio& audio, const assets::VirtualFileSystem& files);

  void play(ui::UiCue cue);
  void play(std::span<const ui::UiCue> cues);

private:
  audio::Audio* audio_;
  std::map<std::string, audio::EffectBank, std::less<>> banks_;
  std::array<std::optional<MenuSound>, MenuCueCount> cues_;
};

} // namespace dwe::app
