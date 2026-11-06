#include <dwe/audio/output.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace dwe::audio {

namespace {

// Seconds that are a whole number of frames in decimal are not always so in binary: a frame short
// by this much still counts as whole.
constexpr double FrameTolerance = 1e-6;

} // namespace

void NullOutput::start(MixSource& source) {
  source_ = &source;
}

// Keeps the fraction of a frame for the next call, so 60 calls of 1/60 s render exactly a second.
void NullOutput::advance(double seconds) {
  if (source_ == nullptr || !(seconds > 0.0)) {
    return;
  }
  pendingFrames_ += seconds * static_cast<double>(MixSampleRate);
  const double whole = std::floor(pendingFrames_ + FrameTolerance);
  pendingFrames_ -= whole;
  auto frames = static_cast<std::uint64_t>(whole);
  renderedFrames_ += frames;
  while (frames > 0) {
    const auto pass = static_cast<std::size_t>(std::min<std::uint64_t>(frames, MaxRenderFrames));
    source_->render(std::span{discarded_}.first(pass * OutputChannels));
    frames -= pass;
  }
}

} // namespace dwe::audio
