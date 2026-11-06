#include <dwe/formats/byte_reader.hpp>
#include <dwe/formats/model/animation.hpp>

#include <cmath>
#include <cstdlib>
#include <format>
#include <limits>
#include <numbers>
#include <optional>
#include <string>

namespace dwe::formats::model {

namespace {

constexpr std::uint16_t HasScaleBit = 0x8000;
constexpr std::uint16_t FrameMask = 0x0FFF;
constexpr std::uint16_t OpcodeShift = 12;
constexpr std::uint16_t LoopStartBit = 0x1000;
constexpr std::uint16_t KeyframeEntryBit = 0x8000;
constexpr std::uint16_t EntryBoneMask = 0x3F;
constexpr std::uint16_t FirstChannelBit = 0x4000;
constexpr std::uint16_t LoopCountMask = 0xFF;
constexpr std::int32_t Fixed12 = 4096;
constexpr std::size_t ChannelCount = 9; // scale x/y/z, rotation x/y/z, location x/y/z
constexpr std::size_t FirstRotationChannel = 3;
constexpr std::size_t FirstLocationChannel = 6;
// Root motion is ignored on X for these two animation ids (anim.c: applyRootMomentum).
constexpr std::uint8_t NoRootXAnimationA = 0x23;
constexpr std::uint8_t NoRootXAnimationB = 0x24;
// Guards one tick against a stream that keeps jumping back without advancing time.
constexpr std::uint32_t MaxInstructionsPerTick = 0x10000;

enum class Opcode : std::uint8_t {
  Keyframe = 0,
  LoopStart = 1,
  LoopEnd = 2,
  TextureMove = 3,
  Sound = 4
};

// One channel of `MomentumData`: a velocity per tick plus a remainder spread over `reload` ticks by
// a counter.
struct Channel {
  std::int16_t reload = 0;    // scale1
  std::int16_t remainder = 0; // subDelta, |value % divisor|
  std::int16_t delta = 0;     // value / divisor
  std::int16_t counter = 0;   // subScale
  std::int8_t sign = 0;       // subValue

  [[nodiscard]] bool isMoving() const { return delta != 0 || remainder != 0; }

  // animApplyMomentum: the step for this tick, with the remainder's extra unit when the counter
  // runs out.
  std::int32_t step() {
    std::int32_t value = delta;
    if (remainder != 0) {
      counter = static_cast<std::int16_t>(counter - remainder);
      if (counter <= 0) {
        counter = static_cast<std::int16_t>(counter + reload);
        value += sign;
      }
    }
    return value;
  }

  // animReadMomentumInstruction. The C division and remainder truncate towards zero, like C++'s.
  void set(std::int16_t value, std::int16_t divisor) {
    delta = static_cast<std::int16_t>(value / divisor);
    remainder = static_cast<std::int16_t>(value % divisor);
    if (remainder != 0) {
      sign = remainder > 0 ? std::int8_t{1} : std::int8_t{-1};
      remainder = static_cast<std::int16_t>(std::abs(remainder));
      counter = divisor;
      reload = divisor;
    }
  }

  // resetMomentumData clears the velocities only; the counters keep their values.
  void reset() {
    delta = 0;
    remainder = 0;
  }
};

using Momentum = std::array<Channel, ChannelCount>;

class Simulator {
public:
  Simulator(std::span<const std::byte> stream, std::uint8_t id, std::size_t boneCount)
      : stream_(stream), momentum_(boneCount), current_(boneCount) {
    animation_.id = id;
    animation_.boneCount = boneCount;
  }

