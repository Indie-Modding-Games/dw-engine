# Online co-op multiplayer (design document)

Status: **proposal** (2026-11-16). None of this is implemented. The game decisions in §6 carry a
recommendation, but are pending approval. The architecture decision is in
[ADR 0024](../adr/0024-co-op-with-authoritative-host.md). To resume the work, start at §10.

## 0. Purpose and scope

**Purpose:** let a second player connect over the internet to another player's game and play together in the same world:
walking, talking to NPCs, advancing the story and fighting.

**In scope:**

- two players online, a host and a guest;
- a direct connection between PCs over WebRTC, with the signaling Worker used only for the first contact;
- the host's game world, with its story;
- the guest with their own player (and their partner, depending on decision 1 of §6).

**Out of scope:**

- versus, which already exists and is something else: a standalone lockstep battle ([versus](../modules/game/versus.md));
- local multiplayer on a single screen, which the project ruled out;
- more than two players (the design does not close it off, but it is not tested);
- dedicated servers and persistent games.

## 1. The idea in one sentence

Both players use **the same player data**. Only **who controls them** changes: the keyboard of the PC where it is
played, or the network. One PC, the **host**, simulates the whole world and is the authority. The other, the **client**, sends its
commands and draws what the host replicates to it. They connect over WebRTC, with the same transport and the same signaling
as versus.

```mermaid
flowchart TB
  subgraph Host["Host PC (authority)"]
    LC["LocalController<br/>keyboard / gamepad"] --> PC["PlayerCommands<br/>one value per player and tick"]
    RC["RemoteController<br/>reads the network"] --> PC
    PC --> SIM["Single simulation<br/>Field with 2 FieldPlayer, story, battle"]
    SIM --> REP["Replicator<br/>20 Hz snapshots + reliable events"]
  end
  subgraph Cliente["Client PC"]
    IN["Local input"] --> ME["My player<br/>own movement"]
    MIR["WorldMirror<br/>interpolates and draws"]
  end
  ME -- "commands and position (unreliable)" --> RC
  REP -- "snapshots (unreliable) + events (reliable)" --> MIR
```

## 2. What the code assumes today

A study of `main` at `aa27246b2`. It is what forces changes before anything is put on the network.

| Topic | Current state | Consequence |
|---|---|---|
| Player in the field | `game::field::Field` creates exactly one player and one partner (`player_`, `partner_`). The input, the click route, `playerFree`, the triggers, `cellSections` and `confirmSection` exist only once. `Body::who` fixes "0 = player, 1 = partner". | The per-player state has to be moved out to `FieldPlayer` (§4.4). A second `Field` will not do, because each `Field` owns the map, the NPCs and the fades. |
| Partner following | `PartnerFollow` and `Field::movePartner` follow the single player. The off-screen rescue uses the renderer's camera and an unsaved random (`FieldScene::gestureRandom_`). | Each partner follows its owner. The off-screen test moves into the simulation (§4.5). |
| Camera | `FieldCameraFollow` follows the player in the drawing, and `FieldScene::followPixel` returns `setCameraRest` to the simulation. | The render feeds the simulation. It has to be cut: one camera per screen, outside the simulation. |
| Input | `input::InputState` is an object with state and queries by name. `game.cpp` builds a `field::FieldInput` each tick (`stick`, `run`, `moveTarget`, `cancelTarget`, `confirm`), which is already a flat value. Dialogs, menus, battle, items and minigames read `InputState` directly (`pressStory`, `pressBattle`, …). | `FieldInput` is the basis of `PlayerCommands`. The rest becomes commands (§4.2). |
| Clock | Fixed tick of 1/60 s (`core::FixedStep`). The field gives "original frames" at 20 fps with a float accumulator; the story, one frame every 3 ticks; battle, its own `FixedStep` at 20 fps. | It works as is on the host. The client does not simulate the world, so it does not need the same clock. |
| Determinism | The field positions are glm floats: they give the same result in one binary, but it is not guaranteed across machines. There are randoms outside `ruleset::Random` (`std::minstd_rand` in effects and the gym, `gestureRandom_`). Battle is deterministic: integers, `checksum()`, "same seed, same battle". | The world is **replicated**, not synchronized in lockstep. The battle checksum remains as a diagnostic tool. |
| World state | `PlaySession`/`GameSession` own everything. `GameSession::saveFile()` gives a deterministic YAML `SaveFile` with about 30 sections, but it is only valid with the field free (`savesWait`/`busyWith`). Running scripts, the transient state of NPCs, battles and fades are not in the save. | The save serves for **joining at a safe point**, not for live replication. |
| Battle | `FighterId` is the index in `fighters`: partner 0, enemies 1 to 3. The enemy AI and the hits target `PartnerId`. Sickness, the bag, thrown items and reviving belong only to fighter 0. | Today there is room for **a single partner** per battle. Two partners force the sides to be redone (§6, decision 3). |
| Network | `net::PeerLink`/`RtcPeerLink` with two channels ("control" reliable and ordered, "inputs" without retransmission). `SignalClient` with 4-letter room codes over the signaling Worker. `ReliableChannel`, `WireWriter`/`WireReader`, the `Identity` handshake with `contentHash` and the message-type byte of the versus protocol. `Lockstep` is only for two sides. | Everything is reused except lockstep, which at most serves for battles. |
| Pauses | Menus pause the simulation according to the local visibility of the interface (`PlaySession::tickSystems`, `paused()`, `screenStops()`). The Start pause freezes everything. Scripts freeze the player, partner, clock and camera (`holdForScript`, `scriptPauseGame`). | In co-op, opening a menu cannot stop the other player's world (§6, decision 5). |
| Who moves an actor | There is no `Player`, `Controller` or `InputSource`. The closest: the autotest and the control socket inject keys into `InputState`; scripts move actors with `driveByScript`/`placeByScript`; battle and the partner have strategy registries (`BattleStrategies`, `PartnerStrategy`). | The controller is a new piece, but it follows the same pattern as the strategies. |

