#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/rhi/upload_ring.hpp>

#include <utility>

namespace dwe::rhi {

namespace {

// Each frame's part starts at a multiple of this, so offsets aligned within a part are aligned in
// the buffer too.
constexpr std::size_t PartAlignment = 256;

} // namespace

core::Result<UploadRing> UploadRing::create(Device& device, std::size_t bytesPerFrame) {
  bytesPerFrame = (bytesPerFrame + PartAlignment - 1) / PartAlignment * PartAlignment;
  auto buffer = Buffer::create(device, {.size = bytesPerFrame * FramesInFlight,
                                        .usage = BufferUsage::Vertex | BufferUsage::Index |
                                                 BufferUsage::Uniform | BufferUsage::Storage,
                                        .location = MemoryLocation::Host});
  if (!buffer) {
    return std::unexpected{buffer.error()};
  }
  return UploadRing{std::move(*buffer), bytesPerFrame};
}

UploadRing::UploadRing(Buffer buffer, std::size_t bytesPerFrame)
    : buffer_(std::move(buffer)), bytesPerFrame_(bytesPerFrame),
      frames_(FramesInFlight, LinearAllocator{bytesPerFrame}) {}

void UploadRing::beginFrame(std::uint32_t slot) {
  DWE_ASSERT(slot < frames_.size());
  slot_ = slot;
  frames_[slot_].reset();
  inFrame_ = true;
}

std::optional<UploadAllocation> UploadRing::allocate(std::size_t size, std::size_t alignment) {
  DWE_ASSERT(alignment <= PartAlignment);
  // Outside a frame the GPU may still be reading the slot.
  DWE_ASSERT(inFrame_);
  const auto offset = frames_[slot_].allocate(size, alignment);
  if (!offset) {
    core::logError(
        "the upload ring is out of room: {} of {} bytes used this frame, {} more asked for",
        frames_[slot_].used(), bytesPerFrame_, size);
    return std::nullopt;
  }
  const std::size_t start = (std::size_t{slot_} * bytesPerFrame_) + *offset;
  return UploadAllocation{.data = buffer_.mapped().subspan(start, size),
                          .buffer = &buffer_,
                          .offset = start,
                          .address = buffer_.address() + start};
}

} // namespace dwe::rhi
