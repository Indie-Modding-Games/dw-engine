#include <dwe/game/coop/profile_transfer.hpp>

#include <format>

namespace dwe::game::coop {

core::Result<void> ProfileAssembler::begin(std::uint32_t transferId, std::uint16_t chunkCount,
                                           ProfilePayloadKind kind) {
  if (transferId == 0) {
    return core::fail("cooperative profile transfer id is zero");
  }
  if (chunkCount == 0 || chunkCount > MaxProfileChunks) {
    return core::fail(std::format("cooperative profile transfer has {} chunks", chunkCount));
  }
  if (static_cast<std::uint8_t>(kind) > static_cast<std::uint8_t>(ProfilePayloadKind::HostSave)) {
    return core::fail("cooperative profile transfer has an unknown payload kind");
  }
  reset();
  transferId_ = transferId;
  kind_ = kind;
  chunks_.resize(chunkCount);
  return {};
}

core::Result<std::optional<net::Bytes>> ProfileAssembler::push(const ProfileChunk& chunk) {
  if (transferId_ == 0 || chunk.transferId != transferId_) {
    return core::fail("cooperative profile chunk belongs to another transfer");
  }
  if (chunk.kind != kind_ || chunk.count != chunks_.size() || chunk.index >= chunks_.size() ||
      chunk.bytes.empty() || chunk.bytes.size() > MaxProfileChunkBytes) {
    return core::fail("invalid cooperative profile chunk bounds");
  }
  if (complete_) {
    const auto& stored = chunks_[chunk.index];
    if (!stored || *stored != chunk.bytes) {
      return core::fail("conflicting cooperative profile retry after completion");
    }
    return assembled_;
  }
  std::optional<net::Bytes>& stored = chunks_[chunk.index];
  if (stored) {
    if (*stored != chunk.bytes) {
      return core::fail("conflicting duplicate cooperative profile chunk");
    }
    return std::optional<net::Bytes>{};
  }
  if (receivedBytes_ + chunk.bytes.size() > MaxProfileBytes) {
    return core::fail("cooperative profile exceeds its maximum size");
  }
  receivedBytes_ += chunk.bytes.size();
  ++receivedChunks_;
  stored = chunk.bytes;
  if (receivedChunks_ != chunks_.size()) {
    return std::optional<net::Bytes>{};
  }
  assembled_.reserve(receivedBytes_);
  for (const auto& part : chunks_) {
    if (!part) {
      return core::fail("incomplete cooperative profile assembly");
    }
    assembled_.insert(assembled_.end(), part->begin(), part->end());
  }
  complete_ = true;
  return assembled_;
}

void ProfileAssembler::reset() {
  transferId_ = 0;
  kind_ = ProfilePayloadKind::GuestSave;
  chunks_.clear();
  receivedBytes_ = 0;
  receivedChunks_ = 0;
  complete_ = false;
  assembled_.clear();
}

} // namespace dwe::game::coop