## 3. Principles

1. **A single world, a single authority.** The host simulates everything: map, NPCs, story, partners, battle, clock and
   randoms. The client never decides anything about the world.
2. **The player is data, the controller is the interface.** Both players are the same `FieldPlayer`. What changes
   is the `PlayerController` that gives them commands each tick.
3. **Everything goes through commands.** No game system reads the keyboard. They read `PlayerCommands`, whether they come from the keyboard, the
   network, an autotest or a replay.
4. **The simulation does not depend on the drawing.** The camera, the renderer and the interface belong to each screen. What the
   simulation needs from them is calculated inside the simulation.
5. **Reuse the versus network.** The same transport, the same signaling and the same handshake. Co-op only
   adds its protocol.
6. **Each step is useful without a network.** Phase C0 (§7) improves the single-player game: autotests, replays and
   cleaner input.

## 4. Pieces

### 4.1 `PlayerController`

The interface that gives a player's commands. One implementation per source:

| Implementation | Where it lives | What it does |
|---|---|---|
| `LocalController` | `apps/game` | Reads `InputState` and builds `PlayerCommands`. It replaces the code in `game.cpp` that today builds `FieldInput` and distributes the presses to each screen. |
| `RemoteController` | `game/coop` (host) | Takes the commands that arrive over the network out of a buffer. If a tick's command is missing, it repeats the last movement and does not repeat actions. |
| `ScriptedController` | `apps/game` (autotest) | Gives commands from a route or a replay. It allows testing two players without a network. |

The shape follows the strategies that already exist: a small interface with no game state. The controller does not touch the
simulation; it only produces values.

### 4.2 `PlayerCommands`

A flat value per player and tick, serializable with `WireWriter`:

- `field`: today's `FieldInput` (`stick`, `run`, `moveTarget`, `cancelTarget`, `confirm`).
- `actions`: a short list of discrete actions: advance or choose in a dialog, choose in a menu, battle command
  (`battle::Command`), use or throw an item, minigame buttons (`FishingButtons`, curling…).

The screens that today receive `InputState` (`pressStory`, `pressBattle`, `pressItemKeeper`, `pressPartnerMenu`,
`pressCurling`, `pressWorldMap`, `pressDestinationChoice`) will receive the actions of the player who is in charge on that
screen. The RmlUi interface keeps emitting `ui::UiCommand`. `LocalController` translates them into actions, instead of
calling `PlaySession::handle` directly.

### 4.3 `PlayerRegistry`

Lives in `GameSession`. One record per player:

- `id` (0 = host, 1 = guest), name and whether it is local or remote.
- its `PlayerController`.
- its partner: the host's is the `Partner` of `SessionState`. The guest's, according to decision 1 of §6.
- its connection state (connected, waiting for a safe point, disconnected).

The maximum size comes from the rules (`max_players: 2` in the first version), not from a constant in the code.

### 4.4 `FieldPlayer`

Everything that is unique in `Field` today and belongs to a player moves to a `FieldPlayer`:

- its entity and that of its partner (the `PlayerTag`/`PartnerTag` tags carry the player id);
- its `FieldInput`, its click route and target, `freeTick`, trail and last trigger;
- its `cellSections` and `confirmSection`;
- its `PartnerFollow` and its off-screen test.

These **stay with the world**: the map, the NPCs, the fades, the map change, the script pauses
(`holdForScript`, `holdForCare`, `holdForItem`) and the `StoryLock` exit filter. `Body::who` goes from "0/1" to
`{player, role}`.

The story scripts that talk about "the player" use **the player who triggered the event** (the one who stepped on the
tile or talked to the NPC). `StoryAdapter` receives that id with each tile section.

### 4.5 Camera and view

- Each screen has its own `FieldCameraFollow`, which follows **its** player. The host sees theirs; the client, theirs.
- The simulation keeps a "view rest" of its own per player (what today enters through `setCameraRest`), calculated with
  the same rules as the camera, but without going through the renderer.
- The partner's off-screen test uses that view rest, not the drawn camera.
- The script camera (`Field::scriptCamera`) and `holdCamera` in battle apply to all screens.

### 4.6 The `game/coop` package

A new package, next to `game/versus` and with the same shape:

| Piece | Side | What it does |
|---|---|---|
| `protocol` | both | Message types (one byte, in a range of their own, not stepping on those of versus or lockstep), handshake with `Identity` (protocol, game version and `contentHash`) and codecs with `WireWriter`/`WireReader`. |
| `CoopLink` | both | Handshake, heartbeat, timeouts and exit reasons, like `VersusLink`. |
| `CoopHost` + `Replicator` | host | Receives commands and gives them to `RemoteController`. Builds the snapshots and the events. |
| `CoopClient` + `WorldMirror` | client | Sends commands. Applies snapshots with interpolation and events in order. Draws with the game's same scenes, in mirror mode (with no simulation of its own of the world). |

The client's session is a `PlaySession` in **mirror mode**: it loads the maps and the models, and draws the interface,
but its systems do not advance; they only apply what the host sends.

### 4.7 Interfaces (sketch)

A sketch to fix the shape; the final names come out of the C0 review. It follows the code's conventions:
`std::expected`, no exceptions, designated initializers.

```cpp
namespace dwe::game {

using PlayerId = std::uint8_t;  // 0 = host, 1 = guest

struct PlayerAction {
  enum class Kind : std::uint8_t { TextAdvance, TextChoice, MenuChoice, BattleCommand, UseItem, ThrowItem, Minigame };
  Kind kind;
  std::int32_t value = 0;  // option, battle command, item…
};

struct PlayerCommands {
  std::uint32_t tick = 0;
  field::FieldInput field;
  std::vector<PlayerAction> actions;  // few per tick
};

class PlayerController {
public:
  virtual ~PlayerController() = default;
  // The player's commands for this tick. Never blocks: if there is nothing, an empty PlayerCommands.
  [[nodiscard]] virtual PlayerCommands next(std::uint32_t tick) = 0;
};

struct PlayerSlot {
  PlayerId id;
  std::string name;
  bool remote = false;
  std::unique_ptr<PlayerController> controller;
  enum class Link : std::uint8_t { Local, Joining, Connected, Dropped } link = Link::Local;
};

}  // namespace dwe::game

namespace dwe::game::field {

struct FieldPlayer {
  PlayerId id;
  entt::entity body;
  entt::entity partner;
  FieldInput input;
  PartnerFollow follow;
  // click route, freeTick, trail, last trigger, cellSections, confirmSection, view rest…
};

}  // namespace dwe::game::field
```

On the network, each command is encoded with `WireWriter`: the tick (u32), the packed `FieldInput` (stick in two i8,
flags in one byte, optional `moveTarget`) and the actions (u8 count, each one a u8 kind and an i32 value).

## 5. Network

### 5.1 Channels

| Flow | Direction | Channel | Rate | Content |
|---|---|---|---|---|
| Commands | client → host | "inputs" (unreliable) | every tick | `PlayerCommands` of the last 3 ticks (redundancy against losses), with their tick number. |
| Own position | client → host | "inputs" (unreliable) | 20 Hz | Position, heading and animation of the client's player (§5.2). |
| Snapshots | host → client | "inputs" (unreliable) | 20 Hz | Per visible entity: id, position, heading, animation and frame. Delta against the last confirmed snapshot. Plus the game clock and the state of the two partners that the HUD shows. |
| Events | host → client | "control" (reliable, ordered) | when they happen | Map change, dialog opened/advanced/closed, battle start and end, shared menu, game to join, disconnection. |
| Actions | client → host | "control" (reliable, ordered) | when they happen | The `actions` that cannot be lost (battle command, dialog choice); they also go in the commands, and the host discards duplicates per tick. |

Estimate: about 20 entities × ~20 bytes × 20 Hz ≈ **8 KB/s** download, and quite a bit less upload.

### 5.2 Movement authority

In the first version, **each client moves its own player** and sends the host its position.

- It is immediate on the client's screen and there is no need to correct positions when the reply arrives.
- The host checks that the position is possible (maximum speed and collision with the map). If it is not, it corrects it and
  tells the client, which jumps to the corrected position.
- Everything else belongs to the host: triggers, tiles, NPCs, story, partners and battle. The client stepping on an exit
  does not change the map until the host decides.
- Among friends, someone cheating is not a risk that justifies more complexity.

Later it can move to full host authority with prediction and reconciliation on the client, without changing the
rest of the pieces.

### 5.3 Joining a game

1. The host opens a room: a 4-letter code through the signaling Worker, same as versus.
2. The client enters with the code. Handshake with `Identity`: if the game version or the content does not match, it is cut
   off with a clear reason.
3. The host waits for the next **safe point** (field free, `savesWait` with no blocks). Meanwhile, the client sees
   "Waiting for the host…".
4. The host sends its `SaveFile` over the reliable channel, plus the map and the current positions.
5. The client loads the map in mirror mode and appears next to the host's player.
6. The snapshots begin.

Reconnection follows the same path. If the guest leaves, their player and partner disappear at the next safe
point, and the host keeps playing alone.

### 5.4 Signaling, relay and rooms

