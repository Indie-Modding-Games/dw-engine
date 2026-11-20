# Co-op room browser for four players

Status: **built** (2026-11-20, #352; see [multiplayer](multiplayer.md) §5.9 and §5.10). One difference from this
design: instead of a `peerId` on every message over a single socket, the host opens one socket per seat
(`/v1/host/<code>?token=&taken=`) and the room pairs each guest with one of them; that way each signal only reaches its
pair and the two-player protocol does not change. Original design (2026-11-17): the proposed limit is four players in
all, a host and up to three guests. The list holds active co-op rooms, not dedicated servers; the world is still
simulated on the host and the game traffic travels over WebRTC.

## State before the change and why it changes

The `apps/versus-signal` Worker only pairs one host and one guest through a short-lived code. It does not list rooms.
`RtcPeerLink` stands for a single WebRTC pair, `CoopLink` only assigns remote player 1, and
`FieldScene::addCoopGuest` adds a single actor. Changing the number in the UI or taking three sockets in the Worker
would give a room the game cannot simulate.

## Contracts to build

### Public room

`RoomListing` carries `roomId`, `joinCode`, `name`, `message`, `hostName`, `players`, `capacity`, `createdAt` and
`state`. `players` is between 1 and 4; `state` is `waiting`, `full` or `starting`. The directory keeps no save names,
partner identity or world state. Name and message have length limits, are treated as plain text and have defaults. A
room disappears when it closes or expires; a full room shows disabled.

### Authority and connections

The host keeps sole authority. Each guest needs its own WebRTC link with the host, an id in the range 1..3 and its own
command buffer. The host takes the commands of the three guests within the same authoritative tick and publishes the
state to each link. There is no full mesh between clients: it adds no authority and multiplies connections.

Signaling has to route each offer, answer and candidate by `peerId`; SDP is never broadcast to every member of the
room. Capacity is confirmed atomically when a seat is reserved and freed on leave, disconnect and expiry. Retries use a
`requestId` so no seat is reserved twice.

## Planned files and changes

| Area | Files | Change |
|---|---|---|
| Room directory | `apps/versus-signal/src/worker.ts`, `room.ts`, new `directory.ts`, `wrangler.toml` | Directory Durable Object; registering, updating and freeing rooms and seats; versioned WebSocket endpoint to list and reserve; TTL cleanup. Durable Object binding migration. |
| Client signaling | `packages/engine/net/include/dwe/net/signal.hpp`, `rtc_peer_link.hpp`, `src/signal.cpp`, `src/rtc_peer_link.cpp` | Directory DTO and async client, signaling routed per participant. Keep the WebRTC transport and the existing network errors. |
| Co-op protocol | `packages/game/coop/include/dwe/game/coop/protocol.hpp`, `src/protocol.cpp`, `src/link.cpp` | New versioned protocol; `MaxPlayers=4`, explicit id assignment, identity and sender checked per link, lobby, ready and leave messages. No packet from an id other than the assigned one is taken. |
| Runtime and app | `apps/game/src/coop_connection.*`, `coop_screen.*`, `coop_gameplay_bridge.*`, `coop_commands.*`, `game.cpp` | Ownership of three guest links, list and refresh, room state messages, reserve and leave, a handshake per guest and the state fanned out. The host starts only when every connected participant is ready with a valid profile. |
| Simulation | `packages/game/field/include/dwe/game/field/field.hpp`, `src/field*`; `apps/game/src/field_scene.*`, `play_session.*` | Players indexed by `PlayerId`; safe add and remove of ids 1..3, separate commands, local camera and partners tied to their owner. World systems still run once a tick. |
| UI and text | `content/packs/game/ui/main_menu/{document.rml,logic.lua,structure.rcss}`, `content/packs/game/localization/{es,en}/main_menu.yaml` | Search screen, refresh, room rows, message, seats taken, state and errors, creating a room with a name and message, and the lobby for host and guests. Actions shown by role. |
| Documentation | `docs/architecture/multiplayer.md`, `apps/versus-signal/README.md`, `docs/modules/services/versus-signal.md` | Topology, directory privacy, limits, coordinated deploy and the manual guide. |

## Build order

1. **Directory contracts and lifecycle:** define the DTOs, limits and transitions; make create, reserve, leave and
   expiry atomic. The room is the source of truth for capacity.
2. **Multi-peer signaling:** add `peerId` to the messages and check that each signal reaches only its host/guest pair.
   Keep the current endpoint working on its own during the transition; never mix two versions in one room.
3. **Host protocol and runtime:** go from a single `CoopLink` to an indexed collection; check the assigned id and
   neutralise each player's stale input separately.
4. **Simulation for four players:** allow four `FieldPlayer` on a shared map, each with its owner and partner; keep
   the camera and the local input in each instance.
5. **Browser UI:** list only `waiting` rooms, show `players/capacity` and the message, refresh on demand and
   periodically; reserve a seat before the handshake; show errors and let the player retry without losing the
   selection.
6. **Starting the game:** the host picks a save and decides when to start; each guest brings its partner profile. The
   world, the progress and the map changes still belong to the host.
7. **Deploy:** deploy the Worker with the migration and the new endpoint before shipping the client. The list does not
   work against the old Worker; show a clear incompatibility error.

## Planned checks

- Directory: create, list, update, reserve and free; the limit of 4; two reservations racing for the last seat; TTL
  and ghost rooms.
- Worker: directed signaling, pairs kept apart, closed rooms never listed, bounded answers and structured errors.
- Protocol: incompatible version, unassigned id, malformed packet, duplicates and each guest leaving.
- Runtime and field: three separate command buffers, player ids 0..3, the right partners, one world update per tick and
  one guest disconnecting without touching the others.
- Manual: four instances, the list, a full room, messages, joining and leaving, a disconnect during a reservation, the
  authoritative start and every guest's local save left untouched.

## Risks and explicit limits

- The list makes a room's existence, name, message and occupancy public. Never publish profiles or progress.
- A central directory can become a hot spot; limit refresh rate, answer size and the number of rooms listed, with
  paging or a filter if it grows.
- The capacity of four must not show in the UI until protocol, runtime and simulation are done. The order above avoids
  offering seats that cannot be used.
- Deploying the Worker is a separate step from the local change and needs access to the matching Cloudflare account.
