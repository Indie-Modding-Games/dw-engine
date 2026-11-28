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
// (#352, #359). 8: item trades between players (#360). 9: every guest's partner fights: battles
// started by any guest, every free guest an ally, each fighter's revive countdown in the frames
// and an item's target partner (#351, #361, #362). 10: a game in progress takes players: the
// host's seat key in its welcome and a returning guest's in its hello, the lobby's game under way
// and whether it takes new players, and the refusal of a room closed to them (#352, #356). 11: the
// world's clock, the host's, told to every guest, and who of the players trains (#365). 12: the
// items thrown in a battle in its frames (the guest's screen draws them) and the host's co-op
// difficulty in the lobby (#351, #361, #352). 13: the folder battle mode. 14: a guest's world ask
// says the host revision it had seen (#422), scripted clips, held partners, partner forms and the
// host's map NPCs ride the snapshots (#412, #420, #419), and NPC events are held per player (#417).
// 15: the partners' poop piles are the host's world: its lists of each map's piles, a guest's pile
// left or eaten asked of the host, and its answer to an eat; the guests' battle HUD: each partner's
// moves and current order, the own partner's finisher gauge and Folder plate, each partner's
// status.
inline constexpr std::uint32_t ProtocolVersion = 15;

[[nodiscard]] constexpr bool validPlayer(PlayerId id) {
  return id < MaxPlayers;
}
[[nodiscard]] constexpr bool validGuest(PlayerId id) {
  return id != HostPlayerId && validPlayer(id);
}

// A worst-case 255-byte map id plus 48 entities fits in one 1200-byte unreliable datagram,
// including ChannelPeerLink's one-byte lane tag. JoinState travels reliably and can carry more.
inline constexpr std::size_t MaxSnapshotEntities = 48;
// A map NPC's id in the snapshots is this plus its slot; the players' and partners' stay under it.
inline constexpr std::uint16_t NpcNetworkBase = 0x100;
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
  // A guest coming back to a room it dropped from: the seat key the host's welcome gave it there
  // (0 for a new player). The host gives it its seat and player id back.
  std::uint64_t rejoinKey = 0;

  friend bool operator==(const Hello&, const Hello&) = default;
};

enum class RejectReason : std::uint8_t {
  None,
  ProtocolMismatch,
  VersionMismatch,
  ContentMismatch,
  RoomFull,
  Busy,
  // The game is under way and its host closed it to new players (a returning one still gets in).
  Closed,
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
  // The guest's key to its seat: given back in its hello (Hello::rejoinKey), it returns to the same
  // seat and player id after a drop. Accepted welcomes only; 0 from an older host.
  std::uint64_t seatKey = 0;

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

// A clip a script plays on a character instead of its motion's (a gym course, a scene): the
// original's animation id and how many times a script started it, so the same clip started again
// plays again on the other machines.
struct ActionClip {
  std::uint16_t clip = 0;
  std::uint8_t starts = 0;

  friend bool operator==(const ActionClip&, const ActionClip&) = default;
};

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
  std::optional<ActionClip> action = std::nullopt;

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
// own (docs/architecture/coop-combat.md §3). `partnerDying` while its partner dies or waits to be
// reborn: no co-op battle starts on its map meanwhile.
struct BattleOffer {
  bool available = false;
  BattleFighterProfile fighter;
  bool partnerDying = false;

  friend bool operator==(const BattleOffer&, const BattleOffer&) = default;
};

// A battle began on the host: who started it (that player's partner fights), the guest's ask it
// answers (0 when the host started it), the map, the NPC slot of each enemy, each fighter's
// species (the partner's first, then the enemies', then the allies'), the player of each ally and
// whether it plays the Folder battle system, whose strip the guests label with stances. `moves`
// holds each fighter's equipped move ids in the same order ("" for an empty place, all empty for
// an enemy), which name the partners' orders on the guests' HUD; none from an older host.
using BattleMoveIds = std::array<std::string, 4>;
struct BattleBegin {
  std::uint32_t battle = 0;
  PlayerId starter = HostPlayerId;
  std::uint32_t ask = 0;
  std::string mapId;
  std::vector<std::int16_t> enemyNpcs;
  std::vector<std::string> species;
  bool fixedFight = false;
  std::vector<PlayerId> allies;
  bool folder = false;
  std::vector<BattleMoveIds> moves{};

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

// A fighter's bars: HP and MP, their maxima, whether it is down and, for a partner down, the
// whole seconds its revive countdown shows (0: none shown). A partner's current order, as its ally
// row names it: in the Classic battle `order` is its command's number (0: none); in a Folder
// battle its active order's move place plus one (0: none, the row names `stance`, the
// game::battle::Stance it moves by). `status` is the game::battle::StatusEffect its row shows, and
// `hpBarShown` whether its HP bar shows (Battle::hpBarShown: an enemy's only a while after a hit).
struct BattleFighterState {
  std::int32_t hp = 0;
  std::int32_t mp = 0;
  std::int32_t maxHp = 0;
  std::int32_t maxMp = 0;
  bool down = false;
  std::uint8_t reviveSeconds = 0;
  std::uint8_t order = 0;
  std::uint8_t stance = 0;
  std::uint8_t status = 0;
  bool hpBarShown = true;

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

enum class BattleThrowStage : std::uint8_t { Hand, Flight, Landed };

struct BattleThrow {
  std::uint16_t serial = 0;
  PlayerId thrower = HostPlayerId;
  std::string item;
  BattleThrowStage stage = BattleThrowStage::Hand;
  std::int32_t x = 0;
  std::int32_t z = 0;
  std::int32_t height = 0;
  std::uint8_t target = 0;

