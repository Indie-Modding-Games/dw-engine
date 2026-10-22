#pragma once

#include <cstddef>
#include <cstdint>

// The mixer's fixed format and its limits (docs/architecture/audio.md, "Mezclador y voces").
namespace dwe::audio {

// The mixer works at the rate of the cooked Opus sounds, in interleaved stereo float.
inline constexpr std::uint32_t MixSampleRate = 48000;
inline constexpr std::size_t OutputChannels = 2;

// Not the original's 24 channels: a generous guard against a script or a mod that never stops
// asking for sounds. Past it, a new voice replaces the oldest one.
inline constexpr std::size_t MaxVoices = 256;
// A guard too: the orders one tick may send to the audio thread.
inline constexpr std::size_t CommandQueueCapacity = 1024;
// Guards, not limits of the original: a clip's rate and a voice's pitch, so a voice never skips
// more than a few hundred frames of its clip per frame of output.
inline constexpr std::uint32_t MaxClipSampleRate = 192000;
inline constexpr float MinPitch = 1.0F / 64.0F;
inline constexpr float MaxPitch = 64.0F;
// The most frames the mixer renders in one pass; longer requests are rendered in pieces.
inline constexpr std::size_t MaxRenderFrames = 1024;

// A volume change takes this long, so it does not click.
inline constexpr double VolumeRampSeconds = 0.010;
// A stopped voice fades out this long before it is released, so it does not click.
inline constexpr double StopFadeSeconds = 0.005;

// The group a voice is mixed in, with its own volume.
enum class Bus : std::uint8_t {
  Music,
  Effects,
};
inline constexpr std::size_t BusCount = 2;

// The volumes the player sets: the master one scales both buses.
enum class VolumeControl : std::uint8_t {
  Master,
  Music,
  Effects,
};
inline constexpr std::size_t VolumeControlCount = 3;

} // namespace dwe::audio
