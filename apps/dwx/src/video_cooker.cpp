#include "video_cooker.hpp"

#include <dwe/assets/media_headers.hpp>

#include <aom/aom_encoder.h>
#include <aom/aomcx.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace dwe::dwx {

namespace {

constexpr std::string_view StreamSignature = "YUV4MPEG2 ";
constexpr std::string_view FrameSignature = "FRAME";
constexpr std::size_t MaxHeaderLine = 1024;

// The source's header: its size and frame rate, and where its first frame starts.
struct Y4mHeader {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t fpsNumerator = 0;
  std::uint32_t fpsDenominator = 0;
  // 8, or 10 for C420p10 (16-bit little-endian samples).
  unsigned bitDepth = 8;
};

std::string_view textOf(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::optional<std::uint32_t> numberOf(std::string_view text) {
  std::uint32_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

// The line at offset, without its line end, and where the next starts.
core::Result<std::pair<std::string_view, std::size_t>> lineAt(std::span<const std::byte> source,
                                                              std::size_t offset) {
  const std::string_view rest = textOf(source.subspan(offset));
  const std::size_t end = rest.substr(0, MaxHeaderLine).find('\n');
  if (end == std::string_view::npos) {
    return core::fail(
        std::format("the Y4M has no line end within {} bytes of offset {}", MaxHeaderLine, offset));
  }
  return std::pair{rest.substr(0, end), offset + end + 1};
}

core::Result<Y4mHeader> readHeader(std::string_view line) {
  if (!line.starts_with(StreamSignature)) {
    return core::fail("not a YUV4MPEG2 video");
  }
  Y4mHeader header;
  for (std::size_t start = StreamSignature.size(); start < line.size();) {
    const std::size_t end = std::min(line.find(' ', start), line.size());
    const std::string_view token = line.substr(start, end - start);
    start = end + 1;
    if (token.empty()) {
      continue;
    }
    const std::string_view value = token.substr(1);
    switch (token.front()) {
    case 'W':
    case 'H': {
      const auto size = numberOf(value);
      if (!size || *size == 0 || *size > std::numeric_limits<std::uint16_t>::max()) {
        return core::fail(std::format("the Y4M parameter '{}' is not a size", token));
      }
      (token.front() == 'W' ? header.width : header.height) = *size;
      break;
    }
    case 'F': {
      const std::size_t colon = value.find(':');
      const auto numerator = numberOf(value.substr(0, colon));
      const auto denominator =
          colon == std::string_view::npos ? std::nullopt : numberOf(value.substr(colon + 1));
      if (!numerator || !denominator || *numerator == 0 || *denominator == 0) {
        return core::fail(std::format("the Y4M parameter '{}' is not a frame rate", token));
      }
      header.fpsNumerator = *numerator;
      header.fpsDenominator = *denominator;
      break;
    }
    case 'I':
      if (value != "p") {
        return core::fail("the Y4M is not progressive");
      }
      break;
    case 'C':
      if (value == "420p10") {
        header.bitDepth = 10;
      } else if (!value.starts_with("420") || value.starts_with("420p")) {
        return core::fail(std::format("the Y4M's chroma '{}' is not 8- or 10-bit 4:2:0", value));
      }
      break;
    default:
      break;
    }
  }
  if (header.width == 0 || header.height == 0 || header.fpsNumerator == 0) {
    return core::fail("the Y4M header has no size or frame rate");
  }
  return header;
}

void put16(std::vector<std::byte>& out, std::uint32_t value) {
  for (unsigned i = 0; i < 2; ++i) {
    out.push_back(static_cast<std::byte>((value >> (8U * i)) & 0xFFU));
  }
}

void put32(std::vector<std::byte>& out, std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::byte>((value >> (8U * i)) & 0xFFU));
  }
}

void put64(std::vector<std::byte>& out, std::uint64_t value) {
  for (unsigned i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::byte>((value >> (8U * i)) & 0xFFU));
  }
}

struct Encoder {
  aom_codec_ctx_t codec{};
  bool started = false;

