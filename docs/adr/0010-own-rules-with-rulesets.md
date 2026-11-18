# 0010. Own rules, organized in rulesets

- **Status:** Accepted
- **Date:** 2026-10-18

## Context

The goal is to reinvent the game, not to reproduce it. We had to choose between two options:

- **A.** Reproduce the behavior of the original (formulas, timings, AI, random number generator and bugs), verified
  with traces and differential tests.
- **B.** Use the original only as inspiration.

## Decision

**Option B.** The original is a starting point, not a contract. In addition:

- **The game rules are data, grouped in rulesets.** A ruleset gathers the formulas and values: evolution, life, care,
  damage, economy. It lives in a content pack, in YAML and Lua.
- **The first ruleset starts from the original's values**, taken from the disc tables and the reference sheets. It is
  not verified to be faithful: it only serves as the starting point for balance.
- **The mod ruleset** is the reinvention, and each game mode can bring its own.
- **Every system has a reference sheet** in [`docs/reference/`](../reference/index.md) that summarizes how the
  original worked, so design can decide knowingly what to keep and what to change.
- **The specification is the GDD.** Every system follows the [design cycle](../conventions/design-cycle.md).

## Consequences

- Design freedom from the first system and quite a bit less work: no traces, no differential tests, and no phase
  dedicated to rebuilding the original.
- The original game does not exist inside the engine: whoever wants to play it uses their disc.
- Written **design pillars** are needed, and a person in charge of design with the final say.
- Balance is our own work: editable tables, a debug panel to adjust values live, simulations (for example, a thousand
  raisings to see which evolutions come out) and frequent playtests are needed.
- Trying a design idea means editing data, not code.

## Alternatives rejected

- **A. Faithful behavior:** a lot of work spent replicating things that are going to change, and it ties the timings
  and formulas to the original.
- **B without rulesets:** the rules would be fixed in C++, and every design change or new game mode would require
  recompiling.
