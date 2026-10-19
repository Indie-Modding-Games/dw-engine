# 0012. Entidades con EnTT

- **Estado:** Propuesta
- **Fecha:** 2026-10-18

## Contexto

El mundo tiene muchas entidades con combinaciones distintas de comportamiento (partner, NPCs, enemigos, objetos,
disparadores), y los modos de juego y los mods tienen que poder combinarlas libremente.

## Decisión

Las entidades del mundo usan un **ECS** con **EnTT**, dentro del módulo `scene`. Los sistemas de `game/` trabajan
sobre sus componentes.

## Consecuencias

- Componer comportamientos sin jerarquías de herencia.
- Datos contiguos en memoria, buenos para el rendimiento.
- El equipo tiene que aprender el modelo ECS.

## Alternativas descartadas

- **Jerarquía de clases:** rígida, y cuesta combinar comportamientos.
- **Un ECS propio:** trabajo que EnTT ya resuelve.
