#pragma once

#include <dwe/session/game_session.hpp>
#include <dwe/session/poop_adapter.hpp>

#include "field_scene.hpp"

namespace dwe::app {

// The partner's poop piles on the field (partner_impl.c: createPoopPile, renderPoop): a pile the
// partner left is placed in front of where it stands, and the piles of the map shown are drawn
// with the `poop_piles` look, at the partner's height.
class FieldPoop {
public:
  // Everything must outlive this object.
  FieldPoop(session::GameSession& session, session::PoopAdapter& poop, FieldScene& scene);

  // While the field is idle on a map: places the piles the partner left.
  void placePending();

  // Hands the scene the piles of the map shown, once a frame.
  void show();

private:
  session::GameSession* session_;
  session::PoopAdapter* poop_;
  FieldScene* scene_;
};

} // namespace dwe::app
