# 0011. "Trae tus propios datos" y política legal de assets

- **Estado:** Propuesta
- **Fecha:** 2026-10-18

## Contexto

Es un proyecto de fans sin ánimo de lucro. Los assets, el código y las marcas del juego original pertenecen a sus
titulares, y el proyecto debe minimizar el riesgo legal sin dejar de ser útil.

## Decisión

- El proyecto **no distribuye nada del original**. Cada usuario aporta su disco y la extracción ocurre en su máquina.
- Lo derivado del original se distribuye solo como recetas o deltas.
- Sin dinero, sin marcas y sin tiendas.
- DCO en cada commit, `provenance.yaml` por asset y chequeo de hashes en la CI.
- Licencias: el código, por decidir entre MIT y GPL-3.0; los assets originales del equipo, CC BY-SA 4.0.

El detalle está en la [política legal de assets](../legal/assets.md).

## Consecuencias

- El nombre definitivo del proyecto no puede usar el nombre de la franquicia.
- Hay que limpiar los repositorios actuales antes de publicar nada.
- Los fixtures de la CI los crea el equipo, porque la CI no tiene el disco.

## Alternativas descartadas

- **Distribuir los assets originales:** es la causa más habitual de las peticiones de retirada.
