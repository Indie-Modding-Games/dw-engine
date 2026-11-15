#pragma once

#include <dwe/assets/image.hpp>
#include <dwe/render/upscale_cache.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace dwe::render {

// A model texture's RGBA pixels as TextureFilter::Smooth (doublings 0: transparent texels take
// their neighbours' colour) or Upscaled (1: doubled by the edge-directed upscaler) uploads them,
// kept in the cache when there is one (only doubled ones are kept).
[[nodiscard]] std::vector<std::byte>
modelTexturePixels(const assets::Image& image, std::uint32_t doublings, const UpscaleCache* cache);

// Makes a model's textures on a thread of its own: the model draws at once with plain textures and
// takes these once they are ready (ModelRenderer::setUpscaleInBackground), so a model loaded while
// the game runs (the status portrait) never holds a frame for its upscale (#325).
class TextureUpscaleJob {
public:
  // What makes one image's pixels on the job's thread.
  using Make = std::function<std::vector<std::byte>(const assets::Image&)>;

  // Starts making each image's pixels, in order; returns at once.
  TextureUpscaleJob(std::vector<assets::Image> images, Make make);
  TextureUpscaleJob(const TextureUpscaleJob&) = delete;
  TextureUpscaleJob& operator=(const TextureUpscaleJob&) = delete;
  TextureUpscaleJob(TextureUpscaleJob&&) = delete;
  TextureUpscaleJob& operator=(TextureUpscaleJob&&) = delete;
  // Cancels it and waits for the image under way.
  ~TextureUpscaleJob();

  // Whether the thread is done: every image made, or stopped after a cancel.
  [[nodiscard]] bool finished() const;
  // Waits, at most `timeout`, for it to finish; whether it did.
  bool wait(std::chrono::milliseconds timeout) const;
  // The pixels, one per image, once finished and not cancelled; nothing before, after a cancel,
  // or a second time.
  [[nodiscard]] std::optional<std::vector<std::vector<std::byte>>> take();
  // The thread stops after the image under way; take() gives nothing from then on.
  void cancel();
  // How long the thread took, once finished.
  [[nodiscard]] double milliseconds() const;

private:
  struct State;
  std::shared_ptr<State> state_;
  std::jthread thread_;
};

} // namespace dwe::render
