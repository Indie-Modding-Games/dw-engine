#pragma once

#include <dwe/game/field/field.hpp>
#include <dwe/session/field_items_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/ui/ui.hpp>

#include <glm/vec3.hpp>

#include <cstdint>

#include "field_scene.hpp"

namespace dwe::app {

// The toilet cell (tamer.c: checkMapInteraction): while the player stands on a cell of value 120
// and the partner needs the toilet, the partner goes (map section 0x4e2: call_partner_routine(0),
// partner state 6; then play_sound(0, 10)). The player stands while the game time runs; the camera
// moves to the toilet; the partner runs to the map's two toilet points, goes (partner_impl.c:
// handleToilet) and walks back to the first point (partner.c: tickPartnerToilet). Without the
// ruleset's toilet_walk, or without toilet points for the map, it goes where it stands.
class FieldToilet {
public:
  // Everything must outlive this object.
  FieldToilet(session::GameSession& session, const session::FieldItemsAdapter& fieldItems,
              FieldScene& scene);

  [[nodiscard]] bool active() const { return step_ != Step::None; }

  // Keeps the pause menu shut while the partner goes; false for any other command.
  [[nodiscard]] bool handle(const ui::UiCommand& command) const;

  // While the field is idle: starts the walk when the player stands on a toilet cell and the
  // partner needs it. Returns whether the partner went at once (no walk).
  bool checkCell();

  // One tick while active, the field's and the game time's; true on the tick the partner went.
  bool tick(float seconds);

private:
  enum class Step : std::uint8_t { None, ToFirst, ToSecond, Going, Back };

  void goNow();
  void finish();
  [[nodiscard]] float frames(std::int32_t count) const;

  session::GameSession* session_;
  const session::FieldItemsAdapter* fieldItems_;
  FieldScene* scene_;
  Step step_ = Step::None;
  // The ruleset's toilet walk while the partner goes.
  const session::ToiletWalkRules* rules_ = nullptr;
  glm::vec3 first_{};
  glm::vec3 second_{};
};

} // namespace dwe::app
