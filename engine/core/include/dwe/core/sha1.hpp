#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace dwe::core {

// A SHA-1 digest (FIPS 180-4). It identifies files; it is not used for security.
using Sha1Digest = std::array<std::byte, 20>;

// Computes a SHA-1 digest incrementally, so a large file can be hashed in chunks.
class Sha1 {
public:
    // Adds bytes to the message.
    void update(std::span<const std::byte> bytes);

    // Pads the message and returns its digest. The hasher then starts a new, empty message.
    Sha1Digest finish();

private:
    static constexpr std::size_t BlockSize = 64;

    void processBlock(std::span<const std::byte, BlockSize> block);

    std::array<std::uint32_t, 5> state_ = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::array<std::byte, BlockSize> buffer_{};
    std::size_t bufferSize_ = 0;
    std::uint64_t messageSize_ = 0;
};

// Returns the digest of a whole buffer.
Sha1Digest sha1(std::span<const std::byte> bytes);

// Returns the digest as 40 lowercase hex digits, the form sha1sum prints.
std::string toHex(const Sha1Digest& digest);

} // namespace dwe::core
