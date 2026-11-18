# scene

The world's entities and their components, with EnTT ([ADR 0012](../../adr/0012-ecs-with-entt.md)). `World` is
the owner of the `entt::registry`; the systems in `packages/game/` add and read components with `registry()`, and `first<Tag>()`
finds, for example, the player.

The shared components (`<dwe/scene/components.hpp>`) are those the app needs to draw, in
[world space](../../architecture/world-space.md):

| Component | What it is |
|---|---|
| `Transform` | Position and yaw (radians about +Y; at 0 it faces +Z), and those of the previous tick for drawing between ticks with `interpolate` |
| `ModelRef` | The ID of the `.glb` that is drawn (`digimon/species_a/model`) |
| `AnimationIntent` | `Motion` (`idle`, `walk`, `run`) and the clip's speed scale, so that the feet go with the ground |
| `PlayerTag`, `PartnerTag` | Mark the player and the partner |

Each system's own state (for example, the trail the partner follows) goes in components of that system's module,
not here. Systems layer: it only depends on `core` and on EnTT.
