# 0001. Reimplementación nativa sin limitaciones de PS1

- **Estado:** Aceptada
- **Fecha:** 2026-10-18

## Contexto

Queremos cambiar la lógica del juego, mejorar los menús, añadir minimapas, reescribir la historia, dar profundidad a
los personajes y crear modos de juego nuevos.

El port que existe hoy compila el decomp sobre psxstack, que reproduce el modelo de memoria de PS1: 2 MB de RAM,
overlays en direcciones fijas, IDs de tamaño fijo (por ejemplo, 180 Digimon en un `u8`) y resolución de 320×240. Cada
mejora grande obliga a pelear contra esos límites y a parchear código de 1999 muy acoplado.

## Decisión

Escribimos un **motor propio en C++** que reinventa el juego. El decomp es la **referencia**: se lee para entender los
formatos del disco y cómo funcionaba cada sistema, pero no se compila dentro del motor ni se replica su
comportamiento ([ADR 0010](0010-reglas-propias-con-rulesets.md)).

## Consecuencias

- Sin límites fijos de memoria, contenido, resolución ni frecuencia de imagen.
- Cada sistema se diseña desde el principio para poder modificarse.
- El primer bucle de juego llega en la fase 3 del [plan](../roadmap.md); las mejoras visibles, desde la fase 2.
- psxstack y los parches del port actual dejan de formar parte del producto.

## Alternativas descartadas

- **Seguir con el port sobre psxstack:** mantiene todas las limitaciones de PS1.
- **Migrar el port poco a poco a C++:** mantiene las limitaciones de PS1 durante toda la migración, porque el código
  antiguo y el nuevo comparten la memoria emulada.
