#pragma once

#include <dwe/game/coop/protocol.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/shared_clock.hpp>

#include <array>
#include <set>
#include <span>
#include <string>
#include <string_view>

#include "coop_gameplay_bridge.hpp"

namespace dwe::app {

// The first course a player runs in a co-op game says once that the world's time goes on.
inline constexpr std::string_view CoopTrainingClockText = "online.coop.training_clock";

// One guest the host tells the clock and the trainings to.
struct CoopTrainingPeer {
  game::coop::PlayerId id = game::coop::GuestPlayerId;
  CoopGameplayBridge* bridge = nullptr;
};

// Training in a shared world (#365, docs/architecture/coop-training.md), an engine addition. The
// host tells every guest its clock, the world's, about once a second and at once when it stops,
// starts or jumps; a guest's session follows it (session::ClockShare::Guest). Each player tells who
// trains: a guest its own course to the host, the host everyone's to every guest, so a tag can
// say a player trains and a trade or a battle leaves them be.
class CoopTraining {
public:
  // The host's tick, after the game's: the clock to each guest that needs it, their trainings in,
  // everyone's out. `ownCourse` is the host's own (PlaySession::trainingCourse).
  void hostTick(const session::GameSession& session, std::string_view ownCourse,
                std::span<const CoopTrainingPeer> guests, double realSeconds);
  // A guest's tick: the host's clock into its session, the others' trainings in, its own out.
  void guestTick(session::GameSession& session, std::string_view ownCourse,
                 game::coop::PlayerId self, CoopGameplayBridge& host);

  // Whether that player's partner is on a course now.
  [[nodiscard]] bool training(game::coop::PlayerId id) const;
  // True once a co-op game, as this player's first course starts.
  [[nodiscard]] bool takeClockNotice();

  // A guest left: its training ends, and it hears everything again if it comes back.
  void playerLeft(game::coop::PlayerId id);
  // The game ended.
  void reset();

private:
  void own(game::coop::PlayerId self, std::string_view course);

  session::WorldClockTeller teller_;
  std::set<game::coop::PlayerId> clockTold_;
  // Each player's course, empty when it does not train.
  std::array<std::string, game::coop::MaxPlayers> courses_{};
  // The host's side: what it told each guest of each player's course.
  std::array<std::array<std::string, game::coop::MaxPlayers>, game::coop::MaxPlayers> told_{};
  // A guest's side: its own course as it last told the host.
  std::string sent_;
  bool sentOnce_ = false;
  bool noticeShown_ = false;
  bool noticeDue_ = false;
};

} // namespace dwe::app
