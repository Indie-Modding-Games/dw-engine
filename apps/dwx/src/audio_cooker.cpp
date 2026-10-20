#include "audio_cooker.hpp"

#include <dwe/assets/sound.hpp>
#include <dwe/formats/audio/wav.hpp>

#include <opusenc.h>

#include <algorithm>
#include <format>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace dwe::dwx {

namespace {

struct FreeComments {
    void operator()(OggOpusComments* comments) const { ope_comments_destroy(comments); }
};

struct FreeEncoder {
    void operator()(OggOpusEnc* encoder) const { ope_encoder_destroy(encoder); }
};

core::Result<void> check(int result, std::string_view what) {
    if (result != OPE_OK) {
        return core::fail(std::format("libopusenc cannot {}: {}", what, ope_strerror(result)));
    }
    return {};
}

std::uint64_t toOpusFrames(std::uint64_t frames, std::uint32_t rate) {
    return ((frames * assets::OpusSampleRate) + (rate / 2)) / rate;
}

} // namespace

OpusLoop opusLoopOf(std::uint64_t start, std::uint64_t end, std::uint64_t frames, std::uint32_t rate) {
    const std::uint64_t total = ((frames * assets::OpusSampleRate) + rate - 1) / rate;
    const std::uint64_t first = std::min(toOpusFrames(start, rate), total);
    const std::uint64_t last = std::min(toOpusFrames(end, rate), total);
    return OpusLoop{.start = first, .length = last - first};
}

core::Result<std::vector<std::byte>> cookAudio(std::span<const std::byte> source, const CookOptions& /*options*/) {
    const auto wav = formats::audio::readWav(source);
    if (!wav) {
        return std::unexpected{wav.error()};
    }
    const std::size_t frames = wav->pcm.size() / wav->channels;
    if (frames == 0) {
        return core::fail("the WAV has no samples");
    }
    if (std::cmp_greater(frames, std::numeric_limits<int>::max())) {
        return core::fail(std::format("the WAV has {} frames, more than libopusenc takes at once", frames));
    }

    const std::unique_ptr<OggOpusComments, FreeComments> comments{ope_comments_create()};
    if (!comments) {
        return core::fail("libopusenc cannot create the comments");
    }
    std::vector<std::pair<std::string, std::string>> tags{{"DWE_UNITY_NOTE", std::to_string(wav->unityNote)}};
    if (wav->loop) {
        const OpusLoop loop = opusLoopOf(wav->loop->start, wav->loop->end, frames, wav->sampleRate);
        if (loop.length > 0) {
            tags.emplace_back("LOOPSTART", std::to_string(loop.start));
            tags.emplace_back("LOOPLENGTH", std::to_string(loop.length));
        }
    }
    for (const auto& [name, value] : tags) {
        if (auto added = check(ope_comments_add(comments.get(), name.c_str(), value.c_str()), "add a comment");
            !added) {
            return std::unexpected{added.error()};
        }
    }

    int error = OPE_OK;
    const std::unique_ptr<OggOpusEnc, FreeEncoder> encoder{
        ope_encoder_create_pull(comments.get(), static_cast<opus_int32>(wav->sampleRate), wav->channels, 0, &error)};
    if (!encoder || error != OPE_OK) {
        return core::fail(std::format("libopusenc cannot create an encoder: {}", ope_strerror(error)));
    }
    const opus_int32 bitrate = OpusBitratePerChannel * wav->channels;
    // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg): libopusenc's and Opus's ctl interface is variadic.
    for (const auto& [result, what] :
         {std::pair{ope_encoder_ctl(encoder.get(), OPE_SET_SERIALNO(OpusSerial)), "set the serial number"},
          std::pair{ope_encoder_ctl(encoder.get(), OPE_SET_COMMENT_PADDING(0)), "drop the comment padding"},
          std::pair{ope_encoder_ctl(encoder.get(), OPUS_SET_BITRATE(bitrate)), "set the bitrate"},
          std::pair{ope_encoder_ctl(encoder.get(), OPUS_SET_COMPLEXITY(OpusComplexity)), "set the complexity"},
          std::pair{ope_encoder_ctl(encoder.get(), OPUS_SET_VBR(1)), "use a variable bitrate"}}) {
        // NOLINTEND(cppcoreguidelines-pro-type-vararg)
        if (auto set = check(result, what); !set) {
            return std::unexpected{set.error()};
        }
    }
    if (auto written =
            check(ope_encoder_write(encoder.get(), wav->pcm.data(), static_cast<int>(frames)), "encode the samples");
        !written) {
        return std::unexpected{written.error()};
    }
    if (auto drained = check(ope_encoder_drain(encoder.get()), "finish the stream"); !drained) {
        return std::unexpected{drained.error()};
    }
    std::vector<std::byte> ogg;
    unsigned char* page = nullptr;
    opus_int32 size = 0;
    while (ope_encoder_get_page(encoder.get(), &page, &size, 1) != 0) {
        const auto bytes = std::as_bytes(std::span{page, static_cast<std::size_t>(size)});
        ogg.insert(ogg.end(), bytes.begin(), bytes.end());
    }
    return ogg;
}

} // namespace dwe::dwx
