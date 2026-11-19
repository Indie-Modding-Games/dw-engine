# Versus signaling server

A Cloudflare Worker with one Durable Object per room that introduces the two players of the online versus
(`docs/modules/game/versus.md`). It is only a go-between while the two games connect: the host creates a room and gets
a four-character code, the guest types it in, and the server relays the WebRTC offer, answer and candidates between
the two until they open their direct connection. Then it closes the room; the match does not go through the server.
Public rooms are also listed in a directory (one more Durable Object) that games read to find a room, and with TURN
on the server hands out relay servers for networks with no direct route. TypeScript with no dependencies.

```
host ──WebSocket /v1/host──►  room K7Q2  ◄──WebSocket /v1/join/K7Q2── guest
 ▲                            (relays)                                  ▲
 └────────────── direct WebRTC connection (data channels) ───────────────┘
```

## Protocol

JSON text messages over the WebSocket. The version is in the path (`/v1/`).

| Who | Message | What it means |
|---|---|---|
| server → host | `{"type":"room","code":"K7Q2"}` | The room is created; the host shows the code. A public room adds `"token"` (the host's key for its state notices) and, with TURN on, `"iceServers"`. |
| server → guest | `{"type":"joined"}` | It has entered the room (with TURN on, plus `"iceServers"`). |
| server → host | `{"type":"peer"}` | The guest has arrived: the host starts the offer. |
| either → server | `{"type":"signal","data":{…}}` | It is relayed as is to the other (`data` is the description `{"description","type"}` or a candidate `{"candidate","mid"}`). |
| either → server | `{"type":"done"}` | It is already connected. When both say it, the server closes the room; if only one says it, it closes the room after 10 s. |
| server → either | `{"type":"error","reason":…}` | And it closes: `no-room` (there is no room with that code), `full` (there are already two), `left` (the other left before connecting), `expired` (5 minutes passed), `limit` (a limit below), `bad-name` (a nickname it does not take). |

- The codes use `23456789ABCDEFGHJKLMNPQRSTUVWXYZ` (no 0, 1, O or I) and are read ignoring case, spaces and hyphens.
  If the code that comes out is already in use, the Worker tries another.
- A room lives at most 5 minutes (a Durable Object alarm); when it closes it wipes its state and the code is free.
- A message of more than 64 KiB, a binary one or one that is not JSON is ignored.
- What is new since the first version are fields and routes an old game never asks for: `/v1/host` and
  `/v1/join/<code>` work for it as before, and it ignores the fields it does not know.

### Routes

| Route | What it does |
|---|---|
| `/v1/host` (WebSocket) | Makes a private room: only its code reaches it. |
| `/v1/host?public=1&name=&icon=&compat=&mode=` (WebSocket) | Makes a public room, listed in the directory with the host's nickname and icon, its compatibility key and its mode (`campaign` or `versus`). |
| `/v1/join/<code>` (WebSocket) | Joins a room by its code. |
| `GET /v1/rooms?compat=<key>` | The public rooms of that compatibility key (all without one) that are not playing, oldest first, 50 at most: `{"rooms":[{"roomId","joinCode","name","message","players","capacity","createdAt","state","phase","hostName","icon","compat","mode","updatedAt"}]}`. Over a WebSocket it is one message, then the server closes. |
| `POST /v1/rooms/<code>/state` | The host's notice of how its public room goes, with `{"token","state"}`; `state` is `ready`, `playing` or `closed`. 204, or `{"error":…}`: `bad-token` (403), `no-room` (404), `bad-state` (400, or 409 for `ready`/`playing` before a guest arrived). Over a WebSocket (the game's client) the two go in the query (`?token=&state=`) and the answer is one message, `{"type":"state"}` or an error. |
| `GET /v1/info` | What the server can do: `{"protocol":"v1","features":["public-rooms","room-state","relay"]}` (`relay` only with TURN on). Over a WebSocket it is one message. A server without this route is a first-version one: no public rooms. |

### Public rooms

