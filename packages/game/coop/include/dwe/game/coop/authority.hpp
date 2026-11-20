#pragma once

#include <dwe/game/coop/protocol.hpp>
#include <dwe/net/peer_link.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dwe::game::coop {

// Where a player stands: the map, the position in integral world units, the yaw in 1/65536 of a
// turn and how it moves. The same quantization as the wire, so the host keeps exactly what it
// sends and compares like with like.
struct PlayerPose {
  std::string mapId;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;
  std::uint16_t yaw = 0;
  Motion motion = Motion::Idle;

  friend bool operator==(const PlayerPose&, const PlayerPose&) = default;
};

// One player of the session, as the authority keeps it (#354, #355).
struct PlayerRecord {
  PlayerId id = HostPlayerId;
  std::string name;
  std::string partnerSpecies;
  // Bumped each time the host puts the player somewhere itself (join, map change, refused state).
  std::uint16_t epoch = 0;
  // The newest state taken from the player's owner, if any since the last epoch.
  std::optional<std::uint32_t> sequence;
  // The newest valid pose: the owner's newest valid state, or where the host put it.
  std::optional<PlayerPose> pose;
  // When the owner was last heard from, and when its newest state was taken.
  net::Millis heardAt{0};
  net::Millis acceptedAt{0};
  // States refused since the newest one taken: past a few, the host corrects the player.
  std::uint32_t refusedInRow = 0;
};

// What the authority made of a player state. Only Accepted changes the record.
enum class StateVerdict : std::uint8_t {
  Accepted,
  // No such player in the session (it left, or never joined).
  UnknownPlayer,
  // The link that sent it does not own that player: a client never writes another's state.
  NotOwner,
  // Not newer than the newest one taken (late, duplicated or reordered on the way).
  Stale,
  // Sent before the guest applied the host's newest correction of it.
  OldEpoch,
  // Not on the map the session is on.
  WrongMap,
  // Farther from the newest valid pose than the player can move in that time.
  TooFar,
};

[[nodiscard]] std::string_view stateVerdictName(StateVerdict verdict);

struct AuthorityRules {
  // The fastest a player moves (world units per second) and a fixed allowance on top.
  float maxSpeed = 2000.0F;
  float slack = 300.0F;
  // The sender's ticks per second (PlayerState::sequence counts them).
  std::uint32_t ticksPerSecond = 60;
  // How much a sequence gap may run ahead of the real time between two states (a sender cannot
  // buy distance by skipping sequence numbers).
  net::Millis clockSlack{1000};
  // Refused states in a row before the host corrects the player to its newest valid pose.
  std::uint32_t refusalsBeforeCorrection = 3;
};

// The host's authority over the shared session state (#353): who is in it (the player registry,
// #355), the map the group is on and every player's newest valid transform (#354). It holds no
// field or scene: the app applies what it decides. Single player never builds one.
//
// What a guest may write: only its own player's transform, through accept(), which drops anything
// stale, from before a correction, on another map or impossibly far. Everything else in the
// session (the map, the story, the clock, the NPCs, the partners, battles) is the host's own
// simulation; guests only receive it.
class WorldAuthority {
public:
  explicit WorldAuthority(std::string hostName = "Host", std::string hostPartner = {},
                          AuthorityRules rules = {});

  // Registry. A joining guest takes the lowest free id (none when the session is full): a guest
  // that comes back after it left joins again as a new record, with a fresh epoch and sequence, so
  // nothing of its old link can write into the new one.
  [[nodiscard]] std::optional<PlayerId> join(std::string name, std::string partnerSpecies,
                                             net::Millis now);
  // Removes a guest (left, dropped or timed out); false when it was not there. The host stays.
  bool leave(PlayerId id);
  void setPartner(PlayerId id, std::string partnerSpecies);
  [[nodiscard]] const PlayerRecord* find(PlayerId id) const;
  [[nodiscard]] std::span<const PlayerRecord> players() const { return players_; }
  [[nodiscard]] std::size_t size() const { return players_.size(); }
  [[nodiscard]] bool full() const { return players_.size() >= MaxPlayers; }
  // Changes on every join, leave or partner change: the roster to send when it moved on.
  [[nodiscard]] std::uint32_t revision() const { return revision_; }
  [[nodiscard]] Roster roster() const;

