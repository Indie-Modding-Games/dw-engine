# Design cycle of a system

The game is reinvented ([ADR 0010](../adr/0010-own-rules-with-rulesets.md)), so each system's specification is its
design document in the GDD, not the original's behavior. Every system (care, evolution, battle, economy, a game
mode…) follows this cycle.

| Step | What | Where | Who |
|---|---|---|---|
| 1. Reference sheet | How the original worked, taken from the decomp | [`docs/reference/`](../reference/index.md) | Programming |
| 2. Design | How it will work now, justified against the design pillars | GDD (design repository) | Design |
| 3. Approval | The person in charge of design approves it | PR in the design repository | Design lead |
| 4. Implementation | The system in C++; its rules and values, in the ruleset (YAML or Lua), never fixed in the code | `packages/game/` + `content/packs/` | Programming |
| 5. Tests | That the system does what the GDD says, and balance simulations | `tests/` | Programming |
| 6. Tuning and playtests | Values changed live from the debug panel and play sessions | Editor and team | Everyone |
| 7. Iterate | What was learned goes back to step 2 | — | Design |

## Rules

- **A system is not programmed without an approved design.** A prototype to explore is fine, but it is not merged. For
  the `original` ruleset, the approved design is the reference sheet ([ADR 0022](../adr/0022-first-ruleset-faithful-to-the-original.md)).
- **Numbers do not live in C++.** If a value can be tuned, it is in the ruleset.
- **Every deviation from the original is a decision.** The design document says what is kept, what changes and why.