  core::Result<Animation> run() {
    if (auto started = start(); !started) {
      return std::unexpected(started.error());
    }
    while (true) {
      if (tick_ >= MaxAnimationTicks) {
        return core::fail(std::format("animation {} does not end within {} ticks", animation_.id,
                                      MaxAnimationTicks));
      }
      applyMomentum();
      animation_.poses.insert(animation_.poses.end(), current_.begin(), current_.end());
      animation_.rootMotion.push_back(rootMotion_);
      noteFrame();

      auto running = runInstructions();
      if (!running) {
        return std::unexpected(running.error());
      }
      if (!*running) {
        break;
      }
      if (frame_ == static_cast<std::int16_t>(animation_.frameCount)) {
        break;
      }
      frame_ = static_cast<std::int16_t>(frame_ + 1);
      ++tick_;
    }
    return std::move(animation_);
  }

private:
  [[nodiscard]] core::Result<std::uint16_t> word(std::size_t at) const {
    const auto value = stream_.u16(at);
    if (!value) {
      return core::fail(
          std::format("animation {} reads past the end of its stream at {:#x}", animation_.id, at));
    }
    return *value;
  }

  core::Result<std::uint16_t> next() {
    auto value = word(at_);
    at_ += 2;
    return value;
  }

  // startAnimation: the header, then the initial pose of every bone but the root.
  core::Result<void> start() {
    const auto header = next();
    if (!header) {
      return std::unexpected(header.error());
    }
    animation_.hasScale = (*header & HasScaleBit) != 0;
    animation_.frameCount = *header & static_cast<std::uint16_t>(~HasScaleBit);
    current_[0] = {.scale = {Fixed12, Fixed12, Fixed12}, .rotation = {}, .location = {}};
    for (std::size_t bone = 1; bone < current_.size(); ++bone) {
      std::array<std::int16_t, ChannelCount> values{Fixed12, Fixed12, Fixed12};
      for (std::size_t channel = animation_.hasScale ? 0 : FirstRotationChannel;
           channel < ChannelCount; ++channel) {
        const auto value = next();
        if (!value) {
          return std::unexpected(value.error());
        }
        values[channel] = static_cast<std::int16_t>(*value);
      }
      current_[bone] = {.scale = {values[0], values[1], values[2]},
                        .rotation = {values[3], values[4], values[5]},
                        .location = {values[6], values[7], values[8]}};
    }
    return {};
  }

  // tickMomentum: every moving channel takes its step. The root's location channels are root motion
  // instead.
  void applyMomentum() {
    for (std::size_t bone = 0; bone < current_.size(); ++bone) {
      BonePose& pose = current_[bone];
      for (std::size_t channel = 0; channel < ChannelCount; ++channel) {
        Channel& state = momentum_[bone][channel];
        if (!state.isMoving()) {
          continue;
        }
        if (bone == 0 && channel >= FirstLocationChannel) {
          break;
        }
        const std::int32_t step = state.step();
        if (channel < FirstRotationChannel) {
          pose.scale[channel] += step;
        } else if (channel < FirstLocationChannel) {
          // The rotation is an int16 in the original and wraps.
          std::int16_t& angle = pose.rotation[channel - FirstRotationChannel];
          angle = static_cast<std::int16_t>(angle + step);
        } else {
          pose.location[channel - FirstLocationChannel] += step;
        }
      }
    }
    applyRootMotion();
  }

  // applyRootMomentum: the root's location steps, turned by the root's rotation and scale, move the
  // entity. The original does this in 4.12 fixed point on the GTE; here it is in doubles, so the
  // result can differ in the last fraction of a unit.
  void applyRootMotion() {
    std::array<double, 3> step{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      if (axis == 0 && (animation_.id == NoRootXAnimationA || animation_.id == NoRootXAnimationB)) {
        continue;
      }
      Channel& state = momentum_[0][FirstLocationChannel + axis];
      step[axis] = state.step();
    }
    const BonePose& root = current_[0];
    for (std::size_t axis = 0; axis < 3; ++axis) {
      step[axis] *= static_cast<double>(root.scale[axis]) / Fixed12;
    }
    const auto angle = [&](std::size_t axis) {
      return static_cast<double>(root.rotation[axis]) * 2.0 * std::numbers::pi / Fixed12;
    };
    // Rx·Ry·Rz applied to the vector: Z first, then Y, then X.
    const double cz = std::cos(angle(2));
    const double sz = std::sin(angle(2));
    const double cy = std::cos(angle(1));
    const double sy = std::sin(angle(1));
    const double cx = std::cos(angle(0));
    const double sx = std::sin(angle(0));
    const double x1 = (cz * step[0]) - (sz * step[1]);
    const double y1 = (sz * step[0]) + (cz * step[1]);
    const double z1 = step[2];
    const double x2 = (cy * x1) + (sy * z1);
    const double z2 = (-sy * x1) + (cy * z1);
    rootMotion_[0] += x2;
    rootMotion_[1] += (cx * y1) - (sx * z2);
    rootMotion_[2] += (sx * y1) + (cx * z2);
  }

