#pragma once

#include <dwe/session/field_items_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <string_view>

#include "field_scene.hpp"
#include "partner_walk_up.hpp"

namespace dwe::app {

inline constexpr std::string_view FieldPraiseCommand = "praise";
inline constexpr std::string_view FieldScoldCommand = "scold";

// Praise and Scold in the pause menu (game_menu.c: the triangle menu's options 4 and 5, partner
// states 15 and 4): the partner walks up to the player and the two turn to each other; a praise
// plays its sound. Then the player gestures and the partner answers (tamer.c:
// tamerTickPraiseScold, where a scold plays its sound); once the player's gesture ends the partner
// is praised or scolded (handlePraiseScold), and the scene ends with the partner's clip (partner.c:
// partnerTickPraiseScold). The game time runs meanwhile; the player waits.
class PraiseScoldCall {
public:
  // The session and the UI must outlive this object.
  PraiseScoldCall(session::GameSession& session, ui::Ui& ui);

  // Whether the scene plays: the player waits meanwhile.
  [[nodiscard]] bool active() const { return step_ != Step::None; }

  // Takes the pause menu's praise and scold; false for any other command.
  bool handle(const ui::UiCommand& command);

  // Once per frame: whether the pause menu could open over the field, which praise and scold need.
  void update(bool fieldIdle);

  // One tick while the scene plays; true on the tick the partner is praised or scolded.
  bool tick(FieldScene& scene, float seconds, const session::FieldItemsAdapter& fieldItems);

private:
  enum class Step : std::uint8_t { None, Walk, Gesture, Answer };

  // The partner is beside the player: the sounds and the clips start.
  void gesture(FieldScene& scene, const session::FieldItemsRules* rules);
  // handlePraiseScold.
  void apply();
  void finish(FieldScene& scene);

  session::GameSession* session_;
  ui::Ui* ui_;
  PartnerWalkUp walkUp_;
  Step step_ = Step::None;
  bool scold_ = false;
  bool fieldIdle_ = false;
};

} // namespace dwe::app
