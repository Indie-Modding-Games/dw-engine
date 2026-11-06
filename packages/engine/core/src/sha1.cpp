#include <dwe/core/sha1.hpp>

#include <algorithm>
#include <bit>
#include <string_view>

namespace dwe::core {

namespace {

constexpr std::size_t RoundCount = 80;
constexpr std::size_t LengthOffset = 56;
constexpr std::array<std::uint32_t, 4> RoundConstants = {0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC,
                                                         0xCA62C1D6};

std::byte byteOf(std::uint64_t value, unsigned shift) {
  return static_cast<std::byte>(static_cast<std::uint8_t>(value >> shift));
}

} // namespace

void Sha1::update(std::span<const std::byte> bytes) {
  messageSize_ += bytes.size();
  while (!bytes.empty()) {
    const std::size_t count = std::min(bytes.size(), BlockSize - bufferSize_);
    std::ranges::copy(bytes.first(count), std::span{buffer_}.subspan(bufferSize_).begin());
    bufferSize_ += count;
    bytes = bytes.subspan(count);
    if (bufferSize_ == BlockSize) {
      processBlock(buffer_);
      bufferSize_ = 0;
    }
  }
}

Sha1Digest Sha1::finish() {
  const std::uint64_t messageBits = messageSize_ * 8;

  // The message is followed by a single 1 bit, zeros up to 8 bytes before the end of a block, and
  // its length in bits as a big-endian 64-bit number.
  const std::array<std::byte, 1> marker = {std::byte{0x80}};
  update(marker);
  const std::array<std::byte, BlockSize> zeros{};
  const std::size_t zeroCount = bufferSize_ <= LengthOffset
                                    ? LengthOffset - bufferSize_
                                    : BlockSize + LengthOffset - bufferSize_;
  update(std::span{zeros}.first(zeroCount));
  std::array<std::byte, sizeof(std::uint64_t)> length{};
  for (std::size_t i = 0; i < length.size(); ++i) {
    length[i] = byteOf(messageBits, static_cast<unsigned>(8 * (length.size() - 1 - i)));
  }
  update(length);

  Sha1Digest digest{};
  for (std::size_t i = 0; i < digest.size(); ++i) {
    digest[i] = byteOf(state_[i / 4], static_cast<unsigned>(8 * (3 - (i % 4))));
  }
  *this = Sha1{};
  return digest;
}

void Sha1::processBlock(std::span<const std::byte, BlockSize> block) {
  std::array<std::uint32_t, RoundCount> words{};
  for (std::size_t i = 0; i < 16; ++i) {
    std::uint32_t word = 0;
    for (const std::byte byte : block.subspan(i * 4, 4)) {
      word = (word << 8U) | std::to_integer<std::uint32_t>(byte);
    }
    words[i] = word;
  }
  for (std::size_t i = 16; i < RoundCount; ++i) {
    words[i] = std::rotl(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
  }

  auto [a, b, c, d, e] = state_;
  for (std::size_t i = 0; i < RoundCount; ++i) {
    std::uint32_t mix = 0;
    if (i < 20) {
      mix = (b & c) | (~b & d);
    } else if (i < 40 || i >= 60) {
      mix = b ^ c ^ d;
    } else {
      mix = (b & c) | (b & d) | (c & d);
    }
    const std::uint32_t next = std::rotl(a, 5) + mix + e + RoundConstants[i / 20] + words[i];
    e = d;
    d = c;
    c = std::rotl(b, 30);
    b = a;
    a = next;
  }
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
}

Sha1Digest sha1(std::span<const std::byte> bytes) {
  Sha1 hasher;
  hasher.update(bytes);
  return hasher.finish();
}

std::string toHex(const Sha1Digest& digest) {
  constexpr std::string_view Digits = "0123456789abcdef";
  std::string hex;
  hex.reserve(digest.size() * 2);
  for (const std::byte byte : digest) {
    const auto value = std::to_integer<std::size_t>(byte);
    hex += Digits[value >> 4U];
    hex += Digits[value & 0xFU];
  }
  return hex;
}

} // namespace dwe::core