  Encoder() = default;
  Encoder(const Encoder&) = delete;
  Encoder& operator=(const Encoder&) = delete;
  Encoder(Encoder&&) = delete;
  Encoder& operator=(Encoder&&) = delete;
  ~Encoder() {
    if (started) {
      aom_codec_destroy(&codec);
    }
  }
};

core::Result<void> check(aom_codec_ctx_t& codec, aom_codec_err_t result, std::string_view what) {
  if (result != AOM_CODEC_OK) {
    const char* detail = aom_codec_error_detail(&codec);
    return core::fail(std::format("libaom cannot {}: {}", what,
                                  detail != nullptr ? detail : aom_codec_err_to_string(result)));
  }
  return {};
}

// Takes the packets the encoder has ready into the IVF frames, counting them.
void collect(aom_codec_ctx_t& codec, std::vector<std::byte>& ivf, std::uint32_t& written) {
  aom_codec_iter_t iterator = nullptr;
  while (const aom_codec_cx_pkt_t* packet = aom_codec_get_cx_data(&codec, &iterator)) {
    if (packet->kind != AOM_CODEC_CX_FRAME_PKT) {
      continue;
    }
    const auto bytes = std::as_bytes(std::span{
        static_cast<const unsigned char*>(packet->data.frame.buf), packet->data.frame.sz});
    put32(ivf, static_cast<std::uint32_t>(bytes.size()));
    put64(ivf, static_cast<std::uint64_t>(packet->data.frame.pts));
    ivf.insert(ivf.end(), bytes.begin(), bytes.end());
    ++written;
  }
}

} // namespace

