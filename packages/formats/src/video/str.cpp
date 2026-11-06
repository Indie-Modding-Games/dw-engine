#include <dwe/formats/video/str.hpp>

#include <algorithm>
#include <format>

#include "../bytes/byte_cursor.hpp"

namespace dwe::formats::video {

namespace {

constexpr std::uint16_t StMarker = 0x0160;
constexpr std::uint16_t StVideoType = 0x8001;

bool isZero(std::span<const std::byte> bytes) {
  return std::ranges::all_of(bytes, [](std::byte value) { return value == std::byte{0}; });
}

core::Result<StrSector> readVideoSector(std::span<const std::byte> data) {
  bytes::ByteCursor cursor{data};
  const std::uint16_t marker = cursor.u16();
  const std::uint16_t type = cursor.u16();
  StrVideoSector sector{.sectorInFrame = cursor.u16(),
                        .sectorsInFrame = cursor.u16(),
                        .frameNumber = cursor.u32(),
                        .frameBytes = cursor.u32(),
                        .width = cursor.u16(),
                        .height = cursor.u16(),
                        .headm = cursor.u32(),
                        .headv = cursor.u32(),
                        .chunk = {}};
  const std::uint32_t reserved = cursor.u32();
  sector.chunk = cursor.take(StChunkSize);
  if (cursor.overrun()) {
    return core::fail("a video sector is too short for its header and chunk");
  }
  if (marker != StMarker || type != StVideoType) {
    return core::fail(
        std::format("a data sector has {:#06x} {:#06x} where the video header goes", marker, type));
  }
  if (reserved != 0) {
    return core::fail(std::format("a video sector's reserved field is {:#x}", reserved));
  }
  if (sector.sectorsInFrame == 0 || sector.sectorInFrame >= sector.sectorsInFrame) {
    return core::fail(std::format("a video sector says it is sector {} of {}", sector.sectorInFrame,
                                  sector.sectorsInFrame));
  }
  return sector;
}

} // namespace

core::Result<StrSector> readStrSector(const disc::SectorData& sector) {
  if (!sector.subheader) {
    return core::fail("the sector has no subheader; stream files need a raw (2352-byte) image");
  }
  const std::uint8_t mode = sector.subheader->submode;
  if ((mode & submode::Audio) != 0) {
    if (sector.form != disc::SectorForm::Form2) {
      return core::fail("an audio sector is not form 2");
    }
    const auto coding = readXaCoding(sector.subheader->coding);
    if (!coding) {
      return std::unexpected{coding.error()};
    }
    return StrAudioSector{.file = sector.subheader->file,
                          .channel = sector.subheader->channel,
                          .coding = *coding,
                          .endOfFile = (mode & submode::EndOfFile) != 0,
                          .data = sector.userData};
  }
  if ((mode & (submode::Video | submode::Data)) != 0) {
    if (sector.form != disc::SectorForm::Form1) {
      return core::fail("a video sector is not form 1");
    }
    return readVideoSector(sector.userData);
  }
  if ((mode & ~(submode::EndOfRecord | submode::EndOfFile)) == 0 && isZero(sector.userData)) {
    return StrPaddingSector{};
  }
  return core::fail(
      std::format("a sector with submode {:#04x} is neither video, audio nor empty padding", mode));
}

core::Result<std::optional<StrFrame>> StrFrameAssembler::add(const StrVideoSector& sector) {
  if (sector.sectorInFrame == nextSector_) {
    return take(sector);
  }
  const auto pending = pendingFrame();
  const std::uint16_t received = nextSector_;
  reset();
  if (sector.sectorInFrame != 0) {
    return core::fail(std::format("sector {} of frame {} comes without the sectors before it",
                                  sector.sectorInFrame, sector.frameNumber));
  }
  // A frame starts while the previous one is missing sectors: the new one goes on, the old one is
  // reported.
  if (auto taken = take(sector); !taken) {
    return taken;
  }
  return core::fail(
      std::format("frame {} stops after {} of its sectors", pending.value_or(0), received));
}

core::Result<std::optional<StrFrame>> StrFrameAssembler::take(const StrVideoSector& sector) {
  if (sector.sectorInFrame == 0) {
    if (std::size_t{sector.frameBytes} > std::size_t{sector.sectorsInFrame} * StChunkSize) {
      return core::fail(std::format("frame {} has {} bytes, more than its {} sectors hold",
                                    sector.frameNumber, sector.frameBytes, sector.sectorsInFrame));
    }
    first_ = sector;
    data_.clear();
  } else {
    if (!first_) {
      return core::fail(std::format("sector {} of frame {} comes without the sectors before it",
                                    sector.sectorInFrame, sector.frameNumber));
    }
    const StrVideoSector& first = *first_;
    if (sector.frameNumber != first.frameNumber || sector.sectorsInFrame != first.sectorsInFrame ||
        sector.frameBytes != first.frameBytes || sector.width != first.width ||
        sector.height != first.height || sector.headm != first.headm ||
        sector.headv != first.headv) {
      const std::uint32_t number = first.frameNumber;
      reset();
      return core::fail(std::format("sector {} of frame {} has a different header",
                                    sector.sectorInFrame, number));
    }
  }
  data_.insert(data_.end(), sector.chunk.begin(), sector.chunk.end());
  nextSector_ = static_cast<std::uint16_t>(sector.sectorInFrame + 1);
  if (nextSector_ < sector.sectorsInFrame) {
    return std::nullopt;
  }
  const StrVideoSector first = first_.value_or(sector);
  reset();
  if (!isZero(std::span{data_}.subspan(first.frameBytes))) {
    return core::fail(std::format("frame {} has data in the padding after its {} bytes",
                                  first.frameNumber, first.frameBytes));
  }
  data_.resize(first.frameBytes);
  bytes::ByteCursor start{data_};
  const std::uint32_t headm = start.u32();
  const std::uint32_t headv = start.u32();
  if (start.overrun() || headm != first.headm || headv != first.headv) {
    return core::fail(
        std::format("frame {}: headm and headv do not match its bitstream", first.frameNumber));
  }
  StrFrame frame{
      .number = first.frameNumber, .width = first.width, .height = first.height, .bitstream = {}};
  frame.bitstream.swap(data_);
  return frame;
}

void StrFrameAssembler::reset() {
  first_.reset();
  nextSector_ = 0;
}

std::optional<std::uint32_t> StrFrameAssembler::pendingFrame() const {
  if (!first_) {
    return std::nullopt;
  }
  return first_->frameNumber;
}

} // namespace dwe::formats::video
