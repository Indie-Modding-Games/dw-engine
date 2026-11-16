# net

The network between two machines (`dwe::net`, `base` layer): a link between two peers with a reliable lane and an unreliable
one, WebRTC to connect them over the internet, lockstep for deterministic simulations and the signaling
server client. It depends on `core` and, inside, on libdatachannel; it knows nothing about the game. Online versus uses it
([versus](../game/versus.md)).

## Pieces

| Piece | What it does |
|---|---|
| `PeerLink` | One end of the connection with the other peer, with two lanes (`Lane`): `Reliable` (each message arrives once and in order) and `Unreliable` (it may arrive late, out of order or never). Nothing blocks: `poll(now)` moves it and the messages wait in it. It says its state (`Connecting`, `Open`, `Failed`), why it failed (`PeerFailure`), when it last heard from the other one, the room code and whether the other one has already reached it |
| `RtcPeerLink` | The real link: WebRTC with data channels (libdatachannel). `host(settings, open)` creates a room on the signaling server and offers; `join(settings, code, open)` enters with the code and answers. They exchange their ICE candidates through the server and, when both channels are open, tell the server to close the room and continue peer to peer. Reliable lane: `control` channel (reliable and ordered); unreliable: `inputs` channel (unordered, no retransmits). It sends a `0x00` byte every 250 ms on `inputs` so the other one knows it is still there |
| `RtcSettings` | The signaling server (`ws://` or `wss://`; empty = none), the ICE servers (`stun:`, later `turn:`) and how long the two can take to open a route once they are in the room (30 s) |
| `SignalClient`, `SignalSocket`, `openWebSocket` | The game's side of the signaling protocol ([versus-signal](https://github.com/Indie-Modding-Games/dw-engine/tree/main/apps/versus-signal)): routes `/v1/host` and `/v1/join/<code>`, JSON messages `room`, `peer`, `joined`, `signal`, `done` and `error`. The socket is libdatachannel's WebSocket client; the tests put an in-memory one |
| `normalizedRoomCode` | The room codes: four characters from `23456789ABCDEFGHJKLMNPQRSTUVWXYZ` (without 0, 1, O or I, which get confused). It accepts lowercase, spaces and hyphens; what cannot be a code is not sent |
| `DatagramLink`, `ChannelPeerLink` | An unreliable datagram link (none exceeds `MaxDatagram`, 1200 bytes) and a `PeerLink` on top: a lane byte in front and a `ReliableChannel` for the reliable lane. The test link uses it |
| `LoopbackNetwork` | Two simulated peers in one process for the tests (`link(side)`, room code `TEST`), with its clock (`advance`) and `LinkConditions`: latency, jitter (which reorders), loss and duplicates with a seed, and `cut()` for a peer that leaves without warning |
| `ReliableChannel` | Messages that arrive once and in order over a `DatagramLink`: sequence number, acknowledgment of the last in order plus a mask of the next 32, resend after 120 ms without an ack and an empty ack every 250 ms of silence |
| `Lockstep` | A deterministic simulation on two machines: each side sends only its inputs, scheduled `inputDelay` ticks ahead; a tick is played when both are known. Inputs go on the unreliable lane and each message repeats all the ones the other has not yet confirmed (up to 32) together with the ack of its own, so losing or reordering messages does not stop the fight. After each tick both send a checksum of their state on the reliable lane and the first tick where they do not match is a desync that stops the game. Messages carry the fight number (`run`): those of another fight are ignored |
| `WireWriter` / `WireReader` | Little-endian integers, length-prefixed texts and reading that fails cleanly if the message is short |

Lockstep messages start with `0x40` (inputs) and `0x41` (checksum); those of versus use `0x10`-`0x14`.

## Failures

`PeerFailure` says why a link did not open or closed, and versus shows a message for each one:

| Failure | When |
|---|---|
| `NoServer` | The game has no signaling server configured |
| `ServerUnreachable` | The server could not be reached, or it cut the connection early |
| `WrongCode` | There is no room with that code (or it already expired) |
| `RoomFull` | The room already has its two players |
| `TimedOut` | The room expired waiting for the guest, or the two did not open a route in 30 s |
| `NoRoute` | ICE failed: there is no direct route between the two networks (TURN is needed, which does not exist yet) |
| `Closed` | The connection closed after opening |

A WebRTC "disconnected" notice does not close the link: it may come back on its own, and versus's silence timeout (8 s) decides.

## Why WebRTC and libdatachannel

