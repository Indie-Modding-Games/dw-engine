# Versus signaling server

A Cloudflare Worker with one Durable Object per room that introduces the two players of the online versus
(`docs/modules/game/versus.md`). It is only a go-between while the two games connect: the host creates a room and gets
a four-character code, the guest types it in, and the server relays the WebRTC offer, answer and candidates between
the two until they open their direct connection. Then it closes the room; the match does not go through the server.
TypeScript with no dependencies.

```
host ──WebSocket /v1/host──►  room K7Q2  ◄──WebSocket /v1/join/K7Q2── guest
 ▲                            (relays)                                  ▲
 └────────────── direct WebRTC connection (data channels) ───────────────┘
```

## Protocol

JSON text messages over the WebSocket. The version is in the path (`/v1/`).

| Who | Message | What it means |
|---|---|---|
| server → host | `{"type":"room","code":"K7Q2"}` | The room is created; the host shows the code. |
| server → guest | `{"type":"joined"}` | It has entered the room. |
| server → host | `{"type":"peer"}` | The guest has arrived: the host starts the offer. |
| either → server | `{"type":"signal","data":{…}}` | It is relayed as is to the other (`data` is the description `{"description","type"}` or a candidate `{"candidate","mid"}`). |
| either → server | `{"type":"done"}` | It is already connected. When both say it, the server closes the room; if only one says it, it closes the room after 10 s. |
| server → either | `{"type":"error","reason":…}` | And it closes: `no-room` (there is no room with that code), `full` (there are already two), `left` (the other left before connecting), `expired` (5 minutes passed). |

- The codes use `23456789ABCDEFGHJKLMNPQRSTUVWXYZ` (no 0, 1, O or I) and are read ignoring case, spaces and hyphens.
  If the code that comes out is already in use, the Worker tries another.
- A room lives at most 5 minutes (a Durable Object alarm); when it closes it wipes its state and the code is free.
- A message of more than 64 KiB, a binary one or one that is not JSON is ignored.

The game's client is in `packages/engine/net/signal.hpp` (protocol) and `rtc_peer_link.hpp` (WebRTC with
libdatachannel).

## Tests

```sh
cd apps/versus-signal && node --test     # Node 22.18 or later: runs the TypeScript without compiling it
```

`test/room.test.ts` tests the rooms with fake sockets and state; `test/worker.test.ts`, the Worker's routes. The game
has its own fake in-memory server (`packages/engine/net/tests/fake_signal_server.hpp`) with the same rules, with which
two real libdatachannel peers connect in a test with no network.

## Deploying it

It is deployed at `wss://dwe-versus-signal.regamelabs.workers.dev`. `wrangler.toml` holds no account, route or secrets;
it is deployed with the account of whoever does it:

```sh
cd apps/versus-signal
npx wrangler dev            # local: ws://localhost:8787
npx wrangler deploy         # with the account and route that apply
```

and put the address (`wss://…`) in `content/packs/game/versus/network.yaml` (`signal_server`), or give it to the game
with `--versus-server wss://…` to try it. With no address the game says no server is set up.

Later: TURN servers for networks with no direct route. The server would give short-lived credentials along with
`room`/`joined`, and the game would add them to `ice_servers`.
