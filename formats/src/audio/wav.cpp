#include <dwe/formats/audio/wav.hpp>

#include "riff_writer.hpp"

namespace dwe::formats::audio {

namespace {

constexpr std::uint32_t PcmFormat = 1;
constexpr std::uint32_t BytesPerSample = 2;
constexpr std::uint32_t BitsPerSample = 16;
constexpr std::uint32_t NanosecondsPerSecond = 1'000'000'000;

} // namespace

std::vector<std::byte> writeWav(const WavSound& sound) {
    RiffWriter riff;
    const std::size_t riffAt = riff.beginList("RIFF", "WAVE");

    const std::size_t formatAt = riff.beginChunk("fmt ");
    riff.u16(PcmFormat);
    riff.u16(1);
    riff.u32(sound.sampleRate);
    riff.u32(sound.sampleRate * BytesPerSample);
    riff.u16(BytesPerSample);
    riff.u16(BitsPerSample);
    riff.endChunk(formatAt);

    if (sound.loop) {
        const std::size_t samplerAt = riff.beginChunk("smpl");
        riff.u32(0); // manufacturer
        riff.u32(0); // product
        riff.u32(sound.sampleRate == 0 ? 0 : NanosecondsPerSecond / sound.sampleRate);
        riff.u32(sound.unityNote);
        riff.u32(0); // pitch fraction
        riff.u32(0); // SMPTE format
        riff.u32(0); // SMPTE offset
        riff.u32(1); // loops
        riff.u32(0); // sampler data
        riff.u32(0); // cue point id
        riff.u32(0); // forward
        riff.u32(static_cast<std::uint32_t>(sound.loop->start));
        riff.u32(static_cast<std::uint32_t>(sound.loop->end > 0 ? sound.loop->end - 1 : 0));
        riff.u32(0); // fraction
        riff.u32(0); // play count: forever
        riff.endChunk(samplerAt);
    }

    riff.pcmChunk("data", sound.pcm);
    riff.endChunk(riffAt);
    return riff.take();
}

} // namespace dwe::formats::audio
