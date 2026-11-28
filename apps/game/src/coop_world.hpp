#pragma once

#include <dwe/game/coop/authority.hpp>
#include <dwe/game/coop/protocol.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/net/peer_link.hpp>

#include <glm/vec3.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop_world_state.hpp"

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

// What a game takes from the shared world in a tick (#357): the messages to send, the other
// players' live map edits for its scene (applyCoopMapEdits), the shared facts they changed in its
// story and the ones its own scripts changed (for the NPCs its map head puts on the map:
// CoopNpcPresence).
struct CoopWorldShare {
  std::vector<CoopOutgoing> out;
  std::vector<game::coop::WorldEdit> mapEdits;
  std::vector<CoopFactChange> facts;
  std::vector<CoopFactChange> ownFacts;
};

// The host's side of the shared world (#353, #354, #355), built when its co-op game links a guest
// and dropped with that game. The authority holds who is in the session and each player's newest
// valid transform; this drives the guests' bodies in the host's field from the states their
// owners report (smoothed, as remote motion), puts them where the host goes, and says what to send.
class CoopHostWorld {
public:
  CoopHostWorld(std::string hostName, std::string hostPartner);

  [[nodiscard]] const game::coop::WorldAuthority& authority() const { return authority_; }
  // A guest's handshake completed: its record (none when the session is full), with the id its
  // link gave it when `as` says one (none when that id is taken).
  [[nodiscard]] std::optional<game::coop::PlayerId>
  join(std::string name, std::string partner, net::Millis now,
       std::optional<game::coop::PlayerId> as = {});
  // A guest left, dropped or went quiet: its record and its track go.
  void leave(game::coop::PlayerId id);
  // Player `id`'s partner is now of `species` (#412: the host's own evolved): the roster that
  // lists it goes to every guest with the next tick.
  void setPartner(game::coop::PlayerId id, std::string species);
  // A message received on guest `from`'s link (its PlayerState, PartnerForm, WorldAsk and
  // ItemClaim matter).
  void receive(game::coop::PlayerId from, const game::coop::Message& message, net::Millis now);
  // After the host's field tick: the host's pose and map into the authority, the guests' bodies
  // driven or put in place, and what to send (corrections, the roster when it changed).
  // `mapSerial` grows on every map the scene enters; `arrivalExit` is how it entered this one.
  [[nodiscard]] std::vector<CoopOutgoing> tick(game::field::Field& field, std::uint32_t mapSerial,
                                               std::optional<std::int32_t> arrivalExit,
                                               net::Millis now);
  // After the tick, the world's interactions (#357): the host's own changes for every guest, the
  // guests' asks taken (applied here, then sent to everyone) or left out, the guests' claims on
  // chests and ground items answered (the first claim on an item still there gets it), the whole
  // story to a guest that joined and to everyone when the map changed, and the ground's list
  // whenever it changed. The host's own takes need no claim: its game is the world.
  [[nodiscard]] CoopWorldShare share(CoopWorldView& view);
  [[nodiscard]] const game::coop::WorldLedger& ledger() const { return ledger_; }

private:
  struct OwnerHeld;
  // A guest's player and partner as its own game shows them (#420, #411): the clip its scripts
  // play, and its partner where its game has it, smoothed as the body (`partner`, the sample of its
  // track; back to following the body without).
  void showHeld(game::field::Field& field, game::coop::PlayerId id, const OwnerHeld& held,
                std::optional<game::field::CharacterState> partner = std::nullopt);
  [[nodiscard]] std::optional<game::field::CharacterState>
  partnerAt(game::coop::PlayerId id, std::string_view mapId, net::Millis now) const;
  void forgetTracks(game::coop::PlayerId id);
  // Whether a claim's item is still there; taking it off the world (and telling the guests) if so.
  bool grant(CoopWorldView& view, const game::coop::ItemClaim& claim, CoopWorldShare& share,
             bool& chestsMayChange);