  void noteFrame() {
    auto& first = firstTickOfFrame_[static_cast<std::uint16_t>(frame_)];
    if (!first) {
      first = tick_;
    }
  }

  // The body of tickAnimation after the momentum. Returns false when a loop repeats forever.
  core::Result<bool> runInstructions() {
    auto current = word(at_);
    if (!current) {
      return std::unexpected(current.error());
    }
    // The original tests a single bit here, so any opcode with bit 12 set would be taken as a loop
    // start. The streams of the original never have one at this point.
    if ((*current & LoopStartBit) != 0) {
      loopCount_ = static_cast<std::uint8_t>(*current & LoopCountMask);
      at_ += 2;
      loopStart_ = at_;
      loopStartCount_ = loopCount_;
      current = word(at_);
      if (!current) {
        return std::unexpected(current.error());
      }
      loopEndFrame_ = static_cast<std::int16_t>(*current & FrameMask);
    }

    std::uint32_t executed = 0;
    while (frame_ == loopEndFrame_) {
      if (++executed > MaxInstructionsPerTick) {
        return core::fail(std::format("animation {} runs more than {} instructions in one tick",
                                      animation_.id, MaxInstructionsPerTick));
      }
      const auto instruction = next();
      if (!instruction) {
        return std::unexpected(instruction.error());
      }
      const auto opcode = static_cast<std::uint8_t>(*instruction >> OpcodeShift);
      core::Result<void> done;
      switch (static_cast<Opcode>(opcode)) {
      case Opcode::Keyframe:
        done = readKeyframe();
        break;
      case Opcode::LoopStart:
        loopCount_ = static_cast<std::uint8_t>(*instruction & LoopCountMask);
        loopStart_ = at_;
        loopStartCount_ = loopCount_;
        break;
      case Opcode::LoopEnd: {
        auto jumped = endLoop();
        if (!jumped) {
          return std::unexpected(jumped.error());
        }
        if (!*jumped) {
          return false;
        }
        break;
      }
      case Opcode::TextureMove:
        done = readTextureMove();
        break;
      case Opcode::Sound:
        done = readSound();
        break;
      default:
        // The original's switch has no case for it and would execute the same word forever.
        return core::fail(std::format("animation {} has unknown opcode {} at {:#x}", animation_.id,
                                      opcode, at_ - 2));
      }
      if (!done) {
        return std::unexpected(done.error());
      }
      const auto following = word(at_);
      if (!following) {
        return std::unexpected(following.error());
      }
      loopEndFrame_ = static_cast<std::int16_t>(*following & FrameMask);
    }
    return true;
  }

  // animHandleKeyFrameInstruction: entries while the next word has bit 15, each a bone, a divisor
  // and one value per channel flagged in bits 14 (channel 0) to 6 (channel 8).
  core::Result<void> readKeyframe() {
    while (true) {
      const auto entry = word(at_);
      if (!entry) {
        return std::unexpected(entry.error());
      }
      if ((*entry & KeyframeEntryBit) == 0) {
        return {};
      }
      at_ += 2;
      const std::size_t bone = *entry & EntryBoneMask;
      if (bone >= momentum_.size()) {
        return core::fail(
            std::format("animation {} moves bone {} of {}", animation_.id, bone, momentum_.size()));
      }
      const auto divisor = next();
      if (!divisor) {
        return std::unexpected(divisor.error());
      }
      std::uint16_t flag = FirstChannelBit;
      for (std::size_t channel = 0; channel < ChannelCount; ++channel, flag >>= 1U) {
        if ((*entry & flag) == 0) {
          continue;
        }
        const auto value = next();
        if (!value) {
          return std::unexpected(value.error());
        }
        if (*divisor == 0) {
          return core::fail(
              std::format("animation {} divides by zero at {:#x}", animation_.id, at_));
        }
        momentum_[bone][channel].set(static_cast<std::int16_t>(*value),
                                     static_cast<std::int16_t>(*divisor));
      }
    }
  }

