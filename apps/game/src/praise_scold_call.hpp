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
// states 15 and 4): the partner walks up to the player, the two turn to each other, a sound plays,
// and the partner is praised or scolded (partner.c: partnerTickPraiseScold, handlePraiseScold;
// tamer.c: tamerTickPraiseScold). The game time runs meanwhile; the player waits.
class PraiseScoldCall {
public:
  // The session and the UI must outlive this object.
  PraiseScoldCall(session::GameSession& session, ui::Ui& ui);

  // Whether the partner walks up: the player waits meanwhile.
  [[nodiscard]] bool active() const { return walkUp_.walking(); }

  // Takes the pause menu's praise and scold; false for any other command.
  bool handle(const ui::UiCommand& command);

  // Once per frame: whether the pause menu could open over the field, which praise and scold need.
  void update(bool fieldIdle);

  // One tick while the partner walks up; true on the tick it is praised or scolded.
  bool tick(FieldScene& scene, float seconds, const session::FieldItemsAdapter& fieldItems);

private:
  session::GameSession* session_;
  ui::Ui* ui_;
  PartnerWalkUp walkUp_;
  bool scold_ = false;
  bool fieldIdle_ = false;
};

} // namespace dwe::app
