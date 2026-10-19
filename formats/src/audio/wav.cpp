#include <dwe/formats/audio/wav.hpp>
#include <dwe/formats/byte_reader.hpp>

#include <format>
#include <string_view>

#include "riff_writer.hpp"

namespace dwe::formats::audio {

namespace {

constexpr std::uint32_t PcmFormat = 1;
constexpr std::uint32_t BytesPerSample = 2;
constexpr std::uint32_t BitsPerSample = 16;
constexpr std::uint32_t NanosecondsPerSecond = 1'000'000'000;
constexpr std::size_t ChunkHeaderSize = 8;
constexpr std::size_t FormatSize = 16;
constexpr std::size_t SamplerSize = 36;
constexpr std::size_t SampleLoopSize = 24;
constexpr std::uint32_t MaxMidiNote = 127;

// Reads the fmt chunk into the file's rate and channels.
core::Result<void> readFormat(const ByteReader& chunk, std::size_t size, WavFile& wav) {
    if (size < FormatSize) {
        return core::fail("its fmt chunk is too short");
    }
    const auto tag = chunk.u16(0);
    const auto channels = chunk.u16(2);
    const auto rate = chunk.u32(4);
    const auto align = chunk.u16(12);
    const auto bits = chunk.u16(14);
    if (!tag || !channels || !rate || !align || !bits) {
        return core::fail("its fmt chunk is too short");
    }
    if (*tag != PcmFormat || *bits != BitsPerSample) {
        return core::fail(std::format("it is not 16-bit PCM (format {}, {} bits)", *tag, *bits));
    }
    if (*channels < 1 || *channels > 2) {
        return core::fail(std::format("it has {} channels; only 1 or 2 are read", *channels));
    }
    if (*rate == 0 || *rate > MaxWavSampleRate) {
        return core::fail(std::format("its sample rate {} is out of range", *rate));
    }
    if (*align != *channels * BytesPerSample) {
        return core::fail("its block size does not match its channels");
    }
    wav.sampleRate = *rate;
    wav.channels = *channels;
    return {};
}

// Reads the unity note and the first loop of a smpl chunk; the loop's end is inclusive there.
core::Result<void> readSampler(const ByteReader& chunk, std::size_t size, WavFile& wav) {
    const auto note = chunk.u32(12);
    const auto loops = chunk.u32(28);
    if (size < SamplerSize || !note || !loops) {
        return core::fail("its smpl chunk is too short");
    }
    if (*note > MaxMidiNote) {
        return core::fail(std::format("its unity note {} is not a MIDI note", *note));
    }
    wav.unityNote = static_cast<std::uint8_t>(*note);
    if (*loops == 0) {
        return {};
    }
    const auto start = chunk.u32(SamplerSize + 8);
    const auto end = chunk.u32(SamplerSize + 12);
    if (size < SamplerSize + SampleLoopSize || !start || !end) {
        return core::fail("its smpl chunk is too short for its loop");
    }
    if (*start > *end) {
        return core::fail("its loop ends before it starts");
    }
    wav.loop = SampleLoop{.start = *start, .end = std::size_t{*end} + 1};
    return {};
}

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

core::Result<WavFile> readWav(std::span<const std::byte> bytes) {
    const ByteReader file{bytes};
    const auto riff = file.chars(0, 4);
    const auto riffSize = file.u32(4);
    const auto wave = file.chars(8, 4);
    if (!riff || *riff != "RIFF" || !wave || *wave != "WAVE" || !riffSize) {
        return core::fail("not a RIFF WAVE file");
    }
    if (*riffSize < 4 || *riffSize > bytes.size() - ChunkHeaderSize) {
        return core::fail("its RIFF size does not match the file");
    }
    const std::size_t end = ChunkHeaderSize + *riffSize;
    WavFile wav;
    std::optional<std::span<const std::byte>> data;
    for (std::size_t at = 12; at < end;) {
        const auto id = file.chars(at, 4);
        const auto size = file.u32(at + 4);
        if (!id || !size || at + ChunkHeaderSize > end || *size > end - at - ChunkHeaderSize) {
            return core::fail(std::format("the chunk at {} does not fit in the file", at));
        }
        const auto body = bytes.subspan(at + ChunkHeaderSize, *size);
        const ByteReader chunk{body};
        if (*id == "fmt ") {
            if (wav.channels != 0) {
                return core::fail("it has two fmt chunks");
            }
            if (auto read = readFormat(chunk, body.size(), wav); !read) {
                return std::unexpected{read.error()};
            }
        } else if (*id == "smpl") {
            if (auto read = readSampler(chunk, body.size(), wav); !read) {
                return std::unexpected{read.error()};
            }
        } else if (*id == "data") {
            if (data) {
                return core::fail("it has two data chunks");
            }
            data = body;
        }
        at += ChunkHeaderSize + *size + (*size & 1U);
    }
    if (wav.channels == 0 || !data) {
        return core::fail(wav.channels == 0 ? "it has no fmt chunk" : "it has no data chunk");
    }
    const std::size_t frameSize = std::size_t{wav.channels} * BytesPerSample;
    if (data->size() % frameSize != 0) {
        return core::fail("its data is not a whole number of frames");
    }
    wav.pcm.resize(data->size() / BytesPerSample);
    for (std::size_t i = 0; i < wav.pcm.size(); ++i) {
        const auto low = std::to_integer<std::uint16_t>((*data)[i * BytesPerSample]);
        const auto high = std::to_integer<std::uint16_t>((*data)[(i * BytesPerSample) + 1]);
        wav.pcm[i] = static_cast<std::int16_t>(low | (high << 8U));
    }
    if (wav.loop && wav.loop->end > data->size() / frameSize) {
        return core::fail("its loop goes past its last frame");
    }
    return wav;
}

} // namespace dwe::formats::audio
