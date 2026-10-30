#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/game/gym/course_flow.hpp>
#include <dwe/game/gym/growing_box.hpp>
#include <dwe/game/gym/gym_rules.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/ui/ui.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "field_scene.hpp"

namespace dwe::app {

inline constexpr std::string_view GymStatsDocument = "gym_stats";
inline constexpr std::string_view GymSlotsDocument = "gym_slots";
// gym("<course>"): runs a course of the gym (gym.yaml: courses), on its map only.
inline constexpr std::string_view GymCommand = "gym";

struct GymScreenDesc {
  session::GameSession* session = nullptr;
  session::StoryAdapter* story = nullptr;
  session::InventoryAdapter* inventory = nullptr;
  session::BattleAdapter* battle = nullptr;
  // The species' moves and specials, for the moves the brains course teaches; none teaches none.
  const assets::DigimonTable* species = nullptr;
  FieldScene* scene = nullptr;
  ui::Ui* ui = nullptr;
};

// A line of the brains course's box under the stats: its text key, its row in pixels down the box,
// and whether it is an order's words (yellow) or a line of the shorter box of a move learned.
struct GymNotice {
  std::string text;
  std::int32_t row = 0;
  bool order = false;
  bool move = false;
};

// The town gym on the field (src/trn): a course runs while the field goes on without the player's
// control, and its stats box counts the gains. The story starts it with routines 39 and 7 and
// waits for takeComplete(); the console starts a course directly.
class GymScreen {
public:
  // Everything in `desc` must outlive this object.
  explicit GymScreen(const GymScreenDesc& desc);
  GymScreen(const GymScreen&) = delete;
  GymScreen& operator=(const GymScreen&) = delete;
  GymScreen(GymScreen&&) = delete;
  GymScreen& operator=(GymScreen&&) = delete;
  ~GymScreen();

  // Routine 39 (map.c: loadTrainingLibrary): the course about to start is not complete.
  void loadLibrary();
  // Routine 7: the course the story's course variable picks.
  core::Result<void> startChosen();
  // A course by id (gym.yaml: courses).
  core::Result<void> start(std::string_view course);
  // tamer.c: isTrainingComplete: whether the last course finished, once.
  [[nodiscard]] bool takeComplete();

  // Whether a course runs: the player has no control meanwhile.
  [[nodiscard]] bool active() const { return run_ != nullptr; }

  // The original's cancel (dw/input.h: CANCEL_BUTTON, the triangle on the American pad) is the
  // menu button here: while a course runs it ends the session instead of opening the menu.
  void pressCancel(bool pressed, bool held);

  // One logic tick of the course, with the player's buttons. Without a course it only closes the
  // stats box of the last one.
  void tick(double seconds, const game::field::FieldInput& intent);

  // Once per frame: shows the roulette while it runs, and the stats box while a course counts its
  // gains.
  void update();

private:
  class Ports;

  void updateSlots();
  // The roulette's and the stats' boxes grow and shrink (ui.c: renderUIBoxAnim).
  void tickBoxes(double seconds);
  void openBox(game::gym::GrowingBox& box, game::gym::BoxRect from, game::gym::BoxRect to) const;
  [[nodiscard]] game::gym::BoxRect partnerOnScreen() const;
  [[nodiscard]] game::gym::BoxRect machineOnScreen() const;
  [[nodiscard]] std::optional<game::gym::Point>
  pixelsFromMiddle(std::optional<glm::vec2> fraction) const;
  void playUiSound(std::int32_t sound) const;
  void publishCountup(screens::ScreenSettings& settings,
                      const game::gym::StatCountup& countup) const;

  GymScreenDesc desc_;
  game::gym::GymRules rules_;
  std::vector<GymNotice> notices_;
  std::unique_ptr<Ports> ports_;
  std::unique_ptr<game::gym::CourseRun> run_;
  // TRAINING_COMPLETE.
  bool complete_ = false;
  bool cancelPressed_ = false;
  bool cancelHeld_ = false;
  bool boxShown_ = false;
  game::gym::GrowingBox statsBox_;
  game::gym::GrowingBox slotsBox_;
  double boxSeconds_ = 0.0;
  std::optional<game::gym::BoxRect> publishedOutline_;
  bool publishedStanding_ = false;
  bool slotsShown_ = false;
  std::optional<game::gym::GymStats> published_;
  std::size_t publishedNotices_ = 0;
};

} // namespace dwe::app