core::Result<std::vector<std::byte>> cookVideo(std::span<const std::byte> source,
                                               const CookOptions& /*options*/) {
  const auto first = lineAt(source, 0);
  if (!first) {
    return std::unexpected{first.error()};
  }
  auto header = readHeader(first->first);
  if (!header) {
    return std::unexpected{header.error()};
  }
  const std::size_t lumaSize = std::size_t{header->width} * header->height;
  const std::size_t chromaSize =
      std::size_t{(header->width + 1) / 2} * std::size_t{(header->height + 1) / 2};
  const bool tenBit = header->bitDepth == 10;
  const std::size_t sampleBytes = tenBit ? 2 : 1;
  const std::size_t pictureSize = (lumaSize + (2 * chromaSize)) * sampleBytes;

  aom_codec_iface_t* iface = aom_codec_av1_cx();
  aom_codec_enc_cfg_t config{};
  Encoder encoder;
  if (auto defaults =
          check(encoder.codec, aom_codec_enc_config_default(iface, &config, AOM_USAGE_GOOD_QUALITY),
                "give its default settings");
      !defaults) {
    return std::unexpected{defaults.error()};
  }
  config.g_w = header->width;
  config.g_h = header->height;
  config.g_timebase = {.num = static_cast<int>(header->fpsDenominator),
                       .den = static_cast<int>(header->fpsNumerator)};
  config.g_threads = 1;
  config.g_input_bit_depth = header->bitDepth;
  config.g_bit_depth = tenBit ? AOM_BITS_10 : AOM_BITS_8;
  config.rc_end_usage = AOM_Q;
  config.kf_max_dist = VideoKeyFrameDistance;
  if (auto started = check(encoder.codec,
                           aom_codec_enc_init(&encoder.codec, iface, &config,
                                              tenBit ? AOM_CODEC_USE_HIGHBITDEPTH : 0),
                           "start the encoder");
      !started) {
    return std::unexpected{started.error()};
  }
  encoder.started = true;
  // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg): libaom's control interface is variadic.
  for (const auto& [result, what] :
       {std::pair{aom_codec_control(&encoder.codec, AOME_SET_CPUUSED, VideoSpeed), "set the speed"},
        std::pair{aom_codec_control(&encoder.codec, AOME_SET_CQ_LEVEL, VideoQuality),
                  "set the quality"},
        std::pair{aom_codec_control(&encoder.codec, AV1E_SET_COLOR_RANGE, AOM_CR_FULL_RANGE),
                  "set the full range"},
        std::pair{
            aom_codec_control(&encoder.codec, AV1E_SET_MATRIX_COEFFICIENTS, AOM_CICP_MC_BT_601),
            "set the colour matrix"}}) {
    // NOLINTEND(cppcoreguidelines-pro-type-vararg)
    if (auto set = check(encoder.codec, result, what); !set) {
      return std::unexpected{set.error()};
    }
  }

  std::vector<std::byte> ivf;
  ivf.reserve(source.size() / 16);
  // The header: the frame count is filled in at the end.
  for (const char c : std::string_view{"DKIF"}) {
    ivf.push_back(static_cast<std::byte>(c));
  }
  put16(ivf, 0);
  put16(ivf, assets::IvfHeaderSize);
  for (const char c : std::string_view{"AV01"}) {
    ivf.push_back(static_cast<std::byte>(c));
  }
  put16(ivf, header->width);
  put16(ivf, header->height);
  put32(ivf, header->fpsNumerator);
  put32(ivf, header->fpsDenominator);
  put32(ivf, 0);
  put32(ivf, 0);

  aom_image_t image{};
  // A 10-bit picture is copied out of the source, whose samples need not be aligned.
  std::vector<std::uint16_t> wide(tenBit ? lumaSize + (2 * chromaSize) : 0);
  std::uint32_t written = 0;
  std::int64_t pts = 0;
  for (std::size_t offset = first->second; offset < source.size();) {
    const auto frame = lineAt(source, offset);
    if (!frame) {
      return std::unexpected{frame.error()};
    }
    if (!frame->first.starts_with(FrameSignature)) {
      return core::fail(std::format("the Y4M has no FRAME at offset {}", offset));
    }
    if (source.size() - frame->second < pictureSize) {
      return core::fail(std::format("the Y4M ends inside frame {}", pts));
    }
    // The picture's planes: in place in the source, or copied when 10-bit.
    auto* planes =
        reinterpret_cast<unsigned char*>(const_cast<std::byte*>(source.data() + frame->second));
    if (tenBit) {
      const std::byte* sample = source.data() + frame->second;
      for (std::uint16_t& value : wide) {
        value = static_cast<std::uint16_t>(std::to_integer<unsigned>(sample[0]) |
                                           (std::to_integer<unsigned>(sample[1]) << 8U));
        sample += 2;
      }
      planes = reinterpret_cast<unsigned char*>(wide.data());
    }
    if (aom_img_wrap(&image, tenBit ? AOM_IMG_FMT_I42016 : AOM_IMG_FMT_I420, header->width,
                     header->height, 1, planes) == nullptr) {
      return core::fail("libaom cannot wrap a picture");
    }
    image.range = AOM_CR_FULL_RANGE;
    image.bit_depth = header->bitDepth;
    if (auto encoded = check(encoder.codec, aom_codec_encode(&encoder.codec, &image, pts, 1, 0),
                             "encode a frame");
        !encoded) {
      return std::unexpected{encoded.error()};
    }
    collect(encoder.codec, ivf, written);
    ++pts;
    offset = frame->second + pictureSize;
  }
  if (pts == 0) {
    return core::fail("the Y4M has no frames");
  }
  // Drain the frames the encoder still holds.
  for (;;) {
    const std::uint32_t before = written;
    if (auto flushed = check(encoder.codec, aom_codec_encode(&encoder.codec, nullptr, 0, 0, 0),
                             "finish the stream");
        !flushed) {
      return std::unexpected{flushed.error()};
    }
    collect(encoder.codec, ivf, written);
    if (written == before) {
      break;
    }
  }
  if (written != static_cast<std::uint64_t>(pts)) {
    return core::fail(std::format("libaom gave {} frames for {}", written, pts));
  }
  constexpr std::size_t FrameCountOffset = 24;
  for (unsigned i = 0; i < 4; ++i) {
    ivf[FrameCountOffset + i] = static_cast<std::byte>((written >> (8U * i)) & 0xFFU);
  }
  return ivf;
}

} // namespace dwe::dwx
