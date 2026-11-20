#pragma once

#include <dwe/core/error.hpp>
#include <dwe/net/datagram.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace dwe::game::coop {

using PlayerId = std::uint8_t;
inline constexpr PlayerId HostPlayerId = 0;
// The first guest's id. Guests take the lowest free id up to MaxPlayers - 1 (the host gives it in
// its welcome); nothing in the protocol is fixed to two players.
inline constexpr PlayerId GuestPlayerId = 1;
inline constexpr std::size_t MaxPlayers = 4;
// 3: co-op battles (#363). 4: the owner's player states, the host's corrections and the roster
// (#353, #354, #355). 5: the shared world's interactions: story facts, live map edits, the
// ground items and the claims on items two players could take (#357). 6: both partners fight
// together: the guest's battle offer, the allies of a battle begin, their figures and the
// spoils' departure (#351, #361). 7: rooms of up to four players' lobby and the sync diagnostics
// (#352, #359).
inline constexpr std::uint32_t ProtocolVersion = 7;

[[nodiscard]] constexpr bool validPlayer(PlayerId id) {
  return id < MaxPlayers;
}
[[nodiscard]] constexpr bool validGuest(PlayerId id) {
  return id != HostPlayerId && validPlayer(id);
}

// A worst-case 255-byte map id plus 48 entities fits in one 1200-byte unreliable datagram,
// including ChannelPeerLink's one-byte lane tag. JoinState travels reliably and can carry more.
inline constexpr std::size_t MaxSnapshotEntities = 48;
inline constexpr std::size_t MaxJoinEntities = 128;
inline constexpr std::int32_t MaxWorldCoordinate = 1'000'000;
inline constexpr std::int32_t MaxViewCoordinate = 1'000'000;
inline constexpr std::size_t MaxProfileChunkBytes = 1000;
inline constexpr std::uint16_t MaxProfileChunks = 128;
inline constexpr std::size_t MaxProfileBytes = MaxProfileChunkBytes * MaxProfileChunks;

struct Identity {
  std::uint32_t protocol = ProtocolVersion;
  std::string gameVersion;
  std::string contentHash;

  friend bool operator==(const Identity&, const Identity&) = default;
};

struct Hello {
  Identity identity;
  std::string playerName;

  friend bool operator==(const Hello&, const Hello&) = default;
};

enum class RejectReason : std::uint8_t {
  None,
  ProtocolMismatch,
  VersionMismatch,
  ContentMismatch,
  RoomFull,
  Busy,
};

struct Welcome {
  Identity identity;
  bool accepted = false;
  PlayerId assignedPlayer = GuestPlayerId;
  RejectReason reason = RejectReason::None;
  std::uint64_t sessionId = 0;
  // The host's name (its online profile), so the guest can show it. Only an accepted welcome
  // carries it, at its end: a rejected one stays as the first protocol had it, so an older guest
  // still reads why it was rejected. Empty from an older host.
  std::string hostName;

  friend bool operator==(const Welcome&, const Welcome&) = default;
};

// The guest's lobby readiness; it never grants authority to select or start the shared world.
struct Ready {
  bool ready = false;

  friend bool operator==(const Ready&, const Ready&) = default;
};

// Input values use signed 15-bit stick coordinates. A click target is in integral world units.
struct FieldCommand {
  std::int16_t stickX = 0;
  std::int16_t stickY = 0;
  bool run = false;
  bool cancelTarget = false;
  bool confirm = false;
  std::optional<std::pair<std::int32_t, std::int32_t>> moveTarget;

  friend bool operator==(const FieldCommand&, const FieldCommand&) = default;
};

struct Commands {
  std::uint32_t sequence = 0;
  FieldCommand field;

  friend bool operator==(const Commands&, const Commands&) = default;
};

enum class Motion : std::uint8_t { Idle, Walk, Run };
enum class EntityRole : std::uint8_t { Player, Partner, Npc };

// Positions are signed integral world units and yaw covers one turn in 16 bits. Quantization is
// part of the protocol so the host and guests do not exchange native floats.
struct EntityState {
  std::uint16_t networkId = 0;
  PlayerId owner = HostPlayerId;
  EntityRole role = EntityRole::Npc;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;
  std::uint16_t yaw = 0;
  Motion motion = Motion::Idle;

  friend bool operator==(const EntityState&, const EntityState&) = default;
};

