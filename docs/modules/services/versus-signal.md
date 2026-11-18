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

## Status

Deployed on Cloudflare (Workers with Durable Objects) at `wss://dwe-versus-signal.regamelabs.workers.dev`, the address that `content/packs/game/versus/network.yaml` carries (`signal_server`). `wrangler.toml` carries no account, route or
secrets: the Signal server workflow (`.github/workflows/signal.yml`) deploys it when a change to it lands on main, or by
hand from Actions. To work on it, run `npx wrangler dev` in `apps/versus-signal` and start the game with
`--versus-server ws://localhost:8787`; versus and co-op both take that address. Later it will be able to hand out short-lived TURN
credentials for networks with no direct route.

## Tests

`node --test` in `apps/versus-signal` (Node 22.18 or later; in CI, in the launcher job): the rooms with fake sockets
and state (pairing, forwarding, closing, expiry, full room, a code that does not exist, a guest who leaves) and the
Worker routes. The game tests its client against an in-memory server with the same rules
([net](../engine/net.md#tests)).
