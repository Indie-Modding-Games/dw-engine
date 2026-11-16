# 0006. Three repositories: code, design and art

- **Status:** Proposed
- **Date:** 2026-10-18
- **Note (2026-11-06):** the code repository is called `dw-engine`.

## Context

Programmers, designers and artists contribute to the project. Source art is heavy, and the legal risk of the content
is different from the legal risk of the code.

## Decision

| Repository | Contents |
|---|---|
| `engine` | Code, tools, schemas, text packs and technical documentation |
| `Digimon-World-Project-Folder` | Game design: GDD, story and characters |
| `Digimon-World-Project-Folder-Assets` | The team's source art, with Git LFS |

## Consequences

- Artists do not clone code and the code repository stays light.
- If a takedown request arrives against the content, the engine survives.
- The technical and design documentation link to each other, without duplicating.
- The assets repository must be emptied before it is converted (see [legal policy](../legal/assets.md)).

## Alternatives rejected

- **A monorepo:** simpler to clone, but it mixes heavy binaries with the code and concentrates all the legal risk in
  one place.
