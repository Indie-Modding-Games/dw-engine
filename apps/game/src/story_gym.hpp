#pragma once

#include <dwe/session/story_scenes.hpp>

#include "gym_screen.hpp"

namespace dwe::app {

// The story's gym (call_routine 39 and 7): it clears the course's completion for GymLibrary, starts
// the course pstat 254 picks for GymCourse and tells the story when the course is complete.
class StoryGym {
public:
  // The gym screen must outlive this object; without one the story's courses end at once.
  void attach(GymScreen* gym) { gym_ = gym; }
  // Takes the story's GymLibrary and GymCourse screens; false for any other screen.
  bool take(const session::StoryScreenRequest& request);
  // Tells the story that the course it waits on is complete.
  void update(session::StoryScenes& scenes);

private:
  GymScreen* gym_ = nullptr;
  // Whether the story waits on a course.
  bool waiting_ = false;
  // The course could not start: the story goes on at once.
  bool over_ = false;
};

} // namespace dwe::app
