# versus-signal

The signaling server for [online versus](../game/versus.md): a Cloudflare Worker with one Durable Object per
room (`apps/versus-signal`, TypeScript with no dependencies). It only introduces the two players: it forwards the
WebRTC offer, answer and candidates between them until they open their direct connection, and closes the room. The
match does not go through it.

Full protocol, tests and how to deploy it: the
[server README](https://github.com/Indie-Modding-Games/dw-engine/blob/main/apps/versus-signal/README.md).

```text
host ──WebSocket /v1/host──►  room K7Q2  ◄──WebSocket /v1/join/K7Q2── guest
  ▲                           (forwards)                                ▲
  └──────────────── direct WebRTC connection (data channels) ───────────┘
```

## Rooms

- The host opens `/v1/host` and receives `room` with a four-character code (`23456789ABCDEFGHJKLMNPQRSTUVWXYZ`,
  with no 0, 1, O or I). If the code that comes out is in use, the Worker tries another.
  The room is private: only `/v1/host?public=1` puts it in the public list (`GET /v1/rooms`).
- The guest opens `/v1/join/<code>` (without lowercase, spaces or hyphens) and receives `joined`; the host receives
  `peer` and starts the offer.
- `signal` is forwarded as is to the other side. When both send `done` (they are already connected) the room closes;
  if only one sends it, after 10 s.
- Errors, and the room closes: `no-room`, `full`, `left` (the other left before connecting) and `expired` (a room
  lives at most 5 minutes). A code that cannot be one is rejected without looking for a room.

The Durable Object uses the WebSocket hibernation API: a room that is waiting costs nothing while nobody talks.

## Rooms of up to four

Co-op seats up to four players (#352). The host asks with `/v1/host?seats=4` (3 or 4; anything else is the two-seat
room above, unchanged for older games) and gets a token in its `room` message. The room then stays open while the host
waits: each guest pairs with one waiting host socket, the two connect as above and their sockets close once both say
`done`, and the host opens its next socket with `/v1/host/<code>?token=&taken=<n>` for the next guest. A guest more
than `seats - 1` gets `full`; a host that lost a guest says `{"type":"seats","taken":n}` and the seat is free again. The
room checks on its host every 2 minutes, lives at most an hour and closes with its host's last socket. `/v1/info`
lists the `seats` feature.

Once the game starts the host says so in its seat count, `{"type":"seats","taken":n,"playing":true,"open":true}`
(#352, #356). The room then keeps going while the game does (a day at most), its listing shows the game in progress
(phase `playing`, state `waiting` while it has a free seat and takes new players), and when the host's last socket
drops it waits a minute for the host to open a seat again before closing. Hosts that never send `playing` get the room
as before.

## Public rooms

A second Durable Object, the directory, keeps the public rooms. A public host says who it is
(`/v1/host?public=1&name=…&icon=…&compat=…&mode=…`) and receives a token with `room`. The list
(`GET /v1/rooms?compat=…`) only has rooms of the same compatibility key (game version and content), never the token
or the address, and leaves out rooms already playing. Each room's phase:

| Phase | When |
|---|---|
| `open` | the host waits for a guest |
| `connected` | the guest arrived (the room shows as full) |
| `ready` | the host says both picked what they play |
| `playing` | the host says the game started: out of the list |

The host sends `connected`, `ready`, `playing` and `closed` to `POST /v1/rooms/<code>/state` with its token (the
game sends it as a WebSocket that gets one answer), and repeats the room's state every minute while it lasts. A room
that closes before a guest arrives leaves the list with it; one whose players met stays until the host says
`closed`, or 3 minutes after its last notice, so a host that crashed leaves no room behind (#416). A late lobby
notice never takes a game under way back to the lobby. A room of more seats whose host left its lobby with guests in
stays listed as full only: nobody can join a room that is gone. `GET /v1/info` says what the
server can do (`public-rooms`, `room-state`, and `relay` when it has TURN).

Limits: 50 open public rooms at most, one per address, six rooms a minute and five list reads every ten seconds per
address (Workers rate limiting), and a nickname of letters, digits, spaces and `. _ ' -` (16 at most). A host that
goes past them receives `error` with `limit` or `bad-name`; the game says it as «the server refused the room».

## Relay (TURN)

With the secrets `TURN_KEY_ID` and `TURN_KEY_API_TOKEN` (a Cloudflare Realtime TURN key), `TURN_ANALYTICS_TOKEN`
(Account Analytics read) and `CLOUDFLARE_ACCOUNT_ID`, the Worker asks Cloudflare for credentials that last 2 hours,
tagged with the room code (`customIdentifier`), and sends them with `room` and `joined` as `iceServers`. The game adds
them to its ICE servers, so two players with no direct route connect through the relay. Only UDP relays are used: the
game's WebRTC library does not relay over TCP or TLS. The online profile's «relay only» connects through the relay
alone, so the other player never learns this machine's address; without a relay that room fails with `no_relay`.

The relay never costs money: before handing out credentials the Worker reads this month's relayed bytes from the
account's GraphQL Analytics (one aggregate of `callsTurnUsageAdaptiveGroups`, kept about 10 minutes in the directory's
storage) and stops once they reach `TURN_MONTHLY_CAP_GB` (`wrangler.toml`, 800 of the 1000 free GB a month). It fails
closed: a missing secret, an analytics error or a timeout means no credentials. Then, as without TURN or when
Cloudflare does not answer, rooms connect directly (STUN) exactly as before. Setting it up (the dashboard steps, the
`wrangler secret put` commands and a billing notification as a backup alert) is in the server README.

## Status

Deployed on Cloudflare (Workers with Durable Objects) at `wss://dwe-versus-signal.regamelabs.workers.dev`, the address that `content/packs/game/versus/network.yaml` carries (`signal_server`). `wrangler.toml` carries no account, route or
secrets: the Signal server workflow (`.github/workflows/signal.yml`) deploys it when a change to it lands on main, or by
hand from Actions. To work on it, run `npx wrangler dev` in `apps/versus-signal` and start the game with
`--versus-server ws://localhost:8787`; versus and co-op both take that address. The secrets for TURN persist across
deploys, so the workflow does not change for them.

## Tests

`node --test` in `apps/versus-signal` (Node 22.18 or later; in CI, in the launcher job): the rooms with fake sockets
and state (pairing, forwarding, closing, expiry, full room, a code that does not exist, a guest who leaves), the
Worker routes, the directory (listing, state notices, expiry, limits) and TURN with Cloudflare's API faked. The game tests its client against an in-memory server with the same rules
([net](../engine/net.md#tests)).
