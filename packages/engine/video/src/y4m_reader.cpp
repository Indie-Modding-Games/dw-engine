#include <dwe/video/y4m_reader.hpp>

#include <algorithm>
#include <charconv>
#include <format>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace dwe::video {

namespace {

constexpr std::string_view StreamSignature = "YUV4MPEG2 ";
constexpr std::string_view FrameSignature = "FRAME";
// A header line longer than this is not a stream dwx wrote (its lines are under 100 bytes).
constexpr std::size_t MaxHeaderLine = 1024;
// The picture sizes the reader accepts: generous, and a forged size cannot ask for gigabytes.
constexpr std::uint32_t MaxDimension = 4096;

// Reads one line, without its line end; nothing when the file ends before any byte of it.
core::Result<std::optional<std::string>> readLine(assets::FileReader& file, std::string_view name) {
  std::string line;
  std::byte next{};
  for (;;) {
    auto count = file.read(std::span{&next, 1});
    if (!count) {
      return std::unexpected{count.error()};
    }
    if (*count == 0) {
      if (line.empty()) {
        return std::optional<std::string>{};
      }
      return core::fail(std::format("the video '{}' ends inside a header line", name));
    }
    if (next == std::byte{'\n'}) {
      return std::optional<std::string>{std::move(line)};
    }
    if (line.size() == MaxHeaderLine) {
      return core::fail(
          std::format("the video '{}' has a header line longer than {}", name, MaxHeaderLine));
    }
    line.push_back(static_cast<char>(next));
  }
}

std::optional<std::uint32_t> numberOf(std::string_view text) {
  std::uint32_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

// Reads one parameter of the stream header into stream; false when it is not acceptable.
bool readParameter(std::string_view token, Y4mStream& stream) {
  const std::string_view value = token.substr(1);
  switch (token.front()) {
  case 'W':
  case 'H': {
    const auto size = numberOf(value);
    if (!size || *size == 0 || *size > MaxDimension) {
      return false;
    }
    (token.front() == 'W' ? stream.width : stream.height) = *size;
    return true;
  }
  case 'F': {
    const std::size_t colon = value.find(':');
    const auto numerator = numberOf(value.substr(0, colon));
    const auto denominator =
        colon == std::string_view::npos ? std::nullopt : numberOf(value.substr(colon + 1));
    if (!numerator || !denominator || *numerator == 0 || *denominator == 0) {
      return false;
    }
    stream.fpsNumerator = *numerator;
    stream.fpsDenominator = *denominator;
    return true;
  }
  case 'C':
    if (value == "420p10") {
      stream.bitDepth = 10;
      return true;
    }
    return value.starts_with("420") && !value.starts_with("420p");
  case 'I':
    return value == "p" || value == "?";
  default:
    // Aspect, comments and extensions (XCOLORRANGE) do not change how frames are read.
    return true;
  }
}

} // namespace

core::Result<Y4mReader> Y4mReader::open(std::unique_ptr<assets::FileReader> file,
                                        std::string name) {
  auto line = readLine(*file, name);
  if (!line) {
    return std::unexpected{line.error()};
  }
  if (!*line || !(*line)->starts_with(StreamSignature)) {
    return core::fail(std::format("'{}' is not a YUV4MPEG2 video", name));
  }
  Y4mStream stream;
  const std::string_view header = std::string_view{**line}.substr(StreamSignature.size());
  for (std::size_t start = 0; start < header.size();) {
    const std::size_t end = std::min(header.find(' ', start), header.size());
    const std::string_view token = header.substr(start, end - start);
    if (!token.empty() && !readParameter(token, stream)) {
      return core::fail(
          std::format("the video '{}' has a header parameter it cannot play: '{}'", name, token));
    }
    start = end + 1;
  }
  if (stream.width == 0 || stream.height == 0 || stream.fpsNumerator == 0) {
    return core::fail(std::format("the video '{}' has no size or frame rate", name));
  }
  return Y4mReader{std::move(file), std::move(name), stream};
}

core::Result<bool> Y4mReader::readFrameHeader() {
  auto line = readLine(*file_, name_);
  if (!line) {
    return std::unexpected{line.error()};
  }
  if (!*line) {
    return false;
  }
  if (!(*line)->starts_with(FrameSignature)) {
    return core::fail(std::format("the video '{}' has a frame without its FRAME line", name_));
  }
  return true;
}

core::Result<void> Y4mReader::readExactly(std::span<std::byte> into) {
  auto count = file_->read(into);
  if (!count) {
    return std::unexpected{count.error()};
  }
  if (*count != into.size()) {
    return core::fail(std::format("the video '{}' ends inside a frame", name_));
  }
  return {};
}

core::Result<bool> Y4mReader::read(YuvPicture& picture) {
  auto started = readFrameHeader();
  if (!started || !*started) {
    return started;
  }
  const std::size_t sampleBytes = stream_.bitDepth > 8 ? 2 : 1;
  picture.width = stream_.width;
  picture.height = stream_.height;
  picture.bitDepth = stream_.bitDepth;
  picture.luma.resize(std::size_t{stream_.width} * stream_.height);
  picture.blue.resize(YuvPicture::chromaSize(stream_.width, stream_.height));
  picture.red.resize(picture.blue.size());
  for (std::vector<std::uint16_t>* plane : {&picture.luma, &picture.blue, &picture.red}) {
    buffer_.resize(plane->size() * sampleBytes);
    if (auto done = readExactly(buffer_); !done) {
      return std::unexpected{done.error()};
    }
    for (std::size_t i = 0; i < plane->size(); ++i) {
      const auto low = std::to_integer<std::uint16_t>(buffer_[i * sampleBytes]);
      (*plane)[i] = sampleBytes == 1
                        ? low
                        : static_cast<std::uint16_t>(
                              low | (std::to_integer<std::uint16_t>(buffer_[(i * 2) + 1]) << 8U));
    }
  }
  return true;
}

core::Result<bool> Y4mReader::skip() {
  auto started = readFrameHeader();
  if (!started || !*started) {
    return started;
  }
  const std::size_t sampleBytes = stream_.bitDepth > 8 ? 2 : 1;
  buffer_.resize(((std::size_t{stream_.width} * stream_.height) +
                  (2 * YuvPicture::chromaSize(stream_.width, stream_.height))) *
                 sampleBytes);
  if (auto done = readExactly(buffer_); !done) {
    return std::unexpected{done.error()};
  }
  return true;
}

} // namespace dwe::video
