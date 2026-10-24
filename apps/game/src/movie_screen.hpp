#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/audio/audio.hpp>
#include <dwe/audio/clip.hpp>
#include <dwe/core/error.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/render/quad_batch.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/video/movie_player.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "movie_rules.hpp"

namespace dwe::app {

// Plays the packs' movies over a black screen: the picture letterboxed as the original shows it
// (320 x 176 at line 32 of a 320 x 240 screen, mov.c: MOV_playMovie), scaled to the window, and
// the sound on the music bus, whose position is the clock the pictures follow.
class MovieScreen {
  // Only create builds one, which needs make_unique to reach the constructor.
  struct Key {
    explicit Key() = default;
  };

public:
  static core::Result<std::unique_ptr<MovieScreen>> create(render::Renderer& renderer,
                                                           audio::Audio& sound);

  MovieScreen(const MovieScreen&) = delete;
  MovieScreen& operator=(const MovieScreen&) = delete;
  MovieScreen(MovieScreen&&) = delete;
  MovieScreen& operator=(MovieScreen&&) = delete;
  ~MovieScreen();
  MovieScreen(Key /*key*/, render::Renderer& renderer, audio::Audio& sound, render::QuadBatch quads)
      : renderer_(&renderer), sound_(&sound), quads_(std::move(quads)) {}

  // Starts a movie; false, with a warning, when it cannot play (a pack without it, an old
  // extraction), and the caller goes on as if it had ended, as the original does when the file is
  // not on the disc.
  bool play(const assets::VirtualFileSystem& files, const MovieRule& rule);

  [[nodiscard]] bool playing() const { return player_.has_value(); }

  // Once per tick: Start, confirm or cancel ends a skippable movie.
  void tick(const input::InputState& input);

  // Once per frame: moves the clock, shows the picture of the moment and ends the movie at its
  // last frame.
  void update(double frameSeconds);

  // Draws the picture; nothing when no movie plays.
  [[nodiscard]] render::Drawer& drawer() { return quads_; }

private:
  [[nodiscard]] double clockAfter(double frameSeconds) const;
  void showPicture(const video::MoviePlayer& player, render::TextureId texture);
  void logDrift();
  void stop();

  render::Renderer* renderer_;
  audio::Audio* sound_;
  render::QuadBatch quads_;
  std::optional<video::MoviePlayer> player_;
  std::optional<render::TextureId> texture_;
  std::optional<audio::VoiceId> voice_;
  std::uint32_t soundRate_ = 0;
  std::string file_;
  bool skippable_ = true;
  // Set once a tick has seen no skip key since the movie started.
  bool armed_ = false;
  double clock_ = 0.0;
  // Game time since the start, and the drift last logged.
  double elapsed_ = 0.0;
  double loggedDrift_ = 0.0;
};

} // namespace dwe::app
