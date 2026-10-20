# 0012. Entidades con EnTT

- **Estado:** Aceptada
- **Fecha:** 2026-10-18 (aceptada el 2026-10-19)

## Contexto

El mundo tiene muchas entidades con combinaciones distintas de comportamiento (partner, NPCs, enemigos, objetos,
disparadores), y los modos de juego y los mods tienen que poder combinarlas libremente.

## Decisión

Las entidades del mundo usan un **ECS** con **EnTT**, dentro del módulo `scene`. Los sistemas de `game/` trabajan
sobre sus componentes.

### Detalles (fase 2)

- EnTT entra por vcpkg (`entt`), solo en `engine/scene`, que lo expone en su API pública: `scene::World` es un envoltorio
  fino del `entt::registry` y los sistemas usan el registro directamente.
- `scene` tiene solo los componentes que comparten varios módulos: `Transform` (posición y yaw en el espacio del mundo,
  con los valores del tick anterior para interpolar al dibujar), `ModelRef`, `AnimationIntent` y las etiquetas
  `PlayerTag` y `PartnerTag`. El estado propio de un sistema va en componentes de su módulo.
- La lógica corre a tick fijo y escribe los componentes; la app los lee para dibujar y no los modifica.

## Consecuencias

- Componer comportamientos sin jerarquías de herencia.
- Datos contiguos en memoria, buenos para el rendimiento.
- El equipo tiene que aprender el modelo ECS.

## Alternativas descartadas

- **Jerarquía de clases:** rígida, y cuesta combinar comportamientos.
- **Un ECS propio:** trabajo que EnTT ya resuelve.
