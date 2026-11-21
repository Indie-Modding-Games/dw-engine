# 0026. Game variants as game modes with fixed options

- **Status:** Accepted
- **Date:** 2026-11-20

## Context

Players asked for two experiences on one engine: the original as it was, with the engine's improvements and fixes
(Classic Enhanced), and the expanded game with everything the project added (Folder Mode). The engine already had
the pieces: game modes (`modes/<id>/mode.yaml`) that the new game asks for in every skin and the save records, rulesets
for the hidden content, the Mega step and every species as a partner, and an options page whose rows each say their
original value. What it lacked was a way for a mode to keep the options that change the rules at the original's value.

## Decision

A variant is a game mode. Classic Enhanced (`modes/classic`) admits only the `original` ruleset and fixes, with a new
optional `options` key, the options that change the balance or the content (difficulty, rare items, free exploration,
extra toilets, the egg after losing every life). Folder Mode (`modes/folder`) admits every ruleset of the rules screen
and fixes nothing. The game applies a mode's fixed values over the player's settings while one of its games is played,
without writing the settings file, publishes them to the UI as locked (UI API 0.28.0: `options.variant()`,
`options.locked(name)`) and refuses commands that change them. The save keeps the mode as before, and format 4 adds it
to the header's summary so the slot list shows it. Saves of the old single mode, `original`, load as Folder Mode. The
behavior is described in [Game variants](../architecture/game-variants.md).

## Consequences

- One build, one set of packs and one code path: the systems never ask which variant is played.
- Every feature players have today stays reachable: old saves keep everything they had, and a new game picks either.
- A save always plays the variant it was started with. Converting Classic Enhanced to Folder Mode is left for later.
- Co-op guests get the host's variant through the host's save, with no protocol change.
- A mod adds or replaces a variant with a `mode.yaml`, like any other asset.
- Only the options a game applies from its own settings can be fixed (`FixableOptions`); fixing graphics or interface
  options would need more work.

## Alternatives rejected

- **A launcher setting or a separate build per variant:** a choice per installation instead of per save, and a second
  download or a second set of packs to keep in step.
- **A flag the systems check:** every system would have to know about variants, and a mod could not add one.
- **Rewriting the player's settings when a Classic Enhanced game starts:** the player's choices for their other games
  would be lost.
