# scene

Las entidades del mundo y sus componentes, con EnTT ([ADR 0012](../../adr/0012-ecs-con-entt.md)). `World` es
el dueño del `entt::registry`; los sistemas de `game/` añaden y leen componentes con `registry()`, y `first<Tag>()`
encuentra, por ejemplo, al jugador.

Los componentes compartidos (`<dwe/scene/components.hpp>`) son los que la app necesita para dibujar, en el
[espacio del mundo](../../architecture/world-space.md):

| Componente | Qué es |
|---|---|
| `Transform` | Posición y yaw (radianes sobre +Y; con 0 mira a +Z), y los del tick anterior para dibujar entre ticks con `interpolate` |
| `ModelRef` | El ID del `.glb` que se dibuja (`digimon/species_a/model`) |
| `AnimationIntent` | `Motion` (`idle`, `walk`, `run`) y la escala de velocidad del clip, para que los pies vayan con el suelo |
| `PlayerTag`, `PartnerTag` | Marcan al jugador y al partner |

El estado propio de cada sistema (por ejemplo, el rastro que sigue el partner) va en componentes del módulo de ese
sistema, no aquí. Capa de sistemas: solo depende de `core` y de EnTT.