struct Snapshot {
  std::string mapId;
  std::uint32_t worldTick = 0;
  std::uint32_t acknowledgedCommand = 0;
  std::vector<EntityState> entities;

  friend bool operator==(const Snapshot&, const Snapshot&) = default;
};

struct JoinState {
  std::uint32_t formatVersion = 1;
  std::string mapId;
  std::uint32_t worldTick = 0;
  std::uint32_t saveTransferId = 0;
  std::uint16_t saveChunkCount = 0;
  std::vector<EntityState> entities;

  friend bool operator==(const JoinState&, const JoinState&) = default;
};

struct ViewState {
  PlayerId player = GuestPlayerId;
  std::uint32_t sequence = 0;
  std::string mapId;
  std::int32_t left = 0;
  std::int32_t top = 0;
  std::int32_t right = 0;
  std::int32_t bottom = 0;

  friend bool operator==(const ViewState&, const ViewState&) = default;
};

// A reliable, bounded piece of the guest's full save profile. The app assembles the complete save
// and extracts only the visitor data approved by the session rules; this protocol layer does not
// depend on save, partner, session, or field modules.
enum class ProfilePayloadKind : std::uint8_t { GuestSave, HostSave };

struct ProfileChunk {
  std::uint32_t transferId = 0;
  std::uint16_t index = 0;
  std::uint16_t count = 0;
  ProfilePayloadKind kind = ProfilePayloadKind::GuestSave;
  net::Bytes bytes;

  friend bool operator==(const ProfileChunk&, const ProfileChunk&) = default;
};

// Co-op battles (docs/architecture/multiplayer.md, §6 decision 3; coop-combat.md): one battle at a
// time runs in the shared world, on the host's simulation, and the partner of whoever started it
// fights, with the partners of the players there who are free to join (its allies). A player
// without a partner in it watches and helps with items from their own bag and cheers for the
// finisher.

// A battle's fighters and figures are few: the partner, up to three enemies and the other players'
// partners, and the NPCs the battle holds on the map (those that run off as it opens and walk away
// after it).
inline constexpr std::size_t MaxBattleEnemies = 3;
inline constexpr std::size_t MaxBattleFighters = MaxBattleEnemies + MaxPlayers;
inline constexpr std::size_t MaxBattleFigures = 24;
inline constexpr std::size_t MaxBattleMoves = 64;
inline constexpr std::size_t MaxBattleLeavers = 64;
// A map NPC slot, or this for the fighting partner, or allyFigureSlot for an ally.
inline constexpr std::int16_t PartnerFigureSlot = -1;
[[nodiscard]] constexpr std::int16_t allyFigureSlot(PlayerId owner) {
  return static_cast<std::int16_t>(-2 - static_cast<std::int16_t>(owner));
}
inline constexpr std::int16_t LowestFigureSlot = allyFigureSlot(MaxPlayers - 1);

// The visiting partner as the guest's game has it, for the host to build its fighter from its own
// tables (session::VisitingPartner): its species, the stats in the original's order (HP, MP,
// offense, defense, speed, brain), HP and MP now, charge mode, the move slots of its species' row
// equipped in the four places (-1 for none), the care values the battle reads and what it knows.
struct BattleFighterProfile {
  std::string species;
  std::array<std::int32_t, 6> stats{};
  std::int32_t hp = 0;
  std::int32_t mp = 0;
  std::uint8_t chargeMode = 1;
  std::array<std::int8_t, 4> equipped{-1, -1, -1, -1};
  std::int32_t discipline = 0;
  std::int32_t tiredness = 0;
  bool sick = false;
  bool injured = false;
  std::vector<std::string> knownMoves;

  friend bool operator==(const BattleFighterProfile&, const BattleFighterProfile&) = default;
};

// The guest touched an enemy (its map's script started a battle): it asks the host to run it with
// its partner. `ask` is the guest's own count, which the answer names.
struct BattleAsk {
  std::uint32_t ask = 0;
  std::string mapId;
  // The map NPC slots the enemies are, in their order, joiners included.
  std::vector<std::int16_t> enemyNpcs;
  bool fixedFight = false;
  bool predefined = false;
  BattleFighterProfile fighter;

  friend bool operator==(const BattleAsk&, const BattleAsk&) = default;
};

// Whether the guest's partner is free to join a battle the host starts on its map, and as what
// fighter (sent when either changes): not while the guest is in a menu, a scene or a battle of its
// own (docs/architecture/coop-combat.md §3).
struct BattleOffer {
  bool available = false;
  BattleFighterProfile fighter;

