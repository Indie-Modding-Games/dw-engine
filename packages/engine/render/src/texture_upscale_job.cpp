#include <dwe/render/scenery_upscale.hpp>
#include <dwe/render/texture_upscale_job.hpp>

#include <condition_variable>
#include <mutex>
#include <span>
#include <stop_token>
#include <utility>

namespace dwe::render {

namespace {

// The upscaler's version in the cache keys: change it when its output changes.
constexpr std::uint32_t TextureUpscaleVersion = 1;

} // namespace

std::vector<std::byte> modelTexturePixels(const assets::Image& image, std::uint32_t doublings,
                                          const UpscaleCache* cache) {
  const std::size_t bytes = image.pixels.size() << (2U * doublings);
  UpscaleKey key;
  key.add(TextureUpscaleVersion).add(doublings).add(image.width).add(image.height);
  key.add(std::span<const std::byte>{image.pixels});
  if (cache != nullptr && doublings > 0) {
    if (auto kept = cache->load(key.value(), bytes)) {
      return std::move(*kept);
    }
  }
  std::vector<std::byte> pixels = upscaleRgba(image.width, image.height, image.pixels, doublings);
  if (cache != nullptr && doublings > 0 && pixels.size() == bytes) {
    cache->store(key.value(), pixels);
  }
  return pixels;
}

struct TextureUpscaleJob::State {
  mutable std::mutex mutex;
  mutable std::condition_variable done;
  bool finished = false;
  bool cancelled = false;
  std::optional<std::vector<std::vector<std::byte>>> pixels;
  double milliseconds = 0.0;
};

TextureUpscaleJob::TextureUpscaleJob(std::vector<assets::Image> images, Make make)
    : state_(std::make_shared<State>()) {
  thread_ = std::jthread{[state = state_, images = std::move(images),
                          make = std::move(make)](const std::stop_token& stop) {
    const auto start = std::chrono::steady_clock::now();
    std::vector<std::vector<std::byte>> made;
    made.reserve(images.size());
    for (const assets::Image& image : images) {
      if (stop.stop_requested()) {
        break;
      }
      made.push_back(make(image));
    }
    const std::scoped_lock lock{state->mutex};
    if (!stop.stop_requested() && !state->cancelled) {
      state->pixels = std::move(made);
    }
    state->milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    state->finished = true;
    state->done.notify_all();
  }};
}

TextureUpscaleJob::~TextureUpscaleJob() {
  cancel();
}

bool TextureUpscaleJob::finished() const {
  const std::scoped_lock lock{state_->mutex};
  return state_->finished;
}

bool TextureUpscaleJob::wait(std::chrono::milliseconds timeout) const {
  std::unique_lock lock{state_->mutex};
  return state_->done.wait_for(lock, timeout, [this] { return state_->finished; });
}

std::optional<std::vector<std::vector<std::byte>>> TextureUpscaleJob::take() {
  const std::scoped_lock lock{state_->mutex};
  if (!state_->finished) {
    return std::nullopt;
  }
  return std::exchange(state_->pixels, std::nullopt);
}

void TextureUpscaleJob::cancel() {
  {
    const std::scoped_lock lock{state_->mutex};
    state_->cancelled = true;
    state_->pixels.reset();
  }
  thread_.request_stop();
}

double TextureUpscaleJob::milliseconds() const {
  const std::scoped_lock lock{state_->mutex};
  return state_->milliseconds;
}

} // namespace dwe::render
