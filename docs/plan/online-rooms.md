# Online: rooms, matchmaking and profile (proposal)

Design proposal for issue #289 ("Online Mode"): what was agreed before programming, ending with the cards it is split
into. It belongs to phase 5 (what is not from the original) and all of it goes in as an **optional Improvement**:
today's versus, with a room code, stays as it is and is the usual one.

**Progress.** Done: the server's public room directory with its limits (card 1, #291), the explicit room state in
the client (card 2, #292: `net::RoomSession`, used by the versus and the co-op room; the guest sees the host's name),
the rooms screens and the online profile (cards 3 and 4) and TURN with short-lived credentials plus the profile's
"relay only" (card 5, #295; it works once the server has its TURN secrets, see the
[server](../modules/services/versus-signal.md#relay-turn)). Left: spectators (card 6). The "Current status" table
below is how things were before.

## Summary

- The proposal is **public rooms with a list and "Find match"**, not an online campaign mode. A shared campaign would
  need to synchronize the whole world between two machines, something the game does not do deterministically; it stays
  out.
- The signaling server gains **a directory of public rooms** (one more Durable Object) with limits against abuse. The
  match is still peer to peer: the server only introduces the players.
- The **online profile is local**: a nickname, an icon and a record saved in the game's settings, with no accounts and
  no data on the server.
- The **room states** (Connected, Ready, Playing) are modeled as an explicit enum in the client and in the server when
  the directory arrives. Spectator is left for the end, as research.
- It fits in Cloudflare's free plan with plenty of margin (numbers below). TURN, for networks that do not allow a
  direct route, has 1000 GB free per month and goes in its own card.

## Current status

| Piece | What it is like today |
|---|---|
| Matchmaking | By room code: the host creates the series and gets a four-character code; the guest types it ([versus-signal](../modules/services/versus-signal.md)) |
| Server | A Cloudflare Worker with one Durable Object per room (`apps/versus-signal`). It forwards WebRTC's offer, answer and candidates and closes the room when both are connected. A room lives for 5 minutes at most; there is no room list and no stored data |
| Connection | Peer-to-peer WebRTC with libdatachannel ([net](../modules/engine/net.md)): one reliable lane and one without guarantees, Cloudflare STUN, no TURN. The server's certificate is checked (#273) |
| Match | Lockstep: each machine sends only its inputs and both simulate the same thing. Before starting, the protocol version and a sum of the content (`versusContentHash`) are checked: two players with different content do not play |
| Client | `VersusFlow` (`apps/game/src/versus_flow.hpp`) is the flow without UI, with an explicit enum of screens (`View`: menu, set up the room, wait, code, pick match and partners, battle, result, error). `VersusScreen` puts it in the `ui/versus` document |
| Profile | There is none: each player carries the name and the partners of the save they pick. The versus writes nothing to the save, like the original |

The original's versus was for two controllers on one console; here it is for two machines over the internet with the
same rules ([game](../modules/apps/game.md), "Online versus"). That is the base that is not touched.

## The community's proposals

From the issue's comments:

1. **A matchmaking and room screen**, with mockups: a list of open rooms with their host and their state, and a button
   to find a match.
2. **An online player profile** shared by all games ("that keeps the experience no matter the world"), something
   customizable so that not everyone looks the same. Not a priority: first rooms and stable online play.
3. **Separate room states**: Connected, Ready, Playing and Spectator, to add modes without turning the rooms into a
   pile of conditions.
4. **Bad networks** (universities, cafes): WebRTC without TURN does not always connect; coturn was mentioned.

## What is proposed

### Public rooms and "Find match"

- When creating a room, the host chooses whether it is **private** (as today: only with the code) or **public** (it
  appears in the list). Private is what comes selected.
- The "Public rooms" screen lists the **compatible** rooms (same protocol version and same content) that are waiting
  for a guest: the host's nickname and icon, number of battles, stage and how long it has been waiting. It refreshes
  every few seconds and you enter with accept; underneath it is the same `join` with the room's code.
- **"Find match"** enters the oldest compatible public room or, if there is none, creates a public one and waits. There
  is no matchmaking by level: there is no reliable data to do it (the profile is local) and with few players splitting
  them leaves empty rooms.
- Rooms that are already playing do not appear in the list (or appear as "Playing", with no way to enter) as long as
  there are no spectators.

### Room states

An explicit enum in the client (`RoomPhase` or similar, inside a room session class that uses `VersusFlow`) and the
same one in the server's directory:

| State | What it means | Who changes it |
|---|---|---|
| Open | The host waits for a guest (in the list if it is public) | The server when creating the room |
| Connected | Both are in the room and open their WebRTC route | The server when the guest arrives |
| Ready | Each player picked a match and partners (today, the `WaitingLineup` step) | Each client, over the reliable lane; the host notifies the directory |
| Playing | The series is under way | The host notifies the directory; the room leaves the list |
| Closed | It ended, someone left or it expired | The server (or its alarm) |
| Spectator (later) | A third party watches the series | See "Spectators" |

Today the server closes the room as soon as both connect; with the directory, the host would keep a light WebSocket
open (with hibernation, which costs no duration) just to notify "Playing" and "Closed", or would notify with a short
HTTP request on each change. The second option is simpler and is the recommended one.

`VersusFlow::View` is already an explicit enum per screen and today there are no loose flags left over, so nothing is
refactored now: the room's state is separated from the screen when the directory arrives (card 2).

### Online profile

- **Local**: nickname (up to 16 characters from a limited alphabet), an icon from a fixed list and a record of online
  wins and losses, saved in the game's settings, not in the save (the original saves nothing about the versus). It is
  sent to the other player at the start and to the directory in public rooms.
- **No accounts and no data on the server**: an experience saved on the server would need identity, passwords or
  sign-in, data protection and moderation; and even so a record that the client itself sends can be faked. It is not
  worth it for a versus between friends.
- The "experience shared by all worlds" stays as the local record. If one day a profile that cannot be faked is
  needed, the starting point is a key per installation (no account) and results signed by both players; that is out of
  this proposal.

### Spectators

Watching a series means receiving the inputs of both players and simulating the same thing. With peer-to-peer lockstep
there are two ways: the host forwards the inputs to a third peer (more load and more upload for the host) or a relay
on the server (one WebSocket per spectator that receives what the host sends; it costs requests and Durable Object
duration while the series lasts). Neither is small, so it goes as research in the last card.

### Hard networks (TURN)

Cloudflare Realtime TURN gives short-lived credentials from an API with a key that only the server has: the Worker
would request them when creating or joining the room and send them with `room` and `joined`; the game adds them to its
`ice_servers` (already a list of data in `versus/network.yaml`). It costs 0.05 USD per GB that leaves Cloudflare to the
client, with 1000 GB free per month; a series moves very little (inputs of a few bytes, 20 per second), so hundreds of
thousands of series fit in the free tier. A coturn of our own would need a paid and maintained server; it is not
recommended. It goes in its own card because it does not depend on the rooms.

## What the server needs

### Room directory

- One **`Lobby`** Durable Object (a single one; if one day it is not enough, one per compatibility key) that keeps the
  open public rooms: code, host's nickname and icon, compatibility key (protocol version and the first bytes of the
  content sum), number of battles, stage, state and creation time.
- New routes, without touching today's:
  - `GET /v1/rooms?compat=<key>`: the open compatible rooms, 50 at most, oldest first.
  - `/v1/host?public=1&...`: creates the room as today and lists it in the directory.
  - `POST /v1/rooms/<code>/state` (host only, with a token it receives when creating the room): "Playing" or
    "Closed".
  - `GET /v1/info`: what the server can do (`{"features":["public-rooms"]}`), so that the game shows the option only if
    the server has it.
- Today's rooms stay the same: the room notifies the directory when it closes or expires (5 minutes waiting, as now);
  a room that is playing is removed from the list and its entry expires by itself if the host does not notify again.

### Limits against abuse

- **Per IP** (`CF-Connecting-IP`) with the Workers Rate Limiting binding: creating rooms, a few per minute at most;
  asking for the list, once every few seconds. The binding counts per Cloudflare location and is approximate, enough
  for this.
- **One open public room per IP** at a time, which the directory checks.
- **Bounded text**: the nickname, from a fixed alphabet and length, and validated on the server; no free-form room
  titles and no chat, so there is nothing to moderate. The client can hide a nickname that the player flags.
- **Sizes and times** as today: messages of 64 KiB at most, rooms that expire, a list of 50 at most.
- **Privacy**: peer-to-peer WebRTC shows each player's IP to the other; it already happens with the room code. With
  public rooms you play with strangers, so the screen warns about it, and with TURN a "relay only"
  (`iceTransportPolicy: relay`) could be offered to whoever does not want to show their IP.

### Cost on Cloudflare's free plan

The free plan gives 100,000 requests per day to Workers and another 100,000 to Durable Objects (incoming WebSocket
messages count 1 in 20) and 13,000 GB-s of duration per day; rooms with hibernation only count duration while they
process a message.

| What | Requests (approx.) |
|---|---|
| Pairing two players (two WebSockets, about 30 signaling messages, the state notice) | ≈ 10 |
| One player watching the list for 2 minutes, refreshing every 5 s | ≈ 50 (Worker and Durable Object) |
| 500 series per day with 2 minutes of list per player | ≈ 55,000 |

There is room for hundreds of series a day. If it gets close to the limit, first the list's refresh is lengthened or it
is cached for a few seconds in the Worker; the paid plan (5 USD a month) raises the limits a lot.

## How it coexists with the usual two-player versus

- **Nothing changes by default.** "Online versus" keeps "Host a series" and "Join a series". The "Public rooms" and
  "Find match" rows only appear with the **"Public online rooms"** Improvement turned on (off by default, documented
  in `docs/modules/apps/game.md` like the other Improvements) and if the server says it can do them (`/v1/info`).
- **Same match protocol.** Public rooms only change how the players find each other: the `join` by code, the version
  and content check, the lockstep and the versus rules (the original's) are not touched.
- **Compatible signaling protocol.** `/v1/host` and `/v1/join/<code>` stay the same; what is new are routes and
  parameters that an old game does not use. An old server without `/v1/info` is treated as "no public rooms".
- **The profile is optional**: with no nickname, the player name from the chosen save is used, as today.

## Out of scope

- Online campaign or shared world.
- Accounts, global ranking and matchmaking by level.
- Chat and free-form room titles.
- More than two players per series.

## Cards

1. **Public room directory on the server**
   - `Lobby` Durable Object with the open rooms by compatibility key; routes `GET /v1/rooms`, `/v1/host?public=1`,
     `POST /v1/rooms/<code>/state` (with the host's token) and `GET /v1/info`.
   - The room notifies the directory when it closes or expires; the list removes the ones that are playing.
   - Limits: Rate Limiting per IP when creating and when listing, one open public room per IP, validated nickname, 50
     rooms at most.
   - Closing: `apps/versus-signal` tests (`node --test`) for the new routes, expiry and each limit; today's routes do
     not change (their tests keep passing) and the server's README documents the new protocol.
2. **Explicit room state in the client**
   - A room session class with an enum (`RoomPhase`: Open, Connected, Ready, Playing, Closed) that `VersusFlow` uses
     instead of deducing the phase from the screen and the link.
   - The signaling client learns `/v1/info`, the list and the state notice (`signal.hpp`), with the tests' in-memory
     server up to date.
   - Closing: `net` and `game` tests with the transitions (including the error ones) over the in-memory server; the
     versus by code is played the same (the two-machine test keeps passing).
3. **"Public rooms" and "Find match" screen (optional Improvement)**
   - "Public online rooms" option, off by default; the new rows only with it and with a server that can do them.
   - Refreshed list of compatible rooms, enter with accept, create a public or private room, "Find match"; the privacy
     notice.
   - Closing: test of the flow over the simulated network (list, enter, search with no rooms and create) and captures
     of the screen; documented in `docs/modules/apps/game.md` as an Improvement.
4. **Local online profile**
   - Nickname, an icon from a fixed list and a record of wins and losses, in the game's settings (not in the save).
   - It is sent to the other player in the versus welcome and to the directory in public rooms; with no nickname, the
     save's name as today.
   - Closing: test of saving and reading the profile and that it travels in the welcome; the save does not change (test
     that the versus does not write the save).
5. **TURN with short-lived credentials**
   - The Worker asks Cloudflare Realtime TURN for credentials (key as a Wrangler secret) and sends them with `room` and
     `joined`.
   - The game adds them to its `ice_servers`; a "relay only" option for whoever does not want to show their IP.
   - Closing: Worker tests with the TURN API simulated and client tests with the credentials; a manual test between
     two networks that today end in `NoRoute`.
6. **Spectators (research)**
   - Compare forwarding through the host and a relay on the server: load, cost on the free plan and delay.
   - Prototype of whichever comes out better behind a development option.
   - Closing: a design note with the decision and its numbers; a prototype that watches a series from start to finish
     over the simulated network.