  friend bool operator==(const BattleThrow&, const BattleThrow&) = default;
};
inline constexpr std::size_t MaxBattleThrows = MaxPlayers;

// The Folder battle's orders and plate (docs/architecture/folder-battle.md, game::battle's
// folder/state.hpp): an order's move place among the four and the fighter it aims at (none: its
// move needs none).
inline constexpr std::size_t MaxBattleQueue = 2;
inline constexpr std::uint8_t LastOrderPhase = 5;
inline constexpr std::uint8_t LastOrderEnd = 4;
inline constexpr std::uint8_t LastOrderProblem = 7;
struct BattleOrderShown {
  std::uint8_t slot = 0;
  std::optional<std::uint8_t> target{};

  friend bool operator==(const BattleOrderShown&, const BattleOrderShown&) = default;
};

// An order that ended other than done (game::battle::OrderNotice): its move place, how and why it
// ended (OrderEnd, OrderProblem) and the move place of the order that replaced it.
struct BattleOrderNotice {
  std::uint8_t slot = 0;
  std::uint8_t end = 0;
  std::uint8_t problem = 0;
  std::optional<std::uint8_t> by{};

  friend bool operator==(const BattleOrderNotice&, const BattleOrderNotice&) = default;
};

// The Folder plate of a frame's partner (game::battle::FolderFighter as the HUD reads it): its
// active order, that order's phase (OrderPhase) with the phase's progress in percent and whether
// its technique committed, the waiting orders, the fighter the next ones aim at, the stance it
// moves by now, its defense bar and the bar's capacity, the seconds its break lasts (0: not
// broken) and the notice of an order that ended.
struct BattleFolderPlate {
  std::optional<BattleOrderShown> active{};
  std::uint8_t phase = 0;
  std::uint8_t phasePercent = 0;
  bool committed = false;
  std::vector<BattleOrderShown> queue{};
  std::optional<std::uint8_t> target{};
  std::uint8_t stance = 0;
  std::uint16_t bar = 0;
  std::uint16_t barMax = 0;
  std::uint8_t brokenSeconds = 0;
  std::optional<BattleOrderNotice> notice{};

  friend bool operator==(const BattleFolderPlate&, const BattleFolderPlate&) = default;
};

// One battle frame as the host shows it (20 a second, unreliable: a lost one is followed by the
// next). `command` is the current command of the partner the guest commands (the fighting one when
// it has none in the battle) and `menu` a bit per command the strip offers it now (bit n: battle
// command n); `bagAvailable`, the finisher gauge's progress and goal (0 without a finisher) and
// the Folder plate (in a Folder battle) are that partner's too; `finisherCharging` is the battle's.
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
  std::vector<BattleThrow> throws;
  std::uint16_t finisherProgress = 0;
  std::uint16_t finisherGoal = 0;
  std::optional<BattleFolderPlate> folder{};