  game::coop::WorldAuthority authority_;
  game::coop::WorldLedger ledger_;
  CoopStoryBaseline facts_;
  CoopGroundTracker ground_;
  std::vector<std::pair<game::coop::PlayerId, game::coop::WorldAsk>> asks_;
  std::vector<std::pair<game::coop::PlayerId, game::coop::ItemClaim>> claims_;
  // Each guest's last claim answered: a claim sent again is answered once.
  std::map<game::coop::PlayerId, std::uint32_t> lastClaim_;
  // Guests sent the whole story since they joined, and the map load it was last sent for.
  std::set<game::coop::PlayerId> synced_;
  std::optional<std::uint32_t> syncSerial_;
  std::map<game::coop::PlayerId, game::coop::PoseTrack> tracks_;
  std::map<game::coop::PlayerId, game::coop::PoseTrack> partnerTracks_;
  // What each guest's newest taken state says its own scripts play on its player and where its
  // game has its partner (#420, #411): the host's field shows them so.
  struct OwnerHeld {
    std::optional<game::coop::ActionClip> action;
    std::optional<game::coop::HeldPartner> partner;
  };
  std::map<game::coop::PlayerId, OwnerHeld> held_;
  // Where partners on a course stand on the host's screen this tick: a guest's on the same machine
  // is drawn beside them (machineSpotBeside).
  std::vector<glm::vec3> takenSpots_;
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
  // on another a map change that arrives at the host's spot (FieldScene::moveCoopSelf). `reload`:
  // the host loaded the map the group is on again (a script's warp reset the room), so this game
  // loads it again too instead of keeping what its scripts left on the old load.
  struct Move {
    std::string map;
    std::optional<std::int32_t> exit;
    assets::SpawnPoint at;
    bool reload = false;
  };
  // Before the guest's field tick, with the field and whether it still goes to the map the last
  // move asked for: the move to make now, if any. Once the player stands where the host said, the
  // reports carry that correction's epoch.
  // A move to another map the host led to waits ZoneLeadTicks first, so the zone notice shows
  // before this game's fade (#356).
  [[nodiscard]] std::optional<Move> apply(const game::field::Field& field, bool moving);
  static constexpr std::uint32_t ZoneLeadTicks = 45;
  // The host led the group to another map: once per change, when its lead starts.
  struct ZoneChange {
    std::string map;
    std::uint16_t epoch = 0;
  };
  [[nodiscard]] std::optional<ZoneChange> takeZoneChange() {
    return std::exchange(zoneChange_, {});
  }
  // Whether this game waits out the lead before following the host to another map.
  [[nodiscard]] bool leadingToZone() const { return zoneLead_.has_value(); }
  // After the field tick `tick` (it counts the guest's ticks): the state to report, every few
  // ticks, while the player stands on the map the host put it on.
  [[nodiscard]] std::optional<game::coop::PlayerState>
  report(const game::field::Field& field, bool moving, std::uint64_t tick) const;
  // After the tick: this guest's partner form for the host while the host's roster lists another
  // (#412: it evolved), at most once a second, so a form lost on the way is told again.
  [[nodiscard]] std::optional<game::coop::PartnerForm> partnerForm(std::string_view species,
                                                                   std::uint64_t tick);
  // The session's players as the host last listed them (none before its first roster).
  [[nodiscard]] const std::optional<game::coop::Roster>& roster() const { return roster_; }
  [[nodiscard]] bool inSession(game::coop::PlayerId id) const;
  // Whether player `id`'s bodies stay on this game's scene: while it is in the session, and after
  // it left while the battle shown still fights with its partner (`fighting`, BattleMirror), as the
  // host keeps them until that battle ends.
  [[nodiscard]] bool keepsBodies(game::coop::PlayerId id,
                                 std::span<const game::coop::PlayerId> fighting) const {
    return inSession(id) || std::ranges::contains(fighting, id);
  }
  // The partner species the roster gives player `id` (empty when none).
  [[nodiscard]] std::string partnerOf(game::coop::PlayerId id) const;
  // After the tick, the world's interactions (#357): what this game changed of the shared world
  // goes to the host as an ask, and the item notice's take as a claim; the host's changes and
  // syncs are applied once each, in order; on the host's map the ground is the host's; the host's
  // answer to the claim goes to the notice.
  [[nodiscard]] CoopWorldShare share(CoopWorldView& view);
  [[nodiscard]] std::uint32_t worldRevision() const { return replica_.revision(); }

private:
  // Sends the notice's take to the host (an item of this game's own ground is granted at once).
  void claim(const CoopWorldView& view, CoopWorldShare& share);

  game::coop::PlayerId self_;
  game::coop::WorldReplica replica_;
  CoopStoryBaseline facts_;
  CoopGroundMirror ground_;
  // The host's changes and syncs, in the order they came.
  std::vector<game::coop::Message> worldIn_;
  std::uint32_t claims_ = 0;
  std::vector<game::coop::ClaimAnswer> answers_;
  // The newest correction applied: states carry it so the host drops ones from before.
  std::uint16_t epoch_ = 0;
  std::optional<game::coop::PlayerCorrection> pending_;
  // The move asked for pending_, until the field arrives.
  bool asked_ = false;
  // The map the newest correction placed this player on.
  std::string placedOn_;
  std::optional<std::uint32_t> zoneLead_;
  std::optional<ZoneChange> zoneChange_;
  // The host's map edits that came while this game was loading their map, for once it stands there.
  std::vector<game::coop::WorldEdit> heldEdits_;
  std::string heldMap_;
  std::optional<game::coop::Roster> roster_;
  // The partner form last told to the host, and when.
  std::string formSent_;
  std::optional<std::uint64_t> formSentAt_;
};

// The presence check of what is shared per map, such as battles (#351): whether `player` stands on
// `field`'s map by the session's presence query (`here`: the host's WorldAuthority::playersOn or a
// guest's CoopGameplayBridge::playersOn, for that map), with the field not between maps.
[[nodiscard]] bool coopPlayerHere(const game::field::Field& field,
                                  std::span<const game::coop::PlayerId> here,
                                  game::coop::PlayerId player);

// The guest's side, against its scene: the host's move of its player, before the field tick.
void applyCoopMove(FieldScene& scene, CoopGuestWorld& guest);

// The other players' doors and mechanisms on this game's map (#357), as its own scripts' edits.
void applyCoopMapEdits(FieldScene& scene, std::span<const game::coop::WorldEdit> edits);

// A guest draws the other players of the session as the host's snapshot smoothed for this frame
// shows them (`world`; none before the first): each player and its partner by id, hidden while
// the guest is not on that map, and gone once the roster no longer lists the player.
// `hostPartner` is the host's partner species for a roster that does not give it.
void presentCoopPlayers(FieldScene& scene, const PresentedCoopWorld* world,
                        const CoopGuestWorld& guest, std::string_view hostPartner,
                        std::span<const game::coop::PlayerId> fighting);

} // namespace dwe::app