  friend bool operator==(const BattleOffer&, const BattleOffer&) = default;
};

// A battle began on the host: who started it (that player's partner fights), the guest's ask it
// answers (0 when the host started it), the map, the NPC slot of each enemy, each fighter's
// species (the partner's first, then the enemies', then the allies') and the player of each ally.
struct BattleBegin {
  std::uint32_t battle = 0;
  PlayerId starter = HostPlayerId;
  std::uint32_t ask = 0;
  std::string mapId;
  std::vector<std::int16_t> enemyNpcs;
  std::vector<std::string> species;
  bool fixedFight = false;
  std::vector<PlayerId> allies;

  friend bool operator==(const BattleBegin&, const BattleBegin&) = default;
};

// Why the host did not run the battle the guest asked for.
enum class BattleRefusal : std::uint8_t {
  // Another battle runs in the world: the guest watches that one instead.
  Busy,
  // The guest is not on the host's map, or its enemies are not there: the guest's game runs it
  // solo.
  NotHere,
  // The host's game could not start it (a screen, a scene or a script holds it, or the start
  // failed): the guest's game runs it solo.
  Failed,
};

struct BattleRefused {
  std::uint32_t ask = 0;
  BattleRefusal reason = BattleRefusal::Busy;

  friend bool operator==(const BattleRefused&, const BattleRefused&) = default;
};

// What the battle screen shows, as the original's battle steps it.
enum class BattlePhase : std::uint8_t { Intro, Fight, Victory, End, Closing };

// A fighter's bars: HP and MP, their maxima, and whether it is down.
struct BattleFighterState {
  std::int32_t hp = 0;
  std::int32_t mp = 0;
  std::int32_t maxHp = 0;
  std::int32_t maxMp = 0;
  bool down = false;

  friend bool operator==(const BattleFighterState&, const BattleFighterState&) = default;
};

// A body the battle holds on the map (app::FighterFigure): the partner or an NPC slot, where it
// stands (integral world units) and faces, the clip it plays and how often it started, whether it
// stands frozen on its first frame, and its clip's pace in sixteenths.
struct BattleFigure {
  std::int16_t slot = PartnerFigureSlot;
  std::int32_t x = 0;
  std::int32_t z = 0;
  std::uint16_t yaw = 0;
  std::uint16_t clip = 0;
  std::uint32_t starts = 0;
  bool frozen = false;
  std::uint8_t pace = 16;

  friend bool operator==(const BattleFigure&, const BattleFigure&) = default;
};

// One battle frame as the host shows it (20 a second, unreliable: a lost one is followed by the
// next). `command` is the current command of the partner the guest commands (the fighting one when
// it has none in the battle) and `menu` a bit per command the strip offers it now (bit n: battle
// command n); `bagAvailable` and `finisherCharging` are that partner's too.
struct BattleFrame {
  std::uint32_t battle = 0;
  std::uint32_t frame = 0;
  BattlePhase phase = BattlePhase::Intro;
  std::uint8_t command = 0;
  std::uint16_t menu = 0;
  bool bagAvailable = false;
  bool finisherCharging = false;
  std::vector<BattleFighterState> fighters;
  std::vector<BattleFigure> figures;

  friend bool operator==(const BattleFrame&, const BattleFrame&) = default;
};

// What the guest does in a battle: as its starter, a command of the strip (`value`, the battle's
// command number) and the end box's close; as either role, an item of its own bag thrown at the
// fighting partner (`item`, an item id) and a cheer (a press of the finisher's charge).
enum class BattleActionKind : std::uint8_t { Command, ThrowItem, Cheer, CloseEnd };

struct BattleAction {
  std::uint32_t battle = 0;
  BattleActionKind kind = BattleActionKind::Command;
  std::int32_t value = 0;
  std::string item;

  friend bool operator==(const BattleAction&, const BattleAction&) = default;
};

enum class BattleOutcome : std::uint8_t { Victory, Fled, Defeat };

