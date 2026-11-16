# 0012. Entities with EnTT

- **Status:** Accepted
- **Date:** 2026-10-18 (accepted on 2026-10-19)

## Context

The world has many entities with different combinations of behavior (partner, NPCs, enemies, objects, triggers), and
game modes and mods must be able to combine them freely.

## Decision

World entities use an **ECS** with **EnTT**, inside the `scene` module. The systems in `packages/game/` work on its
components.

### Details (phase 2)

- EnTT comes in through vcpkg (`entt`), only in `packages/engine/scene`, which exposes it in its public API:
  `scene::World` is a thin wrapper around the `entt::registry` and the systems use the registry directly.
- `scene` has only the components shared by several modules: `Transform` (position and yaw in world space, with the
  values from the previous tick to interpolate when drawing), `ModelRef`, `AnimationIntent` and the tags `PlayerTag`
  and `PartnerTag`. A system's own state goes in components of its module.
- The logic runs at a fixed tick and writes the components; the app reads them to draw and does not modify them.

## Consequences

- Behaviors are composed without inheritance hierarchies.
- Data is contiguous in memory, good for performance.
- The team has to learn the ECS model.

## Alternatives rejected

- **Class hierarchy:** rigid, and behaviors are hard to combine.
- **An ECS of our own:** work that EnTT already solves.