The versus signaling Worker (`apps/versus-signal`) is used, with a new room type. The open-room cards
(#291 public directory, #292 explicit room state, #293 public games screen, #294 local
online profile and #295 TURN relay) serve co-op just as well, so they are **common ground**. Deploying the Worker or
enabling TURN still needs an OK.

### 5.5 Messages

A type byte at the start of each message, in the range `0x60–0x6F` (versus uses `0x10–0x14` and lockstep
`0x40/0x41`).

| Type | Name | Channel | Direction | Content |
|---|---|---|---|---|
| `0x60` | `Hello` | reliable | both | `Identity` (protocol, game version, `contentHash`), player name. |
| `0x61` | `Welcome` / `Refuse` | reliable | host → client | assigned id, or the reason for refusal (version, content, room full). |
| `0x62` | `JoinState` | reliable | host → client | The host's `SaveFile` at the safe point, map and positions. Split up if large. |
| `0x63` | `Commands` | unreliable | client → host | `PlayerCommands` of the last 3 ticks. |
| `0x64` | `OwnPose` | unreliable | client → host | Position, heading and animation of the client's player. |
| `0x65` | `PoseFix` | reliable | host → client | Corrected position when the client's is not possible. |
| `0x66` | `Snapshot` | unreliable | host → client | Tick, game clock, entities (delta against the last confirmed), HUD of the two partners. |
| `0x67` | `SnapshotAck` | unreliable | client → host | Last snapshot received, for the deltas. |
| `0x68` | `Event` | reliable | host → client | Map change, dialog, battle start and end, shared menu. |
| `0x69` | `Action` | reliable | client → host | An action that cannot be lost (it also goes in `Commands`; the host discards duplicates per tick). |
| `0x6A` | `Leave` | reliable | both | Exit reason. |

The heartbeat and the timeouts are those of `CoopLink` (§4.6), with the same values as versus.
A side busy with a long load (its frame loop stalled) keeps its links alive: the transport sends the
heartbeat on its own while the game does not poll it, for up to 90 s (`LinkTimeouts::busy`), so a load longer
than the 8 s of silence drops nobody, and a game hung past that is still dropped. A guest whose host goes quiet
shows "Waiting for" and its name instead of a frozen screen.

### 5.6 Saves in co-op

Co-op never changes or breaks either player's single-player saves. The rule:

- **Only a game the room starts is a co-op game.** The host's start (its own save, picked for the room) and the
  guest's start (the host's world) join the room. A game loaded from the title while a room is still open stays
  single-player: the guest is not added to it and it sends nothing (`PlayStart::coop`, `PlaySession::coopGame`).
- **The host saves its own game, as a single-player game.** The quick save, the save machine and the autosave write
  the host's slot as always. Nothing of the guest reaches that save: the guest's body and partner live only in the
  field, and no save section reads them. A save the host writes during co-op is the same file it would write alone,
  and it loads in single player like any other.
- **The guest saves nothing.** Its game is a copy of the host's world in memory, with the guest's partner in it. It has
  no save storage (no autosave, quick save or save machine) and the copy names no slot
  (`hostWorldWithGuestPartner`), so nothing can take a slot of the guest's disk as its own. The guest's own save is
  only read, to send its partner. The pause menu's save says so, its quit closes without saving, and a notice says it
  as the game starts (`online.coop.guest_not_saved`, `save.coop_guest`, `save.coop_guest_quit_confirm`).
- **Leaving, dropping or crashing changes nothing on disk.** The guest has written nothing; the host's slot holds its
  last save, which never had any guest state in it.

What the guest earns does not go back to its own game yet (decision 4 of §6, phase C4).

Tests: `packages/session/tests/coop_saves_tests.cpp` (a host's save with a guest in the field is the single-player
save, and loads at the host's place), `apps/game/tests/coop_guest_profile_tests.cpp` (the guest's copy names no slot)
and `apps/game/tests/app_flow_tests.cpp` (a guest's game is never saved to a slot; only the room's games are co-op).

### 5.7 Shared world: authority, players and maps

What the first co-op builds of §5.2 and §5.3 do today (#353, #354, #355, #356, #358). It works for up to 4 players
(`game::coop::MaxPlayers`): player 0 is the host, guests are 1 to 3, and nothing names a fixed second player.

**Who decides what.** The host's `WorldAuthority` (`packages/game/coop`) holds the session: who is in it (the player
registry), the map the group is on and every player's newest valid transform.

| State | Owner | How the others learn it |
|---|---|---|
| Who is in the session, their partner species | host (`WorldAuthority`) | `Roster` on every change |
| The group's map, exits and arrivals | host | `PlayerCorrection` with the exit it came in by |
| A guest's own position | the guest, checked by the host | `PlayerState`, refused or corrected when impossible |
| The host's position, NPCs, story, clock, battles | host | snapshots (§5.1) and events |
| How a remote player is drawn | each game, locally | interpolation (below); it never writes the authority |
| What players change in the world (chests, triggers, doors, ground items) | host | `WorldChange`, `WorldSync`, `GroundItems` (§5.8) |

**Messages.** Three kinds join the table of §5.5 and take the place of its `OwnPose` and `PoseFix` sketch (protocol
version 4):