// What the battle left the visiting partner (game::battle::BattleResult's partner side), for the
// guest's game to apply to it: HP and MP at the end, the stat raises, the moves learned, the care
// and time it costs, the lives lost, what its brain opened and what the partner did.
struct BattleSpoils {
  std::int32_t hp = 0;
  std::int32_t mp = 0;
  std::array<std::int32_t, 6> statGains{};
  std::vector<std::string> learnedMoves;
  bool injured = false;
  std::array<std::int32_t, 4> care{}; // happiness, discipline, tiredness, battles
  std::int32_t food = 0;
  std::int32_t livesLost = 0;
  std::int32_t elapsedMinutes = 0;
  bool hourCrossed = false;
  std::int32_t lifetimeHoursLost = 0;
  // How the partner left a co-op battle the team's outcome is not its own
  // (game::battle::Departure: 0 none, 1 ran off, 2 knocked out).
  std::uint8_t departure = 0;
  std::vector<std::uint8_t> commandsLearned;
  std::int32_t mpDiscountLearned = 0;
  std::array<std::int32_t, 4> counters{}; // attacks, big hits, blocks, statuses taken
  bool anyoneHit = false;
  bool partnerHit = false;
  std::int64_t frames = 0;

  friend bool operator==(const BattleSpoils&, const BattleSpoils&) = default;
};

// The battle is over on the host (its end box closed and the beaten enemies gone): how it ended,
// the NPC slots that left the map after it, the bits it gave the host's bag and, when the guest's
// partner fought in it, what it left that partner.
struct BattleEnd {
  std::uint32_t battle = 0;
  BattleOutcome outcome = BattleOutcome::Victory;
  std::vector<std::int16_t> leftMap;
  std::int32_t bits = 0;
  std::optional<BattleSpoils> spoils;

  friend bool operator==(const BattleEnd&, const BattleEnd&) = default;
};

// A player's own transform, sent by the guest that owns it (unreliable, about 20 per second). The
// host keeps the newest valid one per player (authority.hpp) and drops stale ones: `sequence` is
// the sender's tick count (60 per second), so it also times the movement, and `epoch` is the newest
// host correction the guest applied, so states sent before it are told apart.
struct PlayerState {
  PlayerId player = GuestPlayerId;
  std::uint32_t sequence = 0;
  std::uint16_t epoch = 0;
  std::string mapId;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;
  std::uint16_t yaw = 0;
  Motion motion = Motion::Idle;

  friend bool operator==(const PlayerState&, const PlayerState&) = default;
};

// The host puts a guest's player somewhere (it joined, the group changed map, or the host refused
// its state): the guest goes there, on that map, and sends its states with that epoch from then on.
// `exit` is the exit the host's map was entered by, when the guest has to change map; none keeps
// the guest's own map change rules out of it (it enters at the place).
struct PlayerCorrection {
  PlayerId player = GuestPlayerId;
  std::uint16_t epoch = 0;
  std::string mapId;
  std::optional<std::int32_t> exit;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;
  std::uint16_t yaw = 0;

  friend bool operator==(const PlayerCorrection&, const PlayerCorrection&) = default;
};

// Who is in the session, from the host, on every change (reliable). A guest shows exactly these
// players and drops whatever it drew for one that is gone.
struct RosterEntry {
  PlayerId id = HostPlayerId;
  std::string name;
  // The species of the player's partner (empty when it has none).
  std::string partnerSpecies;

  friend bool operator==(const RosterEntry&, const RosterEntry&) = default;
};

struct Roster {
  std::uint32_t revision = 0;
  std::vector<RosterEntry> players;

  friend bool operator==(const Roster&, const Roster&) = default;
};

// The shared world (#357, multiplayer.md "Shared world interactions"). Every game of the session
// runs its own copy of the host's world; what one player's interactions change in it that the
// others must see goes through the host: a guest's game asks (WorldAsk), the host takes what is
// shared and valid, applies it and tells every guest (WorldChange), numbered by its revision so a
// change is never applied twice. A guest that joins or changes map gets the whole state again
// (WorldSync, GroundItems). An item two players could take at once is claimed first (ItemClaim):
// the host gives it to one of them only.
enum class WorldEditKind : std::uint8_t {
  // A story trigger: `key` the trigger, `value` 1 set or 0 clear (chests, recruits, doors,
  // shops, story events: the original keeps them all as triggers).
  Trigger,
  // A story variable: `key` the variable, `value` its value.
  Variable,
  // A live edit of the map in front of the players (a door or mechanism a script moves): object
  // `key` takes flag `value` (set_obj_visibility).
  MapObject,
  // A live edit of the map: the cells from column `key`, row `value`, `width` by `height`, closed.
  MapBlock,
};

// One change of the shared world, as a new value (setting it twice is setting it once).
struct WorldEdit {
  WorldEditKind kind = WorldEditKind::Trigger;
  std::int32_t key = 0;
  std::int32_t value = 0;
  // MapBlock only.
  std::int16_t width = 0;
  std::int16_t height = 0;