  friend bool operator==(const BattleFrame&, const BattleFrame&) = default;
};

// What the guest does in a battle: with a partner in it, a command of the strip for that partner
// (`value`, the battle's command number); as its starter the end box's close; in any role an item
// of its own bag thrown (`item`, an item id; `value` the fighter it flies to plus one, 0 for the
// thrower's own partner or the fighting one) and a cheer (a press of the finisher's charge). In a
// Folder battle (docs/architecture/folder-battle.md) its partner takes orders instead of commands:
// an order (`value`, battleOrderValue's slot and target) and a stance (`value`, the Stance).
enum class BattleActionKind : std::uint8_t { Command, ThrowItem, Cheer, CloseEnd, Order, Stance };

// An order's slot and target in one value: (target + 1) x OrderSlots + slot, target 0 for none.
inline constexpr std::int32_t OrderSlots = 4;
inline constexpr std::int32_t LastStance = 3;
inline constexpr std::uint8_t LastBattleStatus = 4;
[[nodiscard]] constexpr std::int32_t battleOrderValue(std::int32_t slot,
                                                      std::optional<std::int32_t> target) {
  return ((target ? *target + 1 : 0) * OrderSlots) + slot;
}

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
// The guest's partner while its own game holds it (a gym course, a care scene, a script): where it
// stands and what it plays. The host draws it there instead of letting it follow the player.
struct HeldPartner {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;
  std::uint16_t yaw = 0;
  Motion motion = Motion::Idle;
  std::optional<ActionClip> action = std::nullopt;

  friend bool operator==(const HeldPartner&, const HeldPartner&) = default;
};

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
  std::optional<ActionClip> action = std::nullopt;
  std::optional<HeldPartner> partner = std::nullopt;

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

// A guest's partner as its own game has it now (#412): it evolved, or a load gave it another form.
// The host takes it as the link's player's, never the one named here, and lists it in the roster.
struct PartnerForm {
  PlayerId player = GuestPlayerId;
  std::string species;

  friend bool operator==(const PartnerForm&, const PartnerForm&) = default;
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
// what is shared. `ask` is the guest's own count, `seen` the newest host revision its game had
// taken when it made the edits, `mapId` the map the guest was on.
struct WorldAsk {
  std::uint32_t ask = 0;
  std::uint32_t seen = 0;
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

// A poop pile on a map of the host's world: its tile and its size (partner_impl.c: WORLD_POOP).
struct PileEntry {
  std::int16_t x = 0;
  std::int16_t y = 0;
  std::int32_t size = 0;

  friend bool operator==(const PileEntry&, const PileEntry&) = default;
};

inline constexpr std::size_t MaxMapPiles = 64;
inline constexpr std::size_t MaxPileMapText = 64;

// The poop piles on one map of the host's world, sent to every guest when they change: every
// game shows the host's piles. A guest that joins, and every guest on a map change, gets a list
// with `restart` first: its piles on every map go, and the lists after it bring back the maps that
// have some. A restart's map may be empty (the host between maps).
struct PoopPiles {
  std::string mapId;
  bool restart = false;
  std::vector<PileEntry> piles;

  friend bool operator==(const PoopPiles&, const PoopPiles&) = default;
};

// What a guest's partner did with a pile: left one (`size` its size) or ate one.
enum class PileAction : std::uint8_t { Leave, Eat };

// A guest's partner left a pile on a tile or ate the one there: the host puts it in its world or
// takes it out. `ask` is the guest's own count; an eat waits for the host's answer (PileAnswer),
// so two partners never eat one pile.
struct PileAsk {
  std::uint32_t ask = 0;
  PileAction action = PileAction::Leave;
  std::string mapId;
  std::int16_t x = 0;
  std::int16_t y = 0;
  std::int32_t size = 0;

  friend bool operator==(const PileAsk&, const PileAsk&) = default;
};

// The host's answer to an eat: granted with the pile's size, or refused (it was gone).
struct PileAnswer {
  std::uint32_t ask = 0;
  bool granted = false;
  std::int32_t size = 0;

  friend bool operator==(const PileAnswer&, const PileAnswer&) = default;
};

// Item trades between two players of the session (#360; docs/architecture/multiplayer.md, "Item
// trades"). A player asks another on its map, the other accepts or refuses, both put items of
// their own bag on the table and confirm; the host referees it (trade.hpp) and has each side swap
// the items at once. Each bag stays its owner's: the host never writes another player's bag, it
// only asks each side to check and then to apply the swap.
inline constexpr std::size_t MaxTradeItems = 8;
inline constexpr std::uint16_t MaxTradeCount = 999;

// Some of one item (its id in data/items) on the table.
struct TradeItem {
  std::string item;
  std::uint16_t count = 0;

  friend bool operator==(const TradeItem&, const TradeItem&) = default;
};

// A player asks `with` for a trade (to the host).
struct TradeAsk {
  PlayerId with = HostPlayerId;

  friend bool operator==(const TradeAsk&, const TradeAsk&) = default;
};

// The asked player's yes or no (to the host).
struct TradeAnswer {
  std::uint32_t trade = 0;
  bool accept = false;

