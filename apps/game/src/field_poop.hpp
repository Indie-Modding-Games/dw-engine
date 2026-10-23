#pragma once

#include <dwe/game/partner/poop_piles.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/poop_adapter.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>

#include "field_scene.hpp"

namespace dwe::app {

// The partner's poop piles on the field (partner_impl.c: createPoopPile, renderPoop): a pile the
// partner left is placed in front of where it stands, and the piles of the map shown are drawn
// with the `poop_piles` look, at the partner's height. A pile on the field plays its scene first
// (partner.c: tickWildPoop, partner state 7): the player stops and looks, the partner runs up to
// it, squats, leaves the pile with its care effects and plays its after clip; the tamer level may
// drop at the end. A partner that eats piles goes to one near
// the player and eats it (partner_impl.c: tickPoopDetection; partner.c: partnerTickEatShit, state
// 9). The player stands while the game time runs.
class FieldPoop {
public:
  // Everything must outlive this object.
  FieldPoop(session::GameSession& session, session::PoopAdapter& poop, FieldScene& scene);

  [[nodiscard]] bool active() const { return step_ != Step::None; }

  // Keeps the pause menu shut during a scene; false for any other command.
  [[nodiscard]] bool handle(const ui::UiCommand& command) const;

  // While the field is idle on a map: places the piles the partner left, a pile on the field with
  // its scene, or sends a partner that eats piles to one near the player.
  void update();

  // One tick while active, the field's and the game time's; true on the tick the partner ate.
  bool tick(float seconds);

  // Hands the scene the piles of the map shown, once a frame.
  void show();

private:
  enum class Step : std::uint8_t { None, RunUp, Squat, After, ToPile, Eat };

  void placeOne();
  void checkEating();
  void finish();
  void leavePile();
  void endScene();

  session::GameSession* session_;
  session::PoopAdapter* poop_;
  FieldScene* scene_;
  Step step_ = Step::None;
  // The tile of the pile the partner goes to eat.
  game::partner::Tile pileTile_{};
};

} // namespace dwe::app
