#include "story_gym.hpp"

#include <dwe/core/log.hpp>

namespace dwe::app {

bool StoryGym::take(const session::StoryScreenRequest& request) {
  if (request.screen == session::StoryScreen::GymLibrary) {
    if (gym_ != nullptr) {
      gym_->loadLibrary();
    }
    return true;
  }
  if (request.screen != session::StoryScreen::GymCourse) {
    return false;
  }
  waiting_ = request.waits;
  over_ = gym_ == nullptr;
  if (gym_ != nullptr) {
    if (auto started = gym_->startChosen(); !started) {
      core::logWarning("[port:gym] call_routine 7 (script_interp.c:1682): {}; the course is over",
                       started.error().message);
      over_ = true;
    }
  }
  return true;
}

void StoryGym::update(session::StoryScenes& scenes) {
  if (!waiting_) {
    return;
  }
  if (over_ || gym_->takeComplete()) {
    waiting_ = false;
    over_ = false;
    scenes.screenClosed();
    core::logDebug("[port:gym] tickScript call_routine 7 (script_interp.c:459): course complete");
  }
}

} // namespace dwe::app
