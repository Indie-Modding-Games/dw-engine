# versus

The online versus protocol (`dwe::game::versus`, `game` layer): two players, each on their own machine with the
registered partners of their game, play a series of battles in lockstep. It depends on `core` and `net`; it does not know
the battle: the session ([session](../apps/session.md), `VersusSeries`) joins it with `battle`, and the screens are in
the game ([game](../apps/game.md), "Online versus").

## How a series goes

0. The two connect over WebRTC: the host creates a room on the signaling server and shows its four-character
   code, the guest types it and the two open a direct connection ([net](../engine/net.md),
   [versus-signal](../services/versus-signal.md)).
1. The guest sends `Hello` with their identity: protocol version (2), dwe version and the content checksum.
2. The host compares it with their own. If anything differs it answers `Leave` with the reason (version, protocol or
   content) and both show it; if not, `Welcome` with the series: number of battles, arena, seed and input
   delay (3 frames, 150 ms).
3. Each side sends its `Lineup`: one record per battle (name, species, HP, MP, offense, defense, speed,
   brains, discipline and three techniques), as the registration machine stores it.
4. Each battle runs on both machines at once with `net::Lockstep`: only the players' commands travel. With
   the same seed, the same records and the same commands the two simulations are equal; the per-tick checksum
   verifies it and a difference stops the series as a desync.
5. When each battle ends both send `RoundOver` with their result, which has to match. `Series` counts: all the
   battles are played and whoever wins more wins (`VS_initializeVS`); a battle in which both fall or time runs out
   without a winner counts as both losing.
6. `Leave` at any moment ends the series for the other, with the reason.

`VersusLink` carries all of this over a `net::PeerLink`: the versus messages and the checksums on the reliable lane, the
lockstep inputs on the unreliable one (every 50 ms and on each new input it repeats the ones the other has not
confirmed, until the battle ends). Its phases are `Connecting`, `Lineups`, `Playing`, `BetweenRounds`, `Finished`
and `Failed`; the failure says why: the protocol ones (`NoAnswer`, `Silence`, `Left`, `ProtocolMismatch`,
`VersionMismatch`, `ContentMismatch`, `BadLineup`, `Desync`, `Damaged`) and the connection ones, which come from
`net::PeerFailure` (`NoServer`, `ServerUnreachable`, `WrongCode`, `RoomFull`, `TimedOut`, `NoRoute`, `Disconnected`).
Times (`LinkTimeouts`): 10 s waiting for the host's answer from when the connection opens and 8 s of silence once
they are talking.

## The network (`versus/network.yaml`)

`loadNetworkConfig` reads `versus/network` from the game pack (schema `versus_network.schema.json`):

```yaml
signal_server: "wss://dwe-versus-signal.regamelabs.workers.dev"  # ws:// or wss://; empty: there is no server and the game says so
ice_servers: [stun:stun.cloudflare.com:3478]   # stun:, turn: or turns:
```

`dwe-game --versus-server wss://…` changes the server for
one game (to test one). None of this goes in the saved game.

## The content checksum

`session::versusContentHash` is a SHA-1 over the id, the version and the ruleset's string, the YAML of each section and
the bytes of each table. If the two players do not have the same content (another extracted disc, another mod, another
version of the rules), the host rejects it before starting: none of the content travels, only the checksum.

## Risks

- Without TURN, two networks without a direct route between them cannot play: the game says so ("no direct route") and suggests
  another network.

- The battle uses floating point in some calculations (positions, turns). With the same binary and the same CPU it gives the same;
  between different compilers or systems (Linux against Windows) it could diverge. The per-tick checksum detects it and the
  series stops cleanly with "desynchronization"; it has not yet been tested between a Linux and a Windows.

## Tests

`dwe_versus_tests` (label `versus`): the round-trip messages, the series (`VS_initializeVS`), the welcome and
the lineups, rejection by version or content, a whole series in lockstep with 33% loss, results
that do not match, silence, no answer, leaving, each connection failure with its reason and the `network.yaml`.
