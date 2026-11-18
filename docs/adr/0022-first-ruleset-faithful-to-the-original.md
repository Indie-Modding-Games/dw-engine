# 0022. The first ruleset reproduces the original

- **Status:** Accepted
- **Date:** 2026-10-20

## Context

[ADR 0010](0010-own-rules-with-rulesets.md) decides that the rules are data grouped in rulesets and that the first
ruleset only starts from the original's values, without checking that it is faithful. It also asks for design pillars
and a person in charge of design before designing the systems.

At the start of phase 3 the GDD in the design repository is still empty. Waiting for it would block the playable
core, and designing each system without it would give different criteria in each one.

## Decision

- **The first ruleset, `original`, reproduces the rules of the original game as they are in the decomp and in the
  disc tables.** Its specification is the [reference sheets](../reference/index.md); the code cites the decomp
  function it reproduces.
- **The new design arrives as other rulesets** on the same systems. That is why the rules that are logic, and not
  numbers, go behind a strategy that the ruleset chooses.
- **Design owner: Rivarux**, project owner. They approve every design decision; any change from the original is one of
  them.

It qualifies ADR 0010 without replacing it: the goal is still to reinvent the game, and the original is the first
ruleset, not the engine's contract.

## Consequences

- Phase 3 starts without waiting for the GDD and leaves a complete loop that can be played, measured and compared.
- Each new ruleset is measured against the original with the same simulations.
- Reproducing the original costs more than taking inspiration from it: the decomp has to be read system by system and
  tests written from the sheets.
- The original's oddities are design questions: the design owner decides which ones are reproduced.

## Alternatives rejected

- **Wait for the GDD:** it blocks phase 3 with no date.
- **Design each system on the fly:** design decisions made by programming, without a common criterion.