  friend bool operator==(const WorldEdit&, const WorldEdit&) = default;
};

// A reliable message holds at most this many edits (each a few bytes, under the 1200-byte limit).
inline constexpr std::size_t MaxWorldEdits = 64;

// A guest's game changed the shared world (its script set a trigger, a door moved): the host takes
// what is shared. `ask` is the guest's own count;
// `mapId` the map the guest was on.
struct WorldAsk {
  std::uint32_t ask = 0;
  std::string mapId;
  std::vector<WorldEdit> edits;

  friend bool operator==(const WorldAsk&, const WorldAsk&) = default;
};

// The shared world changed on the host: its revision (one more than the last change), the player
// whose interaction it was and the ask it answers (0 for the host's own), the map the map edits
// belong to, and the new values.
struct WorldChange {
  std::uint32_t revision = 0;
  PlayerId origin = HostPlayerId;
  std::uint32_t ask = 0;
  std::string mapId;
  std::vector<WorldEdit> edits;

  friend bool operator==(const WorldChange&, const WorldChange&) = default;
};

// The whole of the shared story at the host's `revision`, in `parts` messages: triggers from
// `firstTrigger` and variables from `firstVariable` on, each part the next run of them.
inline constexpr std::size_t MaxSyncTriggers = 4096;
inline constexpr std::size_t MaxSyncVariables = 128;
struct WorldSync {
  std::uint32_t revision = 0;
  std::uint16_t part = 0;
  std::uint16_t parts = 1;
  std::int32_t firstTrigger = 0;
  std::vector<bool> triggers;
  std::int32_t firstVariable = 0;
  std::vector<std::int32_t> variables;

  friend bool operator==(const WorldSync&, const WorldSync&) = default;
};

// An item on the host's ground: its id (new each time an item comes to lie there), its slot, the
// item's id, its cell and its place in integral world units.
struct GroundEntry {
  std::uint32_t id = 0;
  std::uint8_t slot = 0;
  std::string item;
  std::int16_t column = 0;
  std::int16_t row = 0;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;

  friend bool operator==(const GroundEntry&, const GroundEntry&) = default;
};

inline constexpr std::size_t MaxGroundEntries = 16;
inline constexpr std::size_t MaxGroundItemId = 32;

// What lies on the ground of the host's map, sent when it changes and to a guest arriving there:
// on that map a guest's ground is the host's.
struct GroundItems {
  std::string mapId;
  std::uint32_t mapSerial = 0;
  std::vector<GroundEntry> items;

  friend bool operator==(const GroundItems&, const GroundItems&) = default;
};

// What a guest's player takes: a chest's item (`key` the chest's trigger) or an item on the ground
// (`key` its slot, `id` its id in GroundItems).
enum class ClaimKind : std::uint8_t { Chest, GroundItem };

// A guest's player confirmed taking an item another player could take too: its game gives it only
// once the host answers that it was still there (ClaimAnswer), so one item has one taker. `claim`
// is the guest's own count; `mapId` the map the guest stands on.
struct ItemClaim {
  std::uint32_t claim = 0;
  ClaimKind kind = ClaimKind::Chest;
  std::int32_t key = 0;
  std::uint32_t id = 0;
  std::string mapId;

  friend bool operator==(const ItemClaim&, const ItemClaim&) = default;
};

// The host's answer to a claim: granted, the item is the guest's and the world shows it taken
// (WorldChange, GroundItems); refused, someone took it first.
struct ClaimAnswer {
  std::uint32_t claim = 0;
  bool granted = false;

  friend bool operator==(const ClaimAnswer&, const ClaimAnswer&) = default;
};

enum class LeaveReason : std::uint8_t { Quit, Disconnected, SessionEnded };
struct Leave {
  LeaveReason reason = LeaveReason::Quit;

  friend bool operator==(const Leave&, const Leave&) = default;
};

// The room before its game, as the host keeps it (#352): every seat taken, so that each guest
// shows all of them, not only the host and itself. Reliable, on every change.
enum class SeatState : std::uint8_t {
  // In the room, its link open, its save not picked yet.
  Connected,
  // It picked its save and waits for the host to start.
  Ready,
};

struct LobbySeat {
  PlayerId id = HostPlayerId;
  std::string name;
  SeatState state = SeatState::Connected;

  friend bool operator==(const LobbySeat&, const LobbySeat&) = default;
};