The first version used direct UDP with the IP address in the join code: over the internet a port had to be opened
on the router. WebRTC crosses most NATs with STUN (`stun:stun.cloudflare.com:3478`, in
`versus/network.yaml`), encrypts with DTLS and gives the two kinds of channel that are needed. libdatachannel (MPL-2.0) is a
small C++ library with a C API, without a browser's media stack, and it brings its own WebSocket client for
signaling. Its C API (`rtc.h`) is used because the project builds without exceptions.

- **TLS with Mbed TLS.** The vcpkg port uses OpenSSL, which does not build on the Linux image (it lacks the kernel
  headers) or in the cross-build to Windows. `tooling/vcpkg-ports/libdatachannel` is the vcpkg port with
  `USE_MBEDTLS=ON`, and `tooling/vcpkg-ports/mbedtls` is the Mbed TLS one with DTLS-SRTP enabled (libdatachannel asks for it).
  Only two small things change in libdatachannel's code, in port patches: some `#include <algorithm>` lines that
  macOS's libc++ asks for and `ws-ca-certificates.patch` (below).
- **Licenses.** libdatachannel and libjuice are MPL-2.0: they can be linked without changing the project's license, and
  their files that change remain MPL-2.0 and public (the port's patches). The third-party notice says where
  their code is and where the changes are. usrsctp (BSD-3), plog (MIT), nlohmann-json (MIT) and Mbed TLS (Apache-2.0) go
  in the same notice. The texts are in `scripts/package/licenses/` and `scripts/package/third_party_notices.py`
  gathers them.

## The signaling server's certificate

The WebSocket client checks the certificate of a `wss://` server: it has to be valid for the server name in the
URL and come from a trusted authority. If not, the socket fails and versus counts it as
`ServerUnreachable`. This way nobody in the middle can impersonate the server and pair the players wrongly.

- **Trusted roots** (`trustedSignalRoots`, `trusted_roots.hpp`): first the ones the game brings, the roots of
  the authorities Cloudflare uses to sign the deployed server's certificates (Let's Encrypt, Google Trust
  Services, SSL.com and Sectigo, which it uses for its backup certificates); then the system's: the Linux roots
  file (Debian, Fedora, openSUSE...) or the macOS one (`/etc/ssl/cert.pem`) and Windows's `ROOT` store.
- **Why roots and not the server's certificate.** The server's is renewed every few months and pinning it
  would break the game on every renewal; roots last decades (the first to expire, in 2035). The game's own
  make it connect even if the system does not have them (Windows fills its store as it needs them); the system's,
  that a server of your own (`--versus-server`) with any public certificate also works. When a
  game root nears its end, the one that replaces it is added.
- **How.** libdatachannel with Mbed TLS does not have the system's roots: without them it cannot check anything (and on
  Windows it does not even try). `ws-ca-certificates.patch` lets its C API receive the roots (`caCertificatePemFile`,
  as its C++ API already does) and lets Windows also check against them. The game always gives them to it.
- `ws://` (without TLS) is still valid for a local test server.

## Limits

- No TURN: two networks that do not allow a direct route (some symmetric NATs, corporate networks) end up in `NoRoute`. The
  server could later give short-lived TURN credentials, and `ice_servers` is already a list of data.
- The signaling server is deployed at `wss://dwe-versus-signal.regamelabs.workers.dev`
  (`versus/network.yaml`); `--versus-server` changes it at startup.

## Tests

`dwe_net_tests` (label `net`): the channel over a clean link and over a bad one (latency, loss, duplicates and
reordering), resend, silence and cut, foreign and oversized messages; lockstep between two peers with 25% loss,
repetition and ack of the inputs, messages from another fight, ticks without input, desync
at its tick and damaged messages; the room codes; the signaling client against an in-memory
server with the real one's rules (pairing and forwarding, code that does not exist, full room, server down, guest
who leaves); and two real `RtcPeerLink`s that connect on the same machine through that in-memory server (without network or
STUN) and talk over both lanes.

The server's certificate (`tls_tests.cpp`): a local TLS server (libdatachannel's) with synthetic test
certificates (`test_certificates.hpp`). It opens with a certificate for its name signed by a trusted root; it fails
with one for another name, with one signed by a root the game does not know and with a self-signed one. With
`DWE_VERSUS_LIVE_SERVER=wss://dwe-versus-signal.regamelabs.workers.dev` (optional, needs internet) it checks the
deployed server: it makes a room with all the roots and with only the game's, and fails with a foreign root.
