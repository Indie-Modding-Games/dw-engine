#pragma once

#include <dwe/assets/music.hpp>
#include <dwe/audio/mixing.hpp>
#include <dwe/audio/output.hpp>
#include <dwe/audio/sound_font.hpp>
#include <dwe/audio/voice.hpp>
#include <dwe/core/error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace dwe::audio {

// A song to play on the Music bus.
struct MusicRequest {
  std::shared_ptr<SoundFont> font;
  std::shared_ptr<const assets::Song> song;
  // Whether it loops or stops at its end (sound.c: seqPlay).
  bool loops = true;
  // From 0 to 1 (values outside are clamped), before the volumes of the bus.
  float gain = 1.0F;
};

// The game thread's side of the audio: it asks for voices and volumes, and the mixer applies them
// on the audio thread. Orders take effect together at the next update, in the order they were
// given. The game never waits on the audio thread.
class Audio {
public:
  // Starts output pulling from a new mixer, with every volume at 1.
  explicit Audio(std::unique_ptr<Output> output);
  Audio(Audio&&) noexcept;
  // Assigning would release the old mixer while its output may still be pulling from it.
  Audio& operator=(Audio&&) = delete;
  Audio(const Audio&) = delete;
  Audio& operator=(const Audio&) = delete;
  ~Audio();

  // Plays a clip; with every slot in use the oldest voice gives up its slot. Fails when the clip is
  // not playable, the pitch is outside [MinPitch, MaxPitch], the pan is not finite or this tick's
  // orders are full.
  core::Result<VoiceId> play(const PlayRequest& request);
  // Plays every request or, when any of them would fail, none.
  core::Result<std::vector<VoiceId>> playAll(std::span<const PlayRequest> requests);
  // Fades a voice out and releases it. A voice that already ended is ignored.
  void stop(VoiceId voice);
  // Replaces the music with a song from its start, a hard cut as in the original (sound.c:
  // playMusic). Fails when the font or the song is missing or this tick's orders are full.
  core::Result<void> playMusic(const MusicRequest& request);
  // Cuts the music (sound.c: stopBGM).
  void stopMusic();
  // Sets a volume, from 0 to 1 (values outside are clamped); the mixer ramps to it.
  void setVolume(VolumeControl control, float volume);
  [[nodiscard]] float volume(VolumeControl control) const;

  // Turns the sound chip's reverb on or off (on from the start, as the original sets it at boot:
  // sound.c: initializeMusic). Off, the voices still carry their send but nothing is heard of it.
  void setReverb(bool enabled);
  [[nodiscard]] bool reverb() const { return reverb_; }

  // Whether a voice has not reported its end yet.
  [[nodiscard]] bool isActive(VoiceId voice) const;
  [[nodiscard]] std::size_t activeVoiceCount() const;
  // The clip frame a voice had reached when the audio thread last mixed it (it keeps the last one
  // after the voice ends), or nothing before it started or once another voice took its slot. A
  // clock for what must follow the sound, like a movie's frames.
  [[nodiscard]] std::optional<std::uint64_t> position(VoiceId voice) const;

  // Once per game frame: takes the voices that ended, sends this frame's orders and lets the output
  // advance by seconds.
  void update(double seconds);

private:
  struct Shared;
  [[nodiscard]] static core::Result<void> checkRequest(const PlayRequest& request);
  struct Slot {
    std::uint32_t generation = 0;
    bool active = false;
    std::uint64_t startOrder = 0;
  };

  [[nodiscard]] std::uint32_t takeSlot();
  void collectEndedVoices();
  void logGuards();
  // Lets go of the music the mixer no longer plays.
  void releaseTakenMusic();

  // A song handed to the mixer, held until a later music order is taken.
  struct HeldMusic {
    std::uint64_t serial = 0;
    std::shared_ptr<SoundFont> font;
    std::shared_ptr<const assets::Song> song;
  };

  std::unique_ptr<Shared> shared_;
  // Declared after shared_, so it stops pulling from the mixer before the mixer goes.
  std::unique_ptr<Output> output_;
  std::vector<Slot> slots_;
  std::uint64_t nextStartOrder_ = 0;
  // Holds each playing clip until the mixer reports its voice ended: the audio thread never frees.
  std::unordered_map<std::uint64_t, std::shared_ptr<const Clip>> playingClips_;
  std::vector<HeldMusic> heldMusic_;
  std::uint64_t nextMusicSerial_ = 0;
  std::array<float, VolumeControlCount> volumes_{1.0F, 1.0F, 1.0F};
  bool reverb_ = true;
  std::size_t droppedOrders_ = 0;
  std::size_t stolenVoices_ = 0;
};

} // namespace dwe::audio