  friend bool operator==(const TradeAnswer&, const TradeAnswer&) = default;
};

// A player's whole offer, replacing its last one (to the host).
struct TradeOffer {
  std::uint32_t trade = 0;
  std::vector<TradeItem> items;

  friend bool operator==(const TradeOffer&, const TradeOffer&) = default;
};

// A player confirms (or takes back its confirmation of) the table as the state `revision` showed
// it (to the host).
struct TradeConfirm {
  std::uint32_t trade = 0;
  std::uint32_t revision = 0;
  bool confirmed = false;

  friend bool operator==(const TradeConfirm&, const TradeConfirm&) = default;
};

// A player calls the trade off, or takes back its ask (to the host).
struct TradeCancel {
  std::uint32_t trade = 0;

  friend bool operator==(const TradeCancel&, const TradeCancel&) = default;
};

// Asked: waiting for the asked player's answer. Open: both put items on the table. Executing: both
// confirmed; the offers are frozen while each side swaps.
enum class TradePhase : std::uint8_t { Asked, Open, Executing };

// The table as the host holds it, to both players on every change. Index 0 is the asker's side,
// 1 the asked player's. Any change of an offer counts `revision` up and clears both confirmations.
struct TradeState {
  std::uint32_t trade = 0;
  std::array<PlayerId, 2> players{HostPlayerId, GuestPlayerId};
  TradePhase phase = TradePhase::Asked;
  std::uint32_t revision = 0;
  std::array<std::vector<TradeItem>, 2> offers;
  std::array<bool, 2> confirmed{false, false};

  friend bool operator==(const TradeState&, const TradeState&) = default;
};

// The host asks one side to check the swap on its bag: what it gives and what it gets.
struct TradePrepare {
  std::uint32_t trade = 0;
  std::vector<TradeItem> give;
  std::vector<TradeItem> get;

  friend bool operator==(const TradePrepare&, const TradePrepare&) = default;
};

// Both sides can swap: apply it (the swap the prepare named).
struct TradeCommit {
  std::uint32_t trade = 0;

  friend bool operator==(const TradeCommit&, const TradeCommit&) = default;
};

// The other side could not apply it or left before it did: undo the swap if it was applied.
struct TradeRevert {
  std::uint32_t trade = 0;

  friend bool operator==(const TradeRevert&, const TradeRevert&) = default;
};

// How a side's bag takes the swap: fine, it no longer holds what it gives, something it gives
// cannot be traded (the original's rule for throwing away), or it has no room for what it gets.
enum class TradeCheck : std::uint8_t { Ok, Missing, Untradeable, NoRoom };
enum class TradeStep : std::uint8_t { Prepared, Applied };

// A side's answer to a prepare or a commit (to the host).
struct TradeReply {
  std::uint32_t trade = 0;
  TradeStep step = TradeStep::Prepared;
  TradeCheck check = TradeCheck::Ok;

  friend bool operator==(const TradeReply&, const TradeReply&) = default;
};

enum class TradeOutcome : std::uint8_t {
  Done,
  Refused,
  Cancelled,
  // A player left the session, or the other dropped.
  Left,
  // The other player is in another trade or a battle.
  Busy,
  // The two players are not on the same map.
  Apart,
  Missing,
  Untradeable,
  NoRoom,
};

// The trade is over, and why; `by` is the player it is about (who refused, cancelled, left or
// failed the check). Both bags are as the outcome says: swapped when done, untouched otherwise.
struct TradeClosed {
  std::uint32_t trade = 0;
  TradeOutcome outcome = TradeOutcome::Done;
  PlayerId by = HostPlayerId;

  friend bool operator==(const TradeClosed&, const TradeClosed&) = default;
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

enum class CoopDifficulty : std::uint8_t { Off, Standard, Hard };

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
  // The game is under way (#356): a guest that picks its save joins it at once. `open`: the host
  // takes new players while it plays (a returning one gets in either way).
  bool playing = false;
  bool open = true;
  CoopDifficulty difficulty = CoopDifficulty::Standard;

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

// The world's clock, the host's (docs/architecture/coop-training.md): its time in frames since year
// 0, whether it runs now and at what speed (the tuning panel's multiplier, in thousandths). The
// host tells every guest about once a second and at once when it stops, starts or jumps; a guest's
// clock follows it.
struct WorldClock {
  std::int64_t elapsedFrames = 0;
  bool running = false;
  std::uint32_t speedThousandths = 1000;

