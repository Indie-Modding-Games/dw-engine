#pragma once

#include <dwe/debug/tuning.hpp>
#include <dwe/session/game_session.hpp>

namespace dwe::app {

// The tuning panel's "Partner" tab: what the session's partner view shows, read every frame the tab is open. The
// session must outlive the panel.
debug::TuningTab partnerTab(const session::GameSession& session);

} // namespace dwe::app
