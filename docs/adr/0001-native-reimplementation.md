# 0001. Native reimplementation without PS1 limitations

- **Status:** Accepted
- **Date:** 2026-10-18

## Context

We want to change the game logic, improve the menus, add minimaps, rewrite the story, give the characters more depth
and create new game modes.

The port that exists today builds the decomp on psxstack, which reproduces the PS1 memory model: 2 MB of RAM, overlays
at fixed addresses, fixed-size IDs (for example, 180 Digimon in a `u8`) and a 320×240 resolution. Every big
improvement means fighting those limits and patching tightly coupled code from 1999.

## Decision

We write our **own engine in C++** that reinvents the game. The decomp is the **reference**: we read it to understand
the disc formats and how each system worked, but we do not build it into the engine or replicate its behavior
([ADR 0010](0010-own-rules-with-rulesets.md)).

## Consequences

- No fixed limits on memory, content, resolution or frame rate.
- Every system is designed from the start so it can be modified.
- The first game loop arrives in phase 3 of the [plan](../roadmap.md); visible improvements, from phase 2.
- psxstack and the patches of the current port stop being part of the product.

## Alternatives rejected

- **Keep going with the port on psxstack:** keeps all the PS1 limitations.
- **Migrate the port to C++ bit by bit:** keeps the PS1 limitations during the whole migration, because the old code
  and the new code share the emulated memory.
