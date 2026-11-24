#pragma once

#include <dwe/game/coop/protocol.hpp>

#include <optional>
#include <vector>

namespace dwe::game::coop {

// How many chunks of MaxProfileChunkBytes a profile of `bytes` takes.
[[nodiscard]] constexpr std::size_t profileChunkCount(std::size_t bytes) {
  return (bytes + MaxProfileChunkBytes - 1) / MaxProfileChunkBytes;
}

// Reassembles one reliable save-profile transfer. It accepts chunks in any order, tolerates an
// identical retry, and refuses conflicting duplicates or allocations beyond MaxProfileBytes.
class ProfileAssembler {
public:
  [[nodiscard]] core::Result<void> begin(std::uint32_t transferId, std::uint16_t chunkCount,
                                         ProfilePayloadKind kind);
  [[nodiscard]] core::Result<std::optional<net::Bytes>> push(const ProfileChunk& chunk);
  void reset();

  [[nodiscard]] std::size_t receivedBytes() const { return receivedBytes_; }
  [[nodiscard]] bool complete() const { return complete_; }

private:
  std::uint32_t transferId_ = 0;
  ProfilePayloadKind kind_ = ProfilePayloadKind::GuestSave;
  std::vector<std::optional<net::Bytes>> chunks_;
  std::size_t receivedBytes_ = 0;
  std::size_t receivedChunks_ = 0;
  bool complete_ = false;
  net::Bytes assembled_;
};

} // namespace dwe::game::coop
