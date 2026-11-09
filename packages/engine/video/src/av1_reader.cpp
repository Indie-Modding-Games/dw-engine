#include <dwe/assets/media_headers.hpp>
#include <dwe/video/av1_reader.hpp>

#include <aom/aom_decoder.h>
#include <aom/aomdx.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <span>
#include <utility>

namespace dwe::video {

namespace {

// The picture sizes the reader accepts, as the Y4M reader's: a forged size cannot ask for
// gigabytes.
constexpr std::uint32_t MaxDimension = 4096;
// A frame larger than this is not one dwx wrote: an uncompressed 4096 x 4096 picture is 24 MB.
constexpr std::uint32_t MaxFrameBytes = 32U * 1024U * 1024U;

std::uint32_t u32At(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8U * i);
  }
  return value;
}

// Fills into from the file; the count read, short only at the end.
core::Result<std::size_t> readFully(assets::FileReader& file, std::span<std::byte> into) {
  std::size_t done = 0;
  while (done < into.size()) {
    auto count = file.read(into.subspan(done));
    if (!count) {
      return std::unexpected{count.error()};
    }
    if (*count == 0) {
      break;
    }
    done += *count;
  }
  return done;
}

// Copies one plane of the decoder's picture, whatever its row stride and sample size.
void copyPlane(const aom_image_t& image, int plane, std::uint32_t width, std::uint32_t height,
               std::vector<std::uint16_t>& into) {
  into.resize(static_cast<std::size_t>(width) * height);
  const bool wide = (image.fmt & AOM_IMG_FMT_HIGHBITDEPTH) != 0;
  const auto stride = static_cast<std::size_t>(image.stride[plane]);
  for (std::uint32_t y = 0; y < height; ++y) {
    const unsigned char* row = image.planes[plane] + (y * stride);
    std::uint16_t* out = into.data() + (static_cast<std::size_t>(y) * width);
    if (wide) {
      std::copy_n(reinterpret_cast<const std::uint16_t*>(row), width, out);
    } else {
      std::copy_n(row, width, out);
    }
  }
}

} // namespace

struct Av1Reader::Decoder {
  aom_codec_ctx_t codec{};

  Decoder() = default;
  Decoder(const Decoder&) = delete;
  Decoder& operator=(const Decoder&) = delete;
  Decoder(Decoder&&) = delete;
  Decoder& operator=(Decoder&&) = delete;
  ~Decoder() { aom_codec_destroy(&codec); }
};

Av1Reader::Av1Reader(std::unique_ptr<assets::FileReader> file, std::string name, Y4mStream stream,
                     std::unique_ptr<Decoder> decoder)
    : file_(std::move(file)), name_(std::move(name)), stream_(stream),
      decoder_(std::move(decoder)) {}

Av1Reader::Av1Reader(Av1Reader&&) noexcept = default;
Av1Reader& Av1Reader::operator=(Av1Reader&&) noexcept = default;
Av1Reader::~Av1Reader() = default;

core::Result<Av1Reader> Av1Reader::open(std::unique_ptr<assets::FileReader> file,
                                        std::string name) {
  std::array<std::byte, assets::IvfHeaderSize> header{};
  auto count = readFully(*file, header);
  if (!count) {
    return std::unexpected{count.error()};
  }
  auto info = assets::readIvfHeader(std::span{header}.first(*count));
  if (!info) {
    return core::fail(std::format("the video '{}': {}", name, info.error().message));
  }
  if (info->picture.width > MaxDimension || info->picture.height > MaxDimension) {
    return core::fail(std::format("the video '{}' is {}x{}, larger than the reader takes", name,
                                  info->picture.width, info->picture.height));
  }
  auto decoder = std::make_unique<Decoder>();
  const aom_codec_dec_cfg_t config{.threads = 1, .w = 0, .h = 0, .allow_lowbitdepth = 1};
  if (aom_codec_dec_init(&decoder->codec, aom_codec_av1_dx(), &config, 0) != AOM_CODEC_OK) {
    return core::fail(std::format("the video '{}': the AV1 decoder does not start: {}", name,
                                  aom_codec_error(&decoder->codec)));
  }
  const Y4mStream stream{.width = info->picture.width,
                         .height = info->picture.height,
                         .fpsNumerator = info->rateNumerator,
                         .fpsDenominator = info->rateDenominator};
  return Av1Reader{std::move(file), std::move(name), stream, std::move(decoder)};
}

core::Result<bool> Av1Reader::read(YuvPicture& picture) {
  return decodeNext(&picture);
}

core::Result<bool> Av1Reader::skip() {
  return decodeNext(nullptr);
}

core::Result<bool> Av1Reader::decodeNext(YuvPicture* picture) {
  for (;;) {
    std::array<std::byte, assets::IvfFrameHeaderSize> header{};
    auto count = readFully(*file_, header);
    if (!count) {
      return std::unexpected{count.error()};
    }
    if (*count == 0) {
      return false;
    }
    if (*count != header.size()) {
      return core::fail(std::format("the video '{}' ends inside a frame header", name_));
    }
    const std::uint32_t size = u32At(header, 0);
    if (size == 0 || size > MaxFrameBytes) {
      return core::fail(std::format("the video '{}' has a frame of {} bytes", name_, size));
    }
    frame_.resize(size);
    auto body = readFully(*file_, frame_);
    if (!body) {
      return std::unexpected{body.error()};
    }
    if (*body != size) {
      return core::fail(std::format("the video '{}' ends inside a frame", name_));
    }
    aom_codec_ctx_t& codec = decoder_->codec;
    if (aom_codec_decode(&codec, reinterpret_cast<const std::uint8_t*>(frame_.data()), size,
                         nullptr) != AOM_CODEC_OK) {
      return core::fail(std::format("the video '{}' does not decode: {}", name_,
                                    aom_codec_error_detail(&codec) != nullptr
                                        ? aom_codec_error_detail(&codec)
                                        : aom_codec_error(&codec)));
    }
    aom_codec_iter_t iterator = nullptr;
    const aom_image_t* image = aom_codec_get_frame(&codec, &iterator);
    if (image == nullptr) {
      // A frame that only feeds later ones: read on.
      continue;
    }
    // Drain the rest, so the next decode starts clean.
    while (aom_codec_get_frame(&codec, &iterator) != nullptr) {
    }
    if (image->d_w != stream_.width || image->d_h != stream_.height ||
        (image->fmt & ~AOM_IMG_FMT_HIGHBITDEPTH) != AOM_IMG_FMT_I420) {
      return core::fail(std::format("the video '{}' has a {}x{} picture of another layout than "
                                    "its {}x{} 4:2:0 header",
                                    name_, image->d_w, image->d_h, stream_.width, stream_.height));
    }
    if (picture != nullptr) {
      picture->width = stream_.width;
      picture->height = stream_.height;
      picture->bitDepth = image->bit_depth;
      const std::uint32_t chromaWidth = (stream_.width + 1) / 2;
      const std::uint32_t chromaHeight = (stream_.height + 1) / 2;
      copyPlane(*image, AOM_PLANE_Y, stream_.width, stream_.height, picture->luma);
      copyPlane(*image, AOM_PLANE_U, chromaWidth, chromaHeight, picture->blue);
      copyPlane(*image, AOM_PLANE_V, chromaWidth, chromaHeight, picture->red);
    }
    return true;
  }
}

} // namespace dwe::video