The directory is one Durable Object (`DirectoryObject`, `src/directory.ts`) with the listed rooms. A room's `phase`
(the game's `RoomPhase`) goes:

| Phase | Who sets it | In the list |
|---|---|---|
| `open` | The room, when the host makes it | Yes, joinable |
| `connected` | The room, when the guest arrives; it stays once both peers connect and the room closes | Yes, full |
| `ready` | The host's notice, when both picked what they play | Yes, full |
| `playing` | The host's notice, when the game starts | No |
| closed | The host's notice `closed`, or the room failing, expiring or its host leaving before both connected | Removed |

`state` keeps the first version's two values (`waiting` for open, `full` for the rest). An open entry goes with its
room (5 minutes); any other expires 10 minutes after the host's last notice, so a host that vanished leaves no
entry behind. The compatibility key is the game's: its protocol and the first bytes of its content sum, so a game
only sees rooms it can play in.

### Limits

- Per address (`CF-Connecting-IP`), with the Workers Rate Limiting binding (`wrangler.toml`: `HOST_LIMIT`, 6 rooms a
  minute; `LIST_LIMIT`, 5 reads of the list every 10 seconds). It counts per Cloudflare location and is approximate,
  enough for this. Over it: `limit` (429 over HTTP).
- One open public room per address at a time, and 50 open public rooms at most: the directory refuses the room
  (`limit`). The list answers 50 rooms at most.
- The nickname: 1 to 16 letters (with the Latin accents), digits, spaces and `. _ ' -`, no spaces at the ends;
  anything else is `bad-name`. An icon or compatibility key that is not one is dropped. There are no free-form
  titles and no chat: nothing to moderate.
- Messages of 64 KiB at most, rooms that expire, as before.

### TURN relay

For networks with no direct route the server can hand out relay servers from Cloudflare Realtime TURN with
short-lived credentials (2 hours: enough for a session; expired credentials stop relaying). It is **off** until whoever
deploys the server sets four secrets; without them the server behaves exactly as without TURN and nothing is spent.
Each host and each guest asks the TURN service once; the credentials carry the room code as their `customIdentifier`
(to trace abuse) and go with `room` and `joined` as `"iceServers":[{"urls":["turn:…","turns:…"],"username","credential"}]`
(port 53 left out); the game adds them to its own servers. A client's own `iceServers` never reach the other peer.
The game relays over UDP only (its WebRTC library has no TCP or TLS relay), so it uses the `turn:` URLs with
`transport=udp` (or none) and skips the rest.

**It never costs money.** TURN is billed at 0.05 USD per GB past 1000 free GB a month (shared with the SFU). Before
handing out credentials the server reads this month's relayed bytes from the account's GraphQL Analytics
(`callsTurnUsageAdaptiveGroups`, one aggregate `sum { egressBytes }` from the first day of the UTC month to today)
and stops handing them out once they reach `TURN_MONTHLY_CAP_GB` (`wrangler.toml`, 800 by default: margin for the
analytics' delay and the sessions already relaying). The reading is kept about 10 minutes in the directory's storage,
so not every room asks. It fails closed: no analytics token, an error or a timeout (3 s) means no credentials. Past
the cap, or then, rooms connect as without TURN (direct, through STUN) and a game with no route shows its usual error.

Turning it on:

1. In the Cloudflare dashboard of the account the Worker is deployed to: Realtime > TURN Server > Create (any name,
   for example `dwe-versus`). It shows the key's **Turn Token ID** and its **API Token** (shown once: copy it then).
2. Manage Account > Account API Tokens (or My Profile > API Tokens) > Create Token > Custom token: one permission,
   Account > **Account Analytics** > Read, for this account only. Copy the token.
3. Store the four secrets in the Worker, from this folder, with the same account (`CLOUDFLARE_ACCOUNT_ID` and a token
   that may edit Workers):

   ```sh
   cd apps/versus-signal
   npx wrangler secret put TURN_KEY_ID            # the Turn Token ID
   npx wrangler secret put TURN_KEY_API_TOKEN     # the TURN key's API Token
   npx wrangler secret put TURN_ANALYTICS_TOKEN   # the Account Analytics read token
   npx wrangler secret put CLOUDFLARE_ACCOUNT_ID  # the account id (the repository's CLOUDFLARE_ACCOUNT_ID variable)
   ```

   Setting a secret takes effect at once, without a deploy. To change the cap, edit `TURN_MONTHLY_CAP_GB` in
   `wrangler.toml` (it deploys with the Worker).
4. As a backup alert: Manage Account > Billing > Billable Usage (or Notifications > Add > Billing > Usage Based
   Billing) and add a notification for Realtime / TURN usage, so any charge is mailed at once.
5. Check it: `curl https://<server>/v1/info` now lists `relay`, and a room's `room`/`joined` messages carry
   `iceServers`. The game offers "relay only" in the online profile either way; with it on and no relay it says so.

Secrets live in the Worker, not in `wrangler.toml` or the repository: `wrangler deploy` (and so the Signal server
workflow) keeps them, so the workflow needs no change. To turn TURN off, `npx wrangler secret delete TURN_KEY_ID`
(any one of the four is enough). A failing TURN service or analytics query is logged and the rooms go on without relay.

The game's client is in `packages/engine/net/signal.hpp` (protocol), `room_session.hpp` (a room's phase and the host's
notices) and `rtc_peer_link.hpp` (WebRTC with libdatachannel).

## Tests

```sh
cd apps/versus-signal && node --test     # Node 22.18 or later: runs the TypeScript without compiling it
```

`test/room.test.ts` tests the rooms with fake sockets and state; `test/worker.test.ts`, the Worker's routes;
`test/directory.test.ts`, the directory, its limits and expiry; `test/protocol.test.ts`, the whole server over a fake
Workers runtime (public rooms, the host's notices, the limits and TURN with the API faked); `test/turn.test.ts`, the
TURN spending guard (cap, missing token, analytics errors and timeout, the cached reading). The game
has its own fake in-memory server (`packages/engine/net/tests/fake_signal_server.hpp`) with the same rules, with which
two real libdatachannel peers connect in a test with no network.

## Deploying it

It is deployed at `wss://dwe-versus-signal.regamelabs.workers.dev`. `wrangler.toml` holds no account, route or secrets;
the Signal server workflow (`.github/workflows/signal.yml`) deploys it when a change to it lands on main, or by hand
from Actions. By hand, with your own account:

```sh
cd apps/versus-signal
npx wrangler dev            # local: ws://localhost:8787
npx wrangler deploy         # with the account and route that apply
```

and put the address (`wss://…`) in `content/packs/game/versus/network.yaml` (`signal_server`), or give it to the game
with `--versus-server wss://…` to try it. With no address the game says no server is set up.

Deploying adds the two rate limit bindings and the `TURN_MONTHLY_CAP_GB` var of `wrangler.toml` (no cost). TURN stays
off until its four secrets are set (above).