  // Loop end: count down, then fall through or jump back to the word after the loop start. Returns
  // false when the loop repeats forever.
  core::Result<bool> endLoop() {
    if (!loopStart_) {
      return core::fail(std::format("animation {} ends a loop at {:#x} that never started",
                                    animation_.id, at_ - 2));
    }
    const std::size_t loopStart = *loopStart_;
    if (loopCount_ != LoopForever) {
      --loopCount_;
    }
    if (loopCount_ == 0) {
      at_ += 2;
      return true;
    }
    const auto target = next();
    if (!target) {
      return std::unexpected(target.error());
    }
    noteLoop(static_cast<std::int16_t>(*target));
    if (loopCount_ == LoopForever) {
      // A count that started at 0 wraps to 0xFF here, and from then on the original loops forever
      // too.
      animation_.endsInLoop = true;
      return false;
    }
    frame_ = static_cast<std::int16_t>(*target);
    at_ = loopStart;
    return true;
  }

  void noteLoop(std::int16_t target) {
    const std::size_t endAt = at_ - 4;
    for (const std::size_t seen : loopEnds_) {
      if (seen == endAt) {
        return;
      }
    }
    loopEnds_.push_back(endAt);
    // The tick after the one that ran the target frame's instructions is the one the jump repeats.
    const auto& first = firstTickOfFrame_[static_cast<std::uint16_t>(target)];
    const std::uint32_t firstTick = first && *first < tick_ ? *first + 1 : tick_;
    animation_.loops.push_back(
        {.firstTick = firstTick, .lastTick = tick_, .count = loopStartCount_});
  }

  core::Result<void> readTextureMove() {
    std::array<std::uint16_t, 3> values{};
    for (auto& value : values) {
      const auto read = next();
      if (!read) {
        return std::unexpected(read.error());
      }
      value = *read;
    }
    const auto high = [](std::uint16_t value) { return static_cast<std::uint8_t>(value >> 8U); };
    const auto low = [](std::uint16_t value) { return static_cast<std::uint8_t>(value & 0xFFU); };
    animation_.textureMoves.push_back({.tick = tick_,
                                       .srcX = high(values[0]),
                                       .srcY = low(values[0]),
                                       .width = high(values[1]),
                                       .height = low(values[1]),
                                       .dstX = high(values[2]),
                                       .dstY = low(values[2])});
    return {};
  }

  core::Result<void> readSound() {
    const auto value = next();
    if (!value) {
      return std::unexpected(value.error());
    }
    animation_.sounds.push_back({.tick = tick_,
                                 .bank = static_cast<std::uint8_t>(*value >> 8U),
                                 .sound = static_cast<std::uint8_t>(*value & 0xFFU)});
    return {};
  }

  ByteReader stream_;
  std::vector<Momentum> momentum_;
  std::vector<BonePose> current_;
  std::array<double, 3> rootMotion_{};
  Animation animation_;
  std::size_t at_ = 0;
  std::uint32_t tick_ = 0;
  std::int16_t frame_ = 1;
  std::int16_t loopEndFrame_ = 1;
  std::uint8_t loopCount_ = 0;
  std::uint8_t loopStartCount_ = 0;
  std::optional<std::size_t> loopStart_;
  std::vector<std::size_t> loopEnds_;
  std::array<std::optional<std::uint32_t>, std::numeric_limits<std::uint16_t>::max() + 1>
      firstTickOfFrame_{};
};

} // namespace

core::Result<Animation> simulateAnimation(std::span<const std::byte> stream, std::uint8_t id,
                                          std::size_t boneCount) {
  if (boneCount == 0 || boneCount > MaxAnimationBones) {
    return core::fail(
        std::format("animation {}: a skeleton of {} bones is not supported", id, boneCount));
  }
  Simulator simulator(stream, id, boneCount);
  return simulator.run();
}

} // namespace dwe::formats::model
