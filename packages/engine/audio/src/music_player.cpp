#include "music_player.hpp"

#include <tsf.h>

#include <algorithm>
#include <cmath>

#include "synth_render.hpp"

namespace dwe::audio {

namespace {

using Kind = assets::SongEvent::Kind;

// A song without a tempo event plays at 120 beats per minute, as the MIDI standard says.
constexpr std::uint32_t DefaultTempo = 500000;
constexpr double MicrosecondsPerSecond = 1'000'000.0;
constexpr float MidiMax = 127.0F;
constexpr int PitchCentre = 8192;
constexpr std::uint8_t ChannelVolume = 7;
constexpr std::uint8_t ResetControllers = 121;

} // namespace

void MusicPlayer::start(const MusicOrder& order) noexcept {
  stop();
  if (order.synth == nullptr || order.song == nullptr) {
    return;
  }
  synth_ = order.synth;
  song_ = order.song;
  loops_ = order.loops;
  gain_ = order.gain;
  resetChannels();
  // The loop runs from the loopStart marker, or the start, to the loopEnd marker, or the end
  // (the sound library's sequencer, sound.c: seqPlay).
  loopStart_ = song_->loopStart.value_or(0);
  loopEnd_ = song_->loopEnd.value_or(song_->endTick);
  if (loopEnd_ <= loopStart_) {
    loopStart_ = 0;
    loopEnd_ = song_->endTick;
  }
  // A song with no length has nothing to loop.
  loops_ = loops_ && loopEnd_ > loopStart_;
  next_ = 0;
  tick_ = 0.0;
  setTempo(DefaultTempo);
  sequencing_ = song_->division > 0 && !song_->events.empty();
}

void MusicPlayer::stop() noexcept {
  if (synth_ != nullptr) {
    for (int channel = 0; channel < MusicChannels; ++channel) {
      tsf_channel_sounds_off_all(synth_, channel);
    }
  }
  synth_ = nullptr;
  song_ = nullptr;
  sequencing_ = false;
}

void MusicPlayer::mixInto(std::span<float> frames, std::span<float> send,
                          std::span<const float> gains) noexcept {
  if (synth_ == nullptr) {
    return;
  }
  std::size_t frame = 0;
  while (frame < gains.size()) {
    const std::size_t count = std::min(BlockFrames, gains.size() - frame);
    if (sequencing_) {
      sequence();
    }
    const std::size_t samples = count * OutputChannels;
    renderSynth(synth_, std::span{block_}.first(samples), std::span{reverbBlock_}.first(samples),
                voiceBlock_);
    for (std::size_t i = 0; i < count; ++i) {
      const float gain = gain_ * gains[frame + i];
      for (std::size_t channel = 0; channel < OutputChannels; ++channel) {
        const std::size_t at = ((frame + i) * OutputChannels) + channel;
        frames[at] += block_[(i * OutputChannels) + channel] * gain;
        send[at] += reverbBlock_[(i * OutputChannels) + channel] * gain;
      }
    }
    // A song that played out stops counting, so the tick never outgrows its type.
    tick_ = std::min(tick_ + (ticksPerFrame_ * static_cast<double>(count)),
                     static_cast<double>(song_->endTick));
    frame += count;
  }
}

void MusicPlayer::resetChannels() noexcept {
  for (int channel = 0; channel < MusicChannels; ++channel) {
    tsf_channel_sounds_off_all(synth_, channel);
    tsf_channel_midi_control(synth_, channel, ResetControllers, 0);
    tsf_channel_set_presetnumber(synth_, channel, 0, 0);
    tsf_channel_set_pitchwheel(synth_, channel, PitchCentre);
  }
}

// Sends every event up to the current tick; at the loop's end it goes back to its start, or, for a
// song that plays once, stops sequencing at the end and lets the last notes ring out.
void MusicPlayer::sequence() noexcept {
  const auto& events = song_->events;
  while (true) {
    const auto now = static_cast<std::uint32_t>(tick_);
    const std::uint32_t last = loops_ ? std::min(now, loopEnd_) : now;
    if (next_ < events.size() && events[next_].tick <= last) {
      dispatch(events[next_++]);
      continue;
    }
    if (!loops_ || now < loopEnd_) {
      break;
    }
    // Back into the loop in one step, however far past its end a long block went.
    tick_ = static_cast<double>(loopStart_) + std::fmod(tick_ - static_cast<double>(loopStart_),
                                                        static_cast<double>(loopEnd_ - loopStart_));
    next_ = static_cast<std::size_t>(
        std::ranges::lower_bound(events, loopStart_, {}, &assets::SongEvent::tick) -
        events.begin());
  }
  if (!loops_ && next_ >= events.size()) {
    sequencing_ = false;
  }
}

void MusicPlayer::dispatch(const assets::SongEvent& event) noexcept {
  const int channel = event.channel;
  switch (event.kind) {
  case Kind::NoteOn:
    tsf_channel_note_on(synth_, channel, event.key, static_cast<float>(event.value) / MidiMax);
    break;
  case Kind::NoteOff:
    tsf_channel_note_off(synth_, channel, event.key);
    break;
  case Kind::Control:
    // The original's channel volume is linear; the synthesizer's MIDI volume is a cubic curve.
    if (event.key == ChannelVolume) {
      tsf_channel_set_volume(synth_, channel, static_cast<float>(event.value) / MidiMax);
    } else {
      tsf_channel_midi_control(synth_, channel, event.key, static_cast<int>(event.value));
    }
    break;
  case Kind::Program:
    tsf_channel_set_presetnumber(synth_, channel, static_cast<int>(event.value), 0);
    break;
  case Kind::PitchBend:
    tsf_channel_set_pitchwheel(synth_, channel, static_cast<int>(event.value));
    break;
  case Kind::Tempo:
    setTempo(event.value);
    break;
  }
}

void MusicPlayer::setTempo(std::uint32_t microsecondsPerQuarter) noexcept {
  const std::uint32_t tempo = microsecondsPerQuarter > 0 ? microsecondsPerQuarter : DefaultTempo;
  ticksPerFrame_ = static_cast<double>(song_->division) * MicrosecondsPerSecond /
                   (static_cast<double>(tempo) * static_cast<double>(MixSampleRate));
}

} // namespace dwe::audio
