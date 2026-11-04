#include "weather_ambience.hpp"

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/sound.hpp>
#include <dwe/core/log.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace dwe::app {

namespace {

// The stand-ins: a steady high hiss of the factory maps' bank, and a rising and fading gust of the
// frozen area's bank (sound/esall/<map sound id>).
constexpr std::string_view RainSound = "sound/esall/008/sample_004";
constexpr std::string_view GustSound = "sound/esall/011/sample_004";
// The rain loop's crossfade, in seconds: its end fades into its start.
constexpr double CrossfadeSeconds = 0.25;
constexpr float RainLoudest = 0.5F;
constexpr float GustLoudest = 0.45F;

std::shared_ptr<audio::Clip> loadClip(const assets::VirtualFileSystem& files, std::string_view id) {
  auto parsed = assets::AssetId::parse(id);
  if (!parsed) {
    return nullptr;
  }
  const assets::AssetSource* source = files.resolve(*parsed);
  if (source == nullptr) {
    core::logDebug("weather: no sound {}: the ambience stays silent", id);
    return nullptr;
  }
  auto bytes = files.read(*parsed);
  if (!bytes) {
    return nullptr;
  }
  auto sound =
      source->file.extension() == ".wav" ? assets::decodeWav(*bytes) : assets::decodeOpus(*bytes);
  if (!sound || sound->channels == 0 || sound->pcm.empty()) {
    core::logWarning("weather: sound {} does not decode", id);
    return nullptr;
  }
  return std::make_shared<audio::Clip>(audio::Clip{.sampleRate = sound->sampleRate,
                                                   .channels = sound->channels,
                                                   .pcm = std::move(sound->pcm),
                                                   .loop = std::nullopt});
}

// Makes a clip loop whole without a click: its last frames are faded into its first ones and cut.
void loopWithCrossfade(audio::Clip& clip) {
  const std::uint64_t frames = clip.frameCount();
  const auto fade = std::min<std::uint64_t>(
      static_cast<std::uint64_t>(CrossfadeSeconds * clip.sampleRate), frames / 3);
  if (fade == 0) {
    clip.loop = audio::ClipLoop{.start = 0, .length = frames};
    return;
  }
  const std::uint64_t kept = frames - fade;
  for (std::uint64_t frame = 0; frame < fade; ++frame) {
    const float in = static_cast<float>(frame) / static_cast<float>(fade);
    for (std::uint16_t channel = 0; channel < clip.channels; ++channel) {
      const std::size_t start = (frame * clip.channels) + channel;
      const std::size_t tail = ((kept + frame) * clip.channels) + channel;
      const float mixed = (static_cast<float>(clip.pcm[start]) * in) +
                          (static_cast<float>(clip.pcm[tail]) * (1.0F - in));
      clip.pcm[start] = static_cast<std::int16_t>(std::clamp(mixed, -32768.0F, 32767.0F));
    }
  }
  clip.pcm.resize(kept * clip.channels);
  clip.loop = audio::ClipLoop{.start = 0, .length = kept};
}

std::uint64_t mix(std::uint64_t value) {
  value += 0x9E3779B97F4A7C15ULL;
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

float unit(std::uint64_t value) {
  return static_cast<float>(mix(value) >> 40U) / static_cast<float>(1U << 24U);
}

} // namespace

WeatherAmbience::WeatherAmbience(audio::Audio* audio, const assets::VirtualFileSystem& files)
    : audio_(audio), files_(&files) {}

WeatherAmbience::~WeatherAmbience() {
  stop();
}

float WeatherAmbience::rainGain(float rain) {
  const float steps = std::floor(std::clamp(rain, 0.0F, 1.0F) / RainStep);
  return steps * RainStep * RainLoudest;
}

void WeatherAmbience::loadClips() {
  loaded_ = true;
  if (auto rain = loadClip(*files_, RainSound)) {
    loopWithCrossfade(*rain);
    if (audio::checkClip(*rain)) {
      rain_ = std::move(rain);
    }
  }
  gust_ = loadClip(*files_, GustSound);
}

void WeatherAmbience::update(const game::weather::WeatherLook& look, double seconds) {
  if (audio_ == nullptr) {
    return;
  }
  if (!loaded_ && (look.rain > 0.0F || look.snow > 0.0F)) {
    loadClips();
  }
  const float gain = rainGain(look.rain);
  if (rain_ && gain != rainPlaying_) {
    if (rainVoice_) {
      audio_->stop(*rainVoice_);
      rainVoice_.reset();
    }
    rainPlaying_ = gain;
    if (gain > 0.0F) {
      if (auto voice = audio_->play({.clip = rain_, .gain = gain})) {
        rainVoice_ = *voice;
      }
    }
  }
  if (!gust_ || look.snow <= 0.0F) {
    return;
  }
  gustSeconds_ -= seconds;
  if (gustSeconds_ > 0.0) {
    return;
  }
  ++gusts_;
  // Every 3 to 8 seconds, at its own volume and pitch.
  gustSeconds_ = 3.0 + (5.0 * unit(gusts_ * 3));
  const float gustGain = GustLoudest * look.snow * (0.5F + (0.5F * unit((gusts_ * 3) + 1)));
  const float pitch = 0.8F + (0.35F * unit((gusts_ * 3) + 2));
  static_cast<void>(audio_->play({.clip = gust_, .gain = gustGain, .pitch = pitch}));
}

void WeatherAmbience::stop() {
  if (audio_ != nullptr && rainVoice_) {
    audio_->stop(*rainVoice_);
  }
  rainVoice_.reset();
  rainPlaying_ = 0.0F;
}

} // namespace dwe::app
