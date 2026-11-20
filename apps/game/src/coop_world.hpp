#pragma once

#include <dwe/game/coop/authority.hpp>
#include <dwe/game/coop/protocol.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/net/peer_link.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::app {

class FieldScene;
struct PresentedCoopWorld;

// A field character's pose as the session keeps it, and back (the wire's integral units).
[[nodiscard]] game::coop::PlayerPose coopPoseOf(std::string mapId,
                                                const game::field::CharacterState& state);
[[nodiscard]] game::field::CharacterState fieldStateOf(const game::coop::TrackedPose& pose);
[[nodiscard]] scene::Motion sceneMotionOf(game::coop::Motion motion);

// A message for one other player of the session.
struct CoopOutgoing {
  game::coop::PlayerId to = game::coop::HostPlayerId;
  game::coop::Message message;
};

// The host's side of the shared world (#353, #354, #355), built when its co-op game links a guest
// and dropped with that game. The authority holds who is in the session and each player's newest
// valid transform; this drives the guests' bodies in the host's field from the states their
// owners report (smoothed, as remote motion), puts them where the host goes, and says what to send.
class CoopHostWorld {
public:
  CoopHostWorld(std::string hostName, std::string hostPartner);

  [[nodiscard]] const game::coop::WorldAuthority& authority() const { return authority_; }
  // A guest's handshake completed: its record (none when the session is full).
  [[nodiscard]] std::optional<game::coop::PlayerId> join(std::string name, std::string partner,
                                                         net::Millis now);
  // A guest left, dropped or went quiet: its record and its track go.
  void leave(game::coop::PlayerId id);
  // A message received on guest `from`'s link (only its PlayerState matters here).
  void receive(game::coop::PlayerId from, const game::coop::Message& message, net::Millis now);
  // After the host's field tick: the host's pose and map into the authority, the guests' bodies
  // driven or put in place, and what to send (corrections, the roster when it changed).
  // `mapSerial` grows on every map the scene enters; `arrivalExit` is how it entered this one.
  [[nodiscard]] std::vector<CoopOutgoing> tick(game::field::Field& field, std::uint32_t mapSerial,
                                               std::optional<std::int32_t> arrivalExit,
                                               net::Millis now);

private:
  game::coop::WorldAuthority authority_;
  std::map<game::coop::PlayerId, game::coop::PoseTrack> tracks_;
  // Guests put in the field on the current map (the others get a correction on the next tick).
  std::set<game::coop::PlayerId> placed_;
  std::optional<std::uint32_t> mapSerial_;
  std::uint32_t rosterSent_ = 0;
  std::map<game::coop::PlayerId, game::coop::StateVerdict> verdicts_;
};

// A guest's side of the shared world: it follows the host's corrections of its own player, keeps
// the host's roster and reports its own transform; it takes no exit of its own (the group goes
// where the host goes).
class CoopGuestWorld {
public:
  explicit CoopGuestWorld(game::coop::PlayerId self) : self_{self} {}

  [[nodiscard]] game::coop::PlayerId self() const { return self_; }
  void receive(const game::coop::Message& message);
  // Where the host put this guest's player and the field has yet to take it: on this map a place,
  // on another a map change that arrives at the host's spot (FieldScene::moveCoopSelf).
  struct Move {
    std::string map;
    std::optional<std::int32_t> exit;
    assets::SpawnPoint at;
  };
  // Before the guest's field tick, with the field and whether it still goes to the map the last
  // move asked for: the move to make now, if any. Once the player stands where the host said, the
  // reports carry that correction's epoch.
  [[nodiscard]] std::optional<Move> apply(const game::field::Field& field, bool moving);
  // After the field tick `tick` (it counts the guest's ticks): the state to report, every few
  // ticks, while the player stands on the map the host put it on.
  [[nodiscard]] std::optional<game::coop::PlayerState>
  report(const game::field::Field& field, bool moving, std::uint64_t tick) const;
  // The session's players as the host last listed them (none before its first roster).
  [[nodiscard]] const std::optional<game::coop::Roster>& roster() const { return roster_; }
  [[nodiscard]] bool inSession(game::coop::PlayerId id) const;
  // The partner species the roster gives player `id` (empty when none).
  [[nodiscard]] std::string partnerOf(game::coop::PlayerId id) const;

private:
  game::coop::PlayerId self_;
  // The newest correction applied: states carry it so the host drops ones from before.
  std::uint16_t epoch_ = 0;
  std::optional<game::coop::PlayerCorrection> pending_;
  // The move asked for pending_, until the field arrives.
  bool asked_ = false;
  std::optional<game::coop::Roster> roster_;
};

// The presence check of what is shared per map, such as battles (#351): whether `player` stands on
// `field`'s map by the session's presence query (`here`: the host's WorldAuthority::playersOn or a
// guest's CoopGameplayBridge::playersOn, for that map), with the field not between maps.
[[nodiscard]] bool coopPlayerHere(const game::field::Field& field,
                                  std::span<const game::coop::PlayerId> here,
                                  game::coop::PlayerId player);

// The guest's side, against its scene: the host's move of its player, before the field tick.
void applyCoopMove(FieldScene& scene, CoopGuestWorld& guest);

// A guest draws the other players of the session as the host's snapshot smoothed for this frame
// shows them (`world`; none before the first): each player and its partner by id, hidden while
// the guest is not on that map, and gone once the roster no longer lists the player.
// `hostPartner` is the host's partner species for a roster that does not give it.
void presentCoopPlayers(FieldScene& scene, const PresentedCoopWorld* world,
                        const CoopGuestWorld& guest, std::string_view hostPartner);

} // namespace dwe::app