struct Lobby {
  // How many players the room seats, host included (2 to MaxPlayers).
  std::uint8_t capacity = 2;
  // The host first, then the guests by id.
  std::vector<LobbySeat> seats;

  friend bool operator==(const Lobby&, const Lobby&) = default;
};

// The sync diagnostics (#359), developer tools only: the host asks every guest to report what it
// holds of the shared world (on) or to stop (off). Reliable.
struct SyncWatch {
  bool on = false;

  friend bool operator==(const SyncWatch&, const SyncWatch&) = default;
};

// One player as a guest holds it: where it draws it now (its own player, where it stands).
struct SyncHeld {
  PlayerId id = HostPlayerId;
  std::string mapId;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;

  friend bool operator==(const SyncHeld&, const SyncHeld&) = default;
};

// A guest's report while the host watches (unreliable, a few per second): what it holds of every
// player, and what it saw of the host's snapshots (the newest world tick, how many it missed and
// how many came late or twice).
struct SyncReport {
  std::uint32_t sequence = 0;
  std::uint32_t snapshotTick = 0;
  std::uint32_t snapshotsLost = 0;
  std::uint32_t snapshotsLate = 0;
  std::vector<SyncHeld> players;

  friend bool operator==(const SyncReport&, const SyncReport&) = default;
};

using Message =
    std::variant<Hello, Welcome, Ready, JoinState, Commands, Snapshot, ViewState, Leave,
                 ProfileChunk, BattleAsk, BattleBegin, BattleRefused, BattleFrame, BattleAction,
                 BattleEnd, PlayerState, PlayerCorrection, Roster, WorldAsk, WorldChange, WorldSync,
                 GroundItems, ItemClaim, ClaimAnswer, BattleOffer, Lobby, SyncWatch,
                 SyncReport>;

inline constexpr std::uint8_t HelloKind = 0x60;
inline constexpr std::uint8_t WelcomeKind = 0x61;
inline constexpr std::uint8_t JoinStateKind = 0x62;
inline constexpr std::uint8_t CommandsKind = 0x63;
inline constexpr std::uint8_t BattleAskKind = 0x64;
inline constexpr std::uint8_t BattleBeginKind = 0x65;
inline constexpr std::uint8_t SnapshotKind = 0x66;
inline constexpr std::uint8_t BattleRefusedKind = 0x67;
inline constexpr std::uint8_t BattleFrameKind = 0x68;
inline constexpr std::uint8_t BattleActionMessageKind = 0x69;
inline constexpr std::uint8_t LeaveKind = 0x6A;
inline constexpr std::uint8_t BattleEndKind = 0x6B;
inline constexpr std::uint8_t ViewStateKind = 0x6C;
inline constexpr std::uint8_t ProfileChunkKind = 0x6D;
inline constexpr std::uint8_t ReadyKind = 0x6E;
// The co-op range goes on past 0x6F (versus uses 0x10-0x14 and lockstep 0x40/0x41).
inline constexpr std::uint8_t PlayerStateKind = 0x70;
inline constexpr std::uint8_t PlayerCorrectionKind = 0x71;
inline constexpr std::uint8_t RosterKind = 0x72;
// Co-op combat owns 0x80-0x8F.
inline constexpr std::uint8_t BattleOfferKind = 0x80;
// The shared world's interactions (#357) take 0x90-0x9F.
inline constexpr std::uint8_t WorldAskKind = 0x90;
inline constexpr std::uint8_t WorldChangeKind = 0x91;
inline constexpr std::uint8_t WorldSyncKind = 0x92;
inline constexpr std::uint8_t GroundItemsKind = 0x93;
inline constexpr std::uint8_t ItemClaimKind = 0x94;
inline constexpr std::uint8_t ClaimAnswerKind = 0x95;
// Players and diagnostics (#352, #359): 0xA0-0xAF.
inline constexpr std::uint8_t LobbyKind = 0xA0;
inline constexpr std::uint8_t SyncWatchKind = 0xA1;
inline constexpr std::uint8_t SyncReportKind = 0xA2;

[[nodiscard]] net::Bytes encode(const Message& message);
// Returns empty for a message owned by another protocol. Messages in this range are validated and
// rejected on truncation, trailing bytes, invalid enums, counts, or coordinate bounds.
[[nodiscard]] core::Result<std::optional<Message>> decode(std::span<const std::uint8_t> bytes);

} // namespace dwe::game::coop