  // The map the session is on: the host's. A player's state on another map is refused.
  void setWorldMap(std::string mapId);
  [[nodiscard]] const std::string& worldMap() const { return worldMap_; }

  // Presence (for what is shared per map, such as battles): the players whose newest valid pose is
  // on `mapId`, the host included, by id.
  [[nodiscard]] std::vector<PlayerId> playersOn(std::string_view mapId) const;

  // The host's own player, from its simulation each tick (the host is its own authority).
  void setHostPose(PlayerPose pose, net::Millis now);

  // A guest's state, received on the link of player `from`.
  StateVerdict accept(PlayerId from, const PlayerState& state, net::Millis now);
  // The host puts a guest at a pose itself: a new epoch, the pose taken as the newest valid one,
  // and the correction to send that guest. None for an unknown player or the host.
  std::optional<PlayerCorrection> relocate(PlayerId id, PlayerPose pose,
                                           std::optional<std::int32_t> exit = std::nullopt);
  // The correction to send a guest whose states keep being refused (back to its newest valid pose,
  // as a new epoch); none while it has no pose or has not been refused enough in a row.
  std::optional<PlayerCorrection> correctionDue(PlayerId id);

  // Guests not heard from for `limit` (their link died without a word).
  [[nodiscard]] std::vector<PlayerId> silent(net::Millis now, net::Millis limit) const;
  void heard(PlayerId id, net::Millis now);

private:
  PlayerRecord* record(PlayerId id);

  AuthorityRules rules_;
  std::vector<PlayerRecord> players_;
  std::string worldMap_;
  std::uint32_t revision_ = 1;
};

// A remote player's transform as a viewer draws it (#354): the states arrive with the sender's
// sequence and clock; the track keeps the newest few, drops late or repeated ones, and plays them
// back a little behind the newest so the motion between them is smooth whatever the network's
// jitter. Past the newest it holds a short way and then stops; a jump farther than a player can
// move (a correction, a respawn) is drawn as a jump, not a slide.
struct TrackedPose {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
  // Radians.
  float yaw = 0.0F;
  Motion motion = Motion::Idle;
};

struct PoseTrackRules {
  // How far behind the newest state the track plays (about two 20 Hz states).
  net::Millis delay{100};
  // How far past the newest state it extrapolates before it stops there.
  net::Millis extrapolation{100};
  // A step longer than this between two states is a jump.
  float snapDistance = 800.0F;
  // A state this much later than the quickest delivery seen means the sender stopped its clock (a
  // pause) or the route got slower for good: the track takes the new delay at once instead of
  // creeping toward it.
  net::Millis resync{500};
  std::size_t capacity = 32;
};

class PoseTrack {
public:
  explicit PoseTrack(PoseTrackRules rules = {}) : rules_{rules} {}

  // A state the sender stamped `sequence` at `senderTime` (its own clock), received `now`. False
  // for a state not newer than the newest one kept. A different map starts the track over.
  bool push(std::uint32_t sequence, net::Millis senderTime, const PlayerPose& pose,
            net::Millis now);
  // The pose to draw at `now` (local clock); none before the first state.
  [[nodiscard]] std::optional<TrackedPose> sample(net::Millis now) const;
  [[nodiscard]] const std::string& mapId() const { return mapId_; }
  [[nodiscard]] std::optional<std::uint32_t> newest() const;
  void reset();

private:
  struct Entry {
    std::uint32_t sequence = 0;
    net::Millis senderTime{0};
    PlayerPose pose;
  };
  PoseTrackRules rules_;
  std::vector<Entry> entries_;
  std::string mapId_;
  // The smallest (receive - sender) clock offset seen: the sender's clock on the local one.
  std::optional<net::Millis> offset_;
};

// Whether sequence `candidate` comes after `current`, with uint32 rollover (half-range rule).
[[nodiscard]] constexpr bool sequenceNewer(std::uint32_t candidate, std::uint32_t current) {
  const std::uint32_t delta = candidate - current;
  return delta != 0 && delta < 0x80000000U;
}

} // namespace dwe::game::coop