| Type | Name | Channel | Direction | Content |
|---|---|---|---|---|
| `0x70` | `PlayerState` | unreliable | client → host | Player id, sequence (the guest's tick), the correction epoch it follows, map, position, heading, motion. Every 3 ticks. |
| `0x71` | `PlayerCorrection` | reliable | host → client | Where the host puts that player: map, position, heading, the exit for a map change, and a new epoch. |
| `0x72` | `Roster` | reliable | host → client | Revision and, per player, id, name and partner species. |

**Transform sync.** The guest moves its own player and reports it. The host takes a state only when the link owns that
player, it is newer than the last one taken (late, duplicated and reordered packets are dropped), it carries the
newest correction's epoch (no state from before a correction or a previous map), it is on the group's map, and it
is reachable from the last valid pose at the player's top speed. A body that would stand in a wall on the host's map
is not driven there. After 3 refusals in a row the host sends the player back to its last valid pose, so both sides
agree again. A guest-side jump (a script's `place`) is one of those refusals: it is corrected, not adopted.

**Maps (decision 2 of §6).** The group goes where the host goes. The guest's own exits are held
(`Field::holdExits`); when the host changes map, every guest gets a correction with the new map, the exit and the
host's spawn spot, and arrives there exactly. States from the old map or epoch are refused, and the guest reports
nothing while it travels. Each player still has its own current map: a remote player on another map is neither drawn
nor driven, and is shown again when it arrives.

**Interpolation.** Remote players are drawn from a short buffer of timed states (`PoseTrack`): 100 ms behind the
newest one, blending between the two around that moment. Late states never move a track back, a short gap
extrapolates for at most 100 ms and then holds, a step longer than 800 units (a warp or a correction) is drawn as a
jump, and a sender whose clock paused is picked up again without a slow catch-up. The host draws guests this way,
and guests draw everyone from the host's snapshots, timed on the host's clock. Drawing is presentation only: it
never changes what the authority holds.

**Lifecycle.** A guest joins when its handshake completes: it gets the lowest free id, a body and partner in the
host's field next to the host, a correction to that spot and a new roster. It leaves on `Leave`, a dropped link or a
crash: its record, body, partner and buffers go on both sides, and the roster drops it. When a co-op game ends on
either side (return to title, quit), that side sends `Leave` and closes the room; a guest whose host left goes back
to the title. A player who comes back joins as new, with a clean state.

**Presence.** `WorldAuthority::playersOn(map)` (host) and `CoopGameplayBridge::playersOn(map)` (guest, from the
latest snapshot) say which players stand on a map, for systems that need the others nearby (battles, shared
objects).

Tests: `packages/game/coop/tests/authority_tests.cpp`, `apps/game/tests/coop_world_tests.cpp`,
`apps/game/tests/coop_snapshot_interpolation_tests.cpp`, `apps/game/tests/player_registry_tests.cpp` and the
co-op cases of `packages/game/field/tests/field_tests.cpp`.

### 5.8 Shared world interactions

Every game of the session runs its own copy of the host's world (§6.1), so what one player does to that world has to
reach the others' copies, or a chest one player opened stays full for the rest (#357). The host is the only one that
applies a shared interaction to the world; the others' games ask, and are told the result.

**What is shared and what is local.**

| Interaction | Kept as | Shared or local | How it travels |
|---|---|---|---|
| Story triggers set by any player: events played, NPCs recruited, shops opened, doors and mechanisms opened for good | story triggers | shared | `WorldAsk` → host → `WorldChange` |
| Story variables (counters a script keeps) | story variables | shared | same |
| Chests | their story trigger | shared: one player gets the item, and the chest shows taken (drawer out) in every game at once | a guest's take is an `ItemClaim`, answered by `ClaimAnswer`; then `WorldChange` |
| Items lying on the ground (the map's random items, battle drops) | the host's ground | shared on the host's map: every game there shows the host's ground, and an item goes to one player only | `GroundItems` from the host; a guest's pick-up is an `ItemClaim` |
| Doors and mechanisms a script moves while players watch | live map edits (object flags, closed cells) | shared with the players on that map; on the next visit the map's head script rebuilds them from the shared triggers | `WorldAsk` → host → `WorldChange` |
| Medals, raised species, the player page's key items (rods) | story triggers | local: each player's own | never sent |
| A script's freeze, the talk facing trigger, the next map's name | story variables and a trigger | local: they follow that player's own scripts | never sent |
| The bag, bits, the partner and its care, menus, dialogs on screen, the camera | session systems | local | never sent (battles: §6.1) |

The local facts come from the story rules (`StoryAdapter::playerOwnTrigger` and `playerOwnVariable`): a ruleset that
moves the freeze or the talk facing to another number moves them too.

**How it works.** After every tick each game compares its story with what it last agreed with the session
(`CoopStoryBaseline`): what its own scripts and systems changed is its news. The host's news only needs telling, as
its game is the world. A guest sends its news as a `WorldAsk`; the host's `WorldLedger` drops an ask it took already
(each guest numbers its asks), keeps only the shared part, takes map edits only for the map the session is on,
applies what changes its world, and sends every guest a
`WorldChange` with the next revision, the player whose interaction it was and the new values. A guest applies a change
once (`WorldReplica`: a revision not newer than the newest taken is dropped), and what it applies from the host is
never sent back as its own. Values are absolute (a trigger set, a variable's value), so applying one twice is applying
it once. When two players change the same fact at once the host's order decides, and every game ends up with the
host's value.

**Taking an item.** A chest or an item on the ground could be taken by two players at once, so a guest's take asks
first. When the guest confirms, its item notice stays up (a chest's drawer is already out) and its game sends an
`ItemClaim`: the chest's trigger, or the ground item's slot and id. The host answers each guest's claims in order and
once (a claim sent again is dropped): granted while the chest's trigger is unset or the item still lies there with that
id, and then it sets the trigger or takes the item off its ground and tells everyone as usual. Only on a granted
`ClaimAnswer` does the guest's game put the item in the bag; refused (or with no answer for ten seconds) the guest gets
nothing and the notice says another player took it (`online.coop.item_gone`). The host takes at once, as in single
player, so its take within the same round trip comes first; a take that finds the item gone while its notice was open
gets the same notice. On a map the host is not on, a guest's ground is its own and needs no claim. Single player never
claims.

**Joining late and changing map.** A guest that joins gets the whole shared story (`WorldSync`, in parts that fit a
datagram) and the host's ground; every guest gets them again when the host enters a map. The sync carries the host's
revision, so a change already in it is not applied again.

**Saves (§5.6).** The world is the host's: what a guest changes in it is in the host's next save, like anything the
host does. The guest still saves nothing, and its own save is never written.

**Messages** (protocol version 5, range `0x90–0x9F`):

| Type | Name | Channel | Direction | Content |
|---|---|---|---|---|
| `0x90` | `WorldAsk` | reliable | client → host | The guest's ask number, its map and the edits (trigger, variable, map object, closed cells). |
| `0x91` | `WorldChange` | reliable | host → client | Revision, the player it came from and its ask, the map, the edits. |
| `0x92` | `WorldSync` | reliable | host → client | Revision, part, and a run of triggers (bits) and variables. |
| `0x93` | `GroundItems` | reliable | host → client | The host's map and map load, and each item on the ground: id, slot, item, cell and place. |
| `0x94` | `ItemClaim` | reliable | client → host | The guest's claim number, chest or ground item, the chest's trigger or the item's slot and id, the map. |
| `0x95` | `ClaimAnswer` | reliable | host → client | The claim number and whether it was granted. |

**Limits.** An NPC another player recruited
leaves this game's map on its next load, not at once (its trigger arrives at once, but the NPC on screen follows this
game's own scripts until the map loads again). Only the map's live edits travel; anything else a script does on screen
(a scene, a character's walk) stays in the game that ran it.

Tests: `packages/game/coop/tests/world_state_tests.cpp`, `apps/game/tests/coop_world_state_tests.cpp` (a host and two
guests over encoded messages), the co-op case of `packages/game/field/tests/field_items_tests.cpp` and of
`packages/session/tests/story_adapter_tests.cpp`.
### 5.9 Rooms of four and the sync diagnostics

What #352 and #359 add (protocol version 6, kinds `0xA0`–`0xA2`).

**Rooms.** A co-op room seats the host and up to three guests. The signaling Worker keeps such a room open while the
host waits (`apps/versus-signal`, "Rooms of up to four"): the host holds one link waiting for the next guest, each
guest pairs with one of them, and the room never relays one pair's signals to another. In the game,
`CoopConnection` owns one `CoopLink` per guest, welcomes each with the lowest free id (1 to 3) and lets one guest
leave the lobby without closing the room. Once the game starts the room takes nobody else; a guest that leaves a
started game goes, the others play on, and the room ends with its last guest. Rooms of two (versus, older games)
work as before.

**Lobby.** `CoopScreen` keeps each guest's ready mark and save apart; the host starts once every guest in is ready,
and sends the same save to all of them. The host tells every guest who is in the room:

| Type | Name | Channel | Direction | Content |
|---|---|---|---|---|
| `0xA0` | `Lobby` | reliable | host → client | Capacity (2 to 4) and, per seat taken, id, name and state (connected or ready). |
| `0xA1` | `SyncWatch` | reliable | host → client | Whether the host watches the sync (#359). |
| `0xA2` | `SyncReport` | unreliable | client → host | While watched, twice a second: report sequence, the newest snapshot tick, snapshots lost and late, and where it holds every player. |

The rooms screen shows four rows in every skin: free, taken, the host with its crown, ready marks, and a guest whose
link is still connecting (`online.member.<n>.arriving`).

**Play.** On the host every guest has its own gameplay bridge, controller and body: its commands move its own player
(`GuestFieldIntent`), its states reach the authority under its id, corrections and rosters go only to it, and each
gets the snapshots with its own command acknowledgement. A guest draws everyone from the snapshots and tags each other
player (`coop_hud.tag.<n>.*`). Battles still run with one ally guest, player 1 (#361); the others' battle asks are
refused as busy, so nothing waits on them.

**Sync diagnostics.** A developer tool behind the console (F1), which only opens with the developer tools on, so it
never runs in release builds. `coop_sync("on")` on the host tells every guest to report; `coop_sync("report")` prints
the whole picture once and `coop_sync("off")` stops it. While on, the log says when a player changes map, when
snapshots or commands are lost or come late, and when a guest holds a player somewhere else (another map, or farther
than 600 units) for more than two seconds, and again when it is back in sync. The report lists every player's place
on the host and, per guest, what it holds, how many world ticks it is behind and what it lost.

Tests: `packages/game/coop/tests/sync_watch_tests.cpp`, `apps/game/tests/coop_connection_tests.cpp` (four seats),
`apps/game/tests/coop_sync_tests.cpp` (2, 3 and 4 players headless over lossy links, converging) and the seat tests of
`apps/versus-signal/test`.

## 6. Game decisions (pending)

| # | Question | Recommendation | Alternative |
|---|---|---|---|
| 1 | What does the guest bring? | Their tamer and **their partner** from their own game, as a visitor. | Only a tamer, with no partner: much cheaper, but it contributes less. |
| 2 | Shared map? | Yes: they go **as a group**, and an exit takes both. | Each in their own map: the host would simulate several `Field` at once. |
| 3 | Battle | **Decided (C3, first version):** the partner of **whoever starts the battle** fights; the other helps with items from their own bag and cheers, and sees the battle. One battle at a time. Later, both partners together. See §6.1. | Both partners from the start: the battle's sides have to be redone (enemy AI, hits, commands, HUD). |
| 4 | Progress | The story, the flags, the money and the world belong **to the host**. The guest takes back to their game their partner (stats, age, techniques) and the items they obtain. | Shared progress in both games: it breaks the guest's story. |
| 5 | Pause and menus | In co-op, menus **do not pause** the world. Only the host pauses with Start, and it pauses for both. | Pause for both with any menu: it interrupts the other player's game all the time. |
| 6 | Dialogs and scenes | They freeze both players. **Whoever opened them advances**; the other sees the same box. | Anyone advances: faster, but the other's texts get skipped. |

### 6.1 Decision 3 as built (C3)

The guest runs its own copy of the host's world, so a battle both players share has to run in one place. It runs on
the **host's simulation**:

- **When it is shared (ticket #351).** Only while the other player stands on the same map. The guest hands its
  script's battles to the host only while the host's last snapshot names the guest's map (and no map change is under
  way); anywhere else the battle is the guest's own, solo, exactly as in single player, and nothing waits for the
  host. The host's battles are shown only to a guest on that map that is not in a solo battle of its own. Both sides
  ask the session's presence query (§5.7, `coopPlayerHere`): the host's authority, which also refuses an ask from a
  guest it does not place on its map, and the guest's newest snapshot.

- **Who fights.** The partner of the player whose map script started it (a wild enemy touched, an NPC talked to, a story
  fight). A battle the guest's script asks for goes to the host as `BattleAsk` (map, the enemies' NPC slots with the
  joiners already rolled, the fixed-fight and predefined flags, and the guest partner's fighter: species, stats, HP, MP,
  charge mode, equipped moves, condition, known moves). The host starts it with that visiting partner standing where the
  guest's partner stands in its field (`session::BattleRequest::visitor`); the host's own partner takes no part.
- **One battle at a time.** The shared world runs one battle, so one encounter never makes two. An ask while one
  runs is refused as busy (`BattleRefused`): the guest's script gets "not started", as when a battle cannot start in
  the original, and the guest is pulled into the running battle as its helper. An ask the host cannot take for any
  other reason (it left the map meanwhile, or it is in a screen, scene or script) is refused too, and the guest's game
  then runs that battle itself, solo. Every battle the host starts on the guest's map (its story's, a tournament's) is
  shown to the guest the same way.
- **What the other player sees.** The host sends `BattleBegin`, then a `BattleFrame` every 3 ticks (phase, current
  order, the orders available, whether the bag can be used, the fighters' HP and MP, and every held figure: place,
  heading, clip and its start), then `BattleEnd`. The guest's field holds and draws its own actors where the frames put
  them; the `coop_battle` screen shows the life bars, the starter's orders and the helper's help.
  A helper's camera turns to the battle when it begins and comes back when it ends.
- **What each player can do.** The starter gives the orders (the strip's commands, the finisher) and closes the end
  box. Both can throw an item **from their own bag** (the guest's item leaves its bag; the host's game lends one and
  throws it) and **cheer** (a press of the finisher's charge). On the host, a guest's battle blocks the host's strip and
  finisher button; its bag works. The screen's buttons take the mouse, or the arrows and confirm; the field's extra
  button calls the finisher for the starter and cheers for the helper.
- **Who touched it.** On the host, an enemy that walks into the guest's body is only blocked by it: the guest's own game
  runs that touch and asks for the battle, so the host's script never starts it with the host's partner.
- **Rewards and defeat (decision 4).** Bits and drops go to the host's world. What the battle leaves the fighting
  partner (HP, MP, stat gains, learned moves, care, lives lost, injury, counters) goes back to the guest in `BattleEnd`
  and the guest's game applies it as its own battle's result, defeat included (its lives and its scenes). The NPCs the
  battle took off the map go in `BattleEnd` too, so both fields drop them.
- **Disconnection.** If the guest drops in its own battle, its partner fights on alone on its last order (its own call
  from then on) and nothing is sent back; the host keeps watching and helping and can close the end box. If the host
  drops, the guest's waiting ask becomes a solo battle of its own game, and the battle on screen goes (its script gets
  "not started"); nothing is left holding.

**Both partners fight together** (protocol version 6, [co-op combat](coop-combat.md) §12). When the guest's partner is
on the map where the host's encounter starts and is not busy (the guest sends `BattleOffer` when that changes), it
joins the host's battle as an **ally**, after the enemies: the guest's role is `ally`, it orders its own partner and
throws at it, and its partner's result comes back in the spoils. A partner away or busy is left out and its player
helps as above. Each partner runs or is knocked out on its own; the team loses only when no partner is left.

Limits of this first version: the techniques' effects, the damage numbers and the status marks are drawn only on the
host; the guest sees the fighters move and the life bars. Tournament fighters that are not map NPCs are not drawn on
the guest. A helper's item is drawn thrown from the host's tamer. The enemies jump to the host's places when the battle
starts (each game simulates its own NPCs until then). Drops fall only in the host's world. While the host has its menu
open, the battle waits for both.

## 7. Phases

| Phase | Content | Closing criterion | Time |
|---|---|---|---|
| **C0, no network** | `PlayerCommands` and the three controllers; screens by actions; `FieldPlayer`; camera and off-screen test outside the renderer; `PlayerRegistry`. | The single-player game does not change (story chain green). A test with two scripted players in the same `Field` (only in tests, never as a local mode). | 2–3 weeks |
| **C1, see each other walk** | `game/coop` with handshake, joining at a safe point, snapshots, `WorldMirror`, own movement validated by the host. | Two PCs on the same map see each other walk, each with their partner behind, with simulated losses. | ~2 weeks |
| **C2, interact** | NPCs and tiles per player, replicated dialogs, group map change, menus without pause, disconnection and reconnection. | A stretch of the story played between two PCs. | ~3 weeks |
| **C3, battle** | According to decision 3 (§6.1). **Done, first version:** host-run battles for both players when they share a map (solo otherwise, #351), the other player's view, help and orders, rewards back to the guest, disconnection in battle. Left: both partners in one battle. | A story battle won in co-op. | 2–6 weeks |
| **C4, guest** | The guest's progress back to their game, names over the players, latency indicator, polish. | The guest returns to their game with what they earned. | — |

### 7.1 Tests

| Level | What is tested | How |
|---|---|---|
| Unit | Message codecs (round trip), `RemoteController` buffer (losses, duplicates, disorder), validation of `OwnPose` | doctest in `game/coop` |
| Simulation | Two `FieldPlayer` in the same `Field` with `ScriptedController`: partners that follow their owner, per-player triggers, group map change | doctest in `game/field` and `session` |
| Local network | Host and client in the same process over `net::loopback`, with simulated loss and delay | doctest in `game/coop` |
| Game | Two instances of the game on the same PC (two virtual screens) joined by a room; autotest route in each | new script next to `story-chain.sh` |
| Regression | The single-player game does not change | Full story chain green after each phase |

## 8. Risks

- **Scripts that assume a single player.** Solved by passing the id of the player who triggered the event (§4.4) and
  reviewing the scenes that move the player.
- **Transient state that is not in the save** (NPCs halfway along, running scripts). That is why joining waits for a
  safe point, and the live state travels in the snapshots, not in the save.
- **The render feeds the simulation** (`setCameraRest`, off-screen test). It is cut in C0.
- **Randoms outside `ruleset::Random`.** They do not affect the replica (only the host simulates), but it is a good idea to move them to the
  session's random so that replays are exact.
- **Cost of phase C3** if two partners are chosen in battle.

## 9. Open questions

- The six decisions in §6.
- If the guest brings a partner: how to validate that their game has not been tampered with (probably it is not validated, same
  as in versus, and the `contentHash` is enough).
- What the guest sees during a story scene that only the host triggers (script camera for both, according to
  §4.5, but it remains to be reviewed scene by scene).
- Whether `ScriptedController` fully replaces the autotest's key injection or lives alongside it.

## 10. How to resume

1. Reread this document and [ADR 0024](../adr/0024-co-op-with-authoritative-host.md).
2. Close the decisions in §6 and note them here (change the status from "proposal" to "approved" and the ADR to
   "Accepted").
3. Create the cards on the board, one per phase in §7, starting with C0. That phase does not touch the network, so it can
   start in parallel with the room cards (#291–#294).
4. First step of C0: `PlayerCommands` and `LocalController`. Move to `LocalController` the code of `apps/game/src/game.cpp`
   that today builds `FieldInput` and distributes `InputState` to the screens, without changing the behavior. Then,
   `FieldPlayer` in `packages/game/field`.
5. The starting files: `packages/game/field/{include/dwe/game/field/field.hpp,src/field.cpp}`,
   `apps/game/src/{game.cpp,play_session.cpp,field_scene.cpp,field_camera_follow.hpp}`,
   `packages/session/include/dwe/session/game_session.hpp`, `packages/engine/net/` and `packages/game/versus/` (the network
   that is reused).

## 11. Related documents

- [Controls](controls.md) and [ADR 0014](../adr/0014-gamepads-and-input.md): action map.
- [Saving and loading](save-and-load.md) and [save](save.md): the `SaveFile` used for joining.
- [World and space](world-space.md): field coordinates.
- [net](../modules/engine/net.md), [versus](../modules/game/versus.md) and
  [signaling](../modules/services/versus-signal.md): the network that is reused.
- [field](../modules/game/field.md), [battle](../modules/game/battle.md) and [session](../modules/apps/session.md): the
  modules that change.
- [Co-op combat](coop-combat.md) (#362): every partner on one side, the next step after the first co-op battles.
- [Co-op difficulty](coop-difficulty.md) (#361): enemy scaling and rewards by the number of players.
