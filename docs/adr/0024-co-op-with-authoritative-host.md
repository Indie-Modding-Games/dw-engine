# 0024. Online co-op with an authoritative host and players driven by controllers

- **Status:** Proposed
- **Date:** 2026-11-16

## Context

We want a second player to connect to another player's game and play together in the same world. Today the field has a
single fixed player, the screens read the keyboard directly, the render camera feeds the simulation and the field
positions are floats, which are not guaranteed to give the same result on two machines. Combat is deterministic, and
the versus network (WebRTC, signaling with room codes, handshake with a content hash) already exists. The details are in
[Online co-op multiplayer](../architecture/multiplayer.md).

## Decision

One PC, the **host**, simulates the whole world and is the authority. The other one, the **client**, sends its
commands (and, in the first version, the position of its own player, which the host validates) and draws what the host
replicates to it: unreliable snapshots at 20 Hz and reliable events. Both players are the same data (`FieldPlayer`).
What changes is the `PlayerController` that gives them `PlayerCommands` every tick: local, remote or scripted. The
transport, the signaling and the handshake are those of versus.

## Consequences

- The per-player state has to come out of `Field`, all the screens have to move to commands and the simulation's
  dependency on the drawn camera has to be cut. That work (phase C0) also improves the single-player game: autotests,
  replays and cleaner input.
- The client does not need to simulate the world or match the host bit for bit. Float differences between machines do
  not break the game.
- The host's latency is noticeable in everything that is not your own movement (NPCs, dialogs, combat). With two
  players and a direct connection it is acceptable.
- Joining requires a safe point (free field), because the save does not carry the transient state.
- The room cards and online profile (#291–#295) become a common base for versus and co-op.

## Alternatives rejected

- **Lockstep of the whole world (like versus combat):** the field uses floats and random numbers outside
  `ruleset::Random`; a desync between machines would break the game and would force making the whole field
  deterministic.
- **Dedicated server:** hosting and operation cost for a two-player game; the project wants the PCs to talk directly,
  with a server only for the first contact.
- **Local multiplayer on one screen:** ruled out by the project.
- **Polymorphic player class (local and remote `IPlayer`):** it duplicates the player logic; the difference is only in
  where the commands come from.
