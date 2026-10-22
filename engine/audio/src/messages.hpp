#pragma once

#include <string_view>

// The error and log messages of audio. Templates take their arguments in the order of their
// placeholders.
namespace dwe::audio::messages {

// Clips (clip.cpp).
inline constexpr std::string_view ClipChannels = "a clip has 1 or 2 channels, not {}";
inline constexpr std::string_view ClipRate = "a clip's rate is {} Hz, and clips play at 1 to {} Hz";
inline constexpr std::string_view ClipPartialFrame =
    "a clip of {} channels cannot hold {} samples: the last frame is cut";
inline constexpr std::string_view ClipEmpty = "a clip needs at least one frame";
inline constexpr std::string_view ClipLoopOutside =
    "the loop of a clip ({} frames from frame {}) is empty or ends past its {} frames";

// Voices and orders (audio.cpp).
inline constexpr std::string_view PlayNoClip = "a voice needs a clip";
inline constexpr std::string_view PlayBadPitch = "a voice's pitch is from {} to {}, not {}";
inline constexpr std::string_view PlayBadPan = "a voice's pan is from -1 to 1, not {}";
inline constexpr std::string_view OrdersFull =
    "audio: the orders of this frame are full ({}), so the sound was not played";
inline constexpr std::string_view OrdersDropped =
    "audio: {} orders were dropped this frame: the queue holds {}";
inline constexpr std::string_view VoicesStolen =
    "audio: {} voices replaced the oldest playing ones this frame: the limit is {} voices";

// The device (sdl_output.cpp).
inline constexpr std::string_view DeviceUnavailable = "could not open the sound device: {}";

} // namespace dwe::audio::messages
