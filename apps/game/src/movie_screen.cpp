#include "movie_screen.hpp"

#include <dwe/core/log.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string_view>
#include <utility>

namespace dwe::app {

namespace {

// The screen the original shows a movie on, and where on it the picture goes (mov.c:
// MOV_initEnvironment(&MOV_ENV, 0, 32, 0, 272): 176 lines from line 32 of 240).
constexpr float ScreenWidth = 320.0F;
constexpr float ScreenHeight = 240.0F;
constexpr float PictureTop = 32.0F;

// The actions that skip a movie. The original reads only Start (mov.c: MOV_playMovie reads
// PadRead & 0x800), here "pause"; on a PC menu, confirm and cancel skip too, as Tab, Return or
// Escape would be the first key a player tries.
constexpr std::array<std::string_view, 4> SkipActions = {"pause", "menu", "confirm", "cancel"};

} // namespace

core::Result<std::unique_ptr<MovieScreen>> MovieScreen::create(render::Renderer& renderer,
                                                               audio::Audio& sound) {
  auto quads = render::QuadBatch::create(renderer, rhi::BlendMode::Opaque);
  if (!quads) {
    return std::unexpected{quads.error()};
  }
  return std::make_unique<MovieScreen>(Key{}, renderer, sound, std::move(*quads));
}

MovieScreen::~MovieScreen() {
  stop();
}

bool MovieScreen::play(const assets::VirtualFileSystem& files, const MovieRule& rule) {
  stop();
  auto player = video::MoviePlayer::open(files, rule.file);
  if (!player) {
    core::logWarning("movie '{}' does not play: {}", rule.file, player.error().message);
    core::logDebug("[port:movie] MOV_playMovie (mov.c:225): '{}' is not there, nothing plays",
                   rule.file);
    return false;
  }
  auto texture =
      renderer_->queueTexture({.width = player->info().width, .height = player->info().height});
  if (!texture) {
    core::logWarning("movie '{}' does not play: {}", rule.file, texture.error().message);
    return false;
  }
  texture_ = *texture;
  auto sound = video::MoviePlayer::loadSound(files, rule.file);
  if (!sound) {
    core::logWarning("movie '{}' plays without sound: {}", rule.file, sound.error().message);
  } else if (*sound) {
    soundRate_ = (*sound)->sampleRate;
    auto clip = std::make_shared<const audio::Clip>(audio::Clip{.sampleRate = (*sound)->sampleRate,
                                                                .channels = (*sound)->channels,
                                                                .pcm = std::move((*sound)->pcm),
                                                                .loop = std::nullopt});
    auto voice = sound_->play({.clip = std::move(clip), .bus = audio::Bus::Music});
    if (voice) {
      voice_ = *voice;
    } else {
      core::logWarning("movie '{}' plays without sound: {}", rule.file, voice.error().message);
    }
  }
  player_.emplace(std::move(*player));
  file_ = rule.file;
  skippable_ = rule.skippable;
  armed_ = false;
  clock_ = 0.0;
  elapsed_ = 0.0;
  loggedDrift_ = 0.0;
  core::logInfo("movie '{}' starts", rule.file);
  core::logDebug("[port:movie] MOV_playMovie (mov.c:239): '{}' starts, skippable {}, sound {}",
                 rule.file, rule.skippable, voice_.has_value());
  return true;
}

void MovieScreen::tick(const input::InputState& input) {
  if (!player_ || std::ranges::none_of(SkipActions, [&](std::string_view action) {
        return input.wasPressed(action);
      })) {
    armed_ = player_.has_value();
    return;
  }
  // The press that started the movie (the interview's last answer) does not skip it: only a press
  // on a later tick, after one without a press, does.
  if (!armed_) {
    return;
  }
  if (!skippable_) {
    core::logDebug("[port:movie] MOV_playMovie (mov.c:265): a skip at {:.2f} s keeps '{}'", clock_,
                   file_);
    return;
  }
  core::logInfo("movie skipped");
  core::logDebug("[port:movie] MOV_playMovie (mov.c:265): a skip key ends '{}' at {:.2f} s", file_,
                 clock_);
  stop();
}

// The sound leads: while it plays, the clock is its position; before it starts, or once it has
// ended or failed, game time moves the clock on from there.
double MovieScreen::clockAfter(double frameSeconds) const {
  if (voice_ && soundRate_ > 0 && sound_->isActive(*voice_)) {
    const auto position = sound_->position(*voice_);
    return position ? static_cast<double>(*position) / soundRate_ : clock_;
  }
  return clock_ + frameSeconds;
}

void MovieScreen::update(double frameSeconds) {
  if (!player_ || !texture_) {
    return;
  }
  // Bound once: the calls below leave both in place until stop(), which ends the update.
  auto& player = *player_;
  const render::TextureId texture = *texture_;
  clock_ = clockAfter(frameSeconds);
  elapsed_ += frameSeconds;
  logDrift();
  auto changed = player.advanceTo(clock_);
  if (!changed) {
    core::logWarning("movie stops: {}", changed.error().message);
    stop();
    return;
  }
  if (*changed) {
    const auto& info = player.info();
    if (auto updated = renderer_->queueTextureUpdate(
            texture, {.x = 0, .y = 0, .width = info.width, .height = info.height},
            player.picture());
        !updated) {
      core::logWarning("movie picture not shown: {}", updated.error().message);
    }
  }
  if (player.finishedAt(clock_)) {
    core::logInfo("movie ends");
    core::logDebug("[port:movie] MOV_readFrame (mov.c:191): '{}' showed its {} frames at {:.2f} s",
                   file_, player.info().shownFrames, clock_);
    stop();
    return;
  }
  showPicture(player, texture);
}

// How far the sound's clock is from the frames' time, logged when it moves by DriftStep: the
// sound leads, so a growing gap is the game falling behind.
void MovieScreen::logDrift() {
  constexpr double DriftStep = 0.1;
  const double drift = clock_ - elapsed_;
  if (std::abs(drift - loggedDrift_) >= DriftStep) {
    loggedDrift_ = drift;
    core::logDebug("[port:movie] MOV_playMovie (mov.c:256): '{}' sound clock {:.2f} s, game time "
                   "{:.2f} s, drift {:+.2f} s",
                   file_, clock_, elapsed_, drift);
  }
}

// The original's screen fitted to the window with its shape kept, and the picture on it.
void MovieScreen::showPicture(const video::MoviePlayer& player, render::TextureId texture) {
  quads_.clear();
  if (player.picture().empty()) {
    return;
  }
  const rhi::Extent target = renderer_->targetExtent();
  const float scale = std::min(static_cast<float>(target.width) / ScreenWidth,
                               static_cast<float>(target.height) / ScreenHeight);
  const glm::vec2 screenOrigin{(static_cast<float>(target.width) - (ScreenWidth * scale)) / 2.0F,
                               (static_cast<float>(target.height) - (ScreenHeight * scale)) / 2.0F};
  const auto& info = player.info();
  quads_.add(
      {.position = screenOrigin + glm::vec2{0.0F, PictureTop * scale},
       .size = glm::vec2{static_cast<float>(info.width), static_cast<float>(info.height)} * scale,
       .texture = texture});
}

// The original pauses the stream and clears the screen (mov.c: MOV_playMovie, after its loop).
void MovieScreen::stop() {
  if (voice_) {
    sound_->stop(*voice_);
    voice_.reset();
  }
  if (texture_) {
    renderer_->destroyTexture(*texture_);
    texture_.reset();
  }
  player_.reset();
  quads_.clear();
  soundRate_ = 0;
}

} // namespace dwe::app
