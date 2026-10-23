#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <optional>

// What the story orders read and change outside the runner
// (docs/architecture/interprete-historia.md, "Dónde vive"). The session implements them; the tests
// use fakes.
namespace dwe::game::story {

// The story's triggers and variables. The original has 800 triggers and 256 variables (script.h:
// ScriptState); that is the base set, and the owner of the state may hold more for mods, so it says
// how many it holds. A trigger or variable it does not hold is an error, which the script that
// asked for it sees as a Lua error.
class StoryStatePort {
public:
  StoryStatePort() = default;
  StoryStatePort(const StoryStatePort&) = delete;
  StoryStatePort& operator=(const StoryStatePort&) = delete;
  StoryStatePort(StoryStatePort&&) = delete;
  StoryStatePort& operator=(StoryStatePort&&) = delete;
  virtual ~StoryStatePort() = default;

  // How many triggers and variables the state holds: they are numbered from 0.
  [[nodiscard]] virtual std::int64_t triggerCount() const = 0;
  [[nodiscard]] virtual std::int64_t variableCount() const = 0;

  [[nodiscard]] virtual core::Result<bool> trigger(std::int64_t trigger) const = 0;
  virtual core::Result<void> setTrigger(std::int64_t trigger, bool set) = 0;
  [[nodiscard]] virtual core::Result<std::int64_t> variable(std::int64_t variable) const = 0;
  virtual core::Result<void> setVariable(std::int64_t variable, std::int64_t value) = 0;
};

// The session's random numbers, as the C library's rand() gave them to the original: 0 to
// RandomMax.
class StoryRandomPort {
public:
  static constexpr std::int64_t RandomMax = 0x7FFF;

  StoryRandomPort() = default;
  StoryRandomPort(const StoryRandomPort&) = delete;
  StoryRandomPort& operator=(const StoryRandomPort&) = delete;
  StoryRandomPort(StoryRandomPort&&) = delete;
  StoryRandomPort& operator=(StoryRandomPort&&) = delete;
  virtual ~StoryRandomPort() = default;

  // The next number, from 0 to RandomMax.
  virtual std::int64_t next() = 0;
};

// The sound effects the story asks for. A slot is the original's sound slot (sound.c: playSound):
// 0 system sounds, 1 the second system bank, 8 the bank of the map or the battle. The owner knows
// which bank each slot holds. Effects never wait and never fail a script: a sound that cannot play
// stays silent.
class SoundPort {
public:
  SoundPort() = default;
  SoundPort(const SoundPort&) = delete;
  SoundPort& operator=(const SoundPort&) = delete;
  SoundPort(SoundPort&&) = delete;
  SoundPort& operator=(SoundPort&&) = delete;
  virtual ~SoundPort() = default;

  virtual void playSound(std::int64_t slot, std::int64_t sound) = 0;
  // A sound whose voices are kept so it can be stopped before it ends (sound.c: playSound2, which
  // returns its voice mask): a handle for stopHeldSound, or nothing when it did not play. An owner
  // that cannot stop voices plays it as playSound does.
  virtual std::optional<std::int64_t> playHeldSound(std::int64_t slot, std::int64_t sound) {
    playSound(slot, sound);
    return std::nullopt;
  }
  // Stops the voices of a held sound (sound.c: thunkStopSoundMask); a handle that ended already
  // does nothing.
  virtual void stopHeldSound(std::int64_t /*held*/) {}
};

// The music the story asks for (script_interp.c: playBGM, resetBGM). A font is the original's music
// font number; which of its songs plays (day, night or a fixed one) the owner works out. Music
// never waits and never fails a script: a song that cannot play stays silent.
class MusicPort {
public:
  MusicPort() = default;
  MusicPort(const MusicPort&) = delete;
  MusicPort& operator=(const MusicPort&) = delete;
  MusicPort(MusicPort&&) = delete;
  MusicPort& operator=(MusicPort&&) = delete;
  virtual ~MusicPort() = default;

  // Plays a font's song from its start, even the one playing; 0xFF stops the music.
  virtual void playBgm(std::int64_t font) = 0;
  // Stops the music and forgets the font.
  virtual void resetBgm() = 0;
};

// The player's control while a script runs. Freezing takes control from the player and the partner,
// lets the camera go and stops game time; releasing gives them back (script_interp.c:
// scriptPauseGame, tickScript).
class FieldControlPort {
public:
  FieldControlPort() = default;
  FieldControlPort(const FieldControlPort&) = delete;
  FieldControlPort& operator=(const FieldControlPort&) = delete;
  FieldControlPort(FieldControlPort&&) = delete;
  FieldControlPort& operator=(FieldControlPort&&) = delete;
  virtual ~FieldControlPort() = default;

  virtual void freeze() = 0;
  virtual void release() = 0;
};

// The map changes a script makes. The owner fades the screen out, showing the new map's name if
// asked, enters the map at the exit and then tells the runner (StoryRunner::enterMap), which runs
// the new map's chain while the screen fades back in (map.c: scriptTickChangeMap).
class MapChangePort {
public:
  MapChangePort() = default;
  MapChangePort(const MapChangePort&) = delete;
  MapChangePort& operator=(const MapChangePort&) = delete;
  MapChangePort(MapChangePort&&) = delete;
  MapChangePort& operator=(MapChangePort&&) = delete;
  virtual ~MapChangePort() = default;

  virtual void startMapChange(std::int32_t map, std::int32_t exit, bool showName) = 0;
};

} // namespace dwe::game::story
