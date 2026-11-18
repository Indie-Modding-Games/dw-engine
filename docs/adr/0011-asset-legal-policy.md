# 0011. "Bring your own data" and the legal policy for assets

- **Status:** Accepted
- **Date:** 2026-10-18
- **Note (2026-11-06):** accepted because the code already follows it: the git hooks and the pack check enforce the policy, and the license is MIT. `provenance.yaml` and the hash check in CI do not exist yet ([legal policy](../legal/assets.md)).

## Context

It is a non-profit fan project. The assets, code and marks of the original game belong to their owners, and the
project must minimize legal risk while still being useful.

## Decision

- The project **distributes nothing from the original**. Each user provides their own disc and the extraction happens
  on their machine.
- What is derived from the original is distributed only as recipes or deltas.
- No money, no marks and no stores.
- `provenance.yaml` per asset and a hash check in CI.
- Licenses: the code, MIT (decided on 2026-11-06, see `LICENSE`); the team's original assets, CC BY-SA 4.0.

The details are in the [legal policy for assets](../legal/assets.md).

## Consequences

- The project's final name cannot use the franchise's name.
- The current repositories must be cleaned before anything is published.
- The CI fixtures are created by the team, because CI does not have the disc.

## Alternatives rejected

- **Distribute the original assets:** it is the most common cause of takedown requests.