  friend bool operator==(const WorldClock&, const WorldClock&) = default;
};

// A course id longer than this is damaged.
inline constexpr std::size_t MaxCourseText = 32;

// Who trains (#365): a player's course while it runs, empty once it ends. A guest tells the host
// about its own; the host tells every guest about everyone's, its own included.
struct TrainingStatus {
  PlayerId player = HostPlayerId;
  std::string course;

  friend bool operator==(const TrainingStatus&, const TrainingStatus&) = default;
};

// A map id longer than this is damaged.
inline constexpr std::size_t MaxEventMapText = 64;

// The NPC whose event a player's game plays now (#417): its map and the NPC's script id while the
// event runs, an empty map once it ends. A guest tells the host about its own; the host tells every
// guest about everyone's. The other games leave that NPC be meanwhile, so the event starts once.
struct EventStatus {
  PlayerId player = HostPlayerId;
  std::string mapId;
  std::uint16_t npc = 0;

  friend bool operator==(const EventStatus&, const EventStatus&) = default;
};

using Message =
    std::variant<Hello, Welcome, Ready, JoinState, Commands, Snapshot, ViewState, Leave,
                 ProfileChunk, BattleAsk, BattleBegin, BattleRefused, BattleFrame, BattleAction,
                 BattleEnd, PlayerState, PlayerCorrection, Roster, WorldAsk, WorldChange, WorldSync,
                 GroundItems, ItemClaim, ClaimAnswer, BattleOffer, Lobby, SyncWatch, SyncReport,
                 TradeAsk, TradeAnswer, TradeOffer, TradeConfirm, TradeCancel, TradeState,
                 TradePrepare, TradeCommit, TradeRevert, TradeReply, TradeClosed, WorldClock,
                 TrainingStatus, PartnerForm, EventStatus, PoopPiles, PileAsk, PileAnswer>;

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
inline constexpr std::uint8_t PartnerFormKind = 0x73;
// Co-op combat owns 0x80-0x8F.
inline constexpr std::uint8_t BattleOfferKind = 0x80;
// The shared world's interactions (#357) take 0x90-0x9F.
inline constexpr std::uint8_t WorldAskKind = 0x90;
inline constexpr std::uint8_t WorldChangeKind = 0x91;
inline constexpr std::uint8_t WorldSyncKind = 0x92;
inline constexpr std::uint8_t GroundItemsKind = 0x93;
inline constexpr std::uint8_t ItemClaimKind = 0x94;
inline constexpr std::uint8_t ClaimAnswerKind = 0x95;
inline constexpr std::uint8_t PoopPilesKind = 0x96;
inline constexpr std::uint8_t PileAskKind = 0x97;
inline constexpr std::uint8_t PileAnswerKind = 0x98;
// Players and diagnostics (#352, #359): 0xA0-0xAF.
inline constexpr std::uint8_t LobbyKind = 0xA0;
inline constexpr std::uint8_t SyncWatchKind = 0xA1;
inline constexpr std::uint8_t SyncReportKind = 0xA2;
// Item trades (#360): 0xB0-0xBF.
inline constexpr std::uint8_t TradeAskKind = 0xB0;
inline constexpr std::uint8_t TradeAnswerKind = 0xB1;
inline constexpr std::uint8_t TradeOfferKind = 0xB2;
inline constexpr std::uint8_t TradeConfirmKind = 0xB3;
inline constexpr std::uint8_t TradeCancelKind = 0xB4;
inline constexpr std::uint8_t TradeStateKind = 0xB5;
inline constexpr std::uint8_t TradePrepareKind = 0xB6;
inline constexpr std::uint8_t TradeCommitKind = 0xB7;
inline constexpr std::uint8_t TradeRevertKind = 0xB8;
inline constexpr std::uint8_t TradeReplyKind = 0xB9;
inline constexpr std::uint8_t TradeClosedKind = 0xBA;
// Training in a shared world (#365): 0xC0-0xCF.
inline constexpr std::uint8_t WorldClockKind = 0xC0;
inline constexpr std::uint8_t TrainingStatusKind = 0xC1;
inline constexpr std::uint8_t EventStatusKind = 0xC2;

[[nodiscard]] net::Bytes encode(const Message& message);
// Returns empty for a message owned by another protocol. Messages in this range are validated and
// rejected on truncation, trailing bytes, invalid enums, counts, or coordinate bounds.
[[nodiscard]] core::Result<std::optional<Message>> decode(std::span<const std::uint8_t> bytes);

} // namespace dwe::game::coop
