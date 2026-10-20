# 0021. Animación muestreada en la CPU, skinning en la GPU, en un módulo `animation`

- **Estado:** Aceptada
- **Fecha:** 2026-10-19

## Contexto

Los modelos extraídos son glTF con piel rígida (cada vértice sigue a un hueso) y clips con un fotograma por tick del
original, bucles y movimiento raíz en `extras` ([modelos](../formats/modelos.md#gltf)). El juego necesita reproducir
idle, andar y correr del jugador y del compañero (R2.5), el campo necesita el movimiento raíz o la velocidad de paso
de esos clips sin depender de `render` (`game/*` no usa `render`), y `render` dibuja N instancias por frame.

## Decisión

- El muestreo de clips y el cálculo de las matrices de los huesos se hacen en la **CPU**, en un módulo nuevo
  `animation` (capa de sistemas, depende de `core` y `assets`), con funciones puras y un `AnimationPlayer` por
  instancia.
- El **skinning se hace en la GPU**: los vértices se suben una vez y el shader los transforma con las matrices de
  la instancia, que se suben cada frame.
- El tiempo de un clip respeta los `extras` del extractor: los bucles con cuenta vienen desenrollados y el último
  bucle de un clip que termina en bucle se repite para siempre, con un tick de paso de `lastTick` a `firstTick`. La
  traslación del hueso raíz es movimiento raíz: no entra en la pose y se da aparte.

## Consecuencias

- Las pruebas de poses corren sin GPU (en la CI): tick N = fotograma N, bucles, movimiento raíz.
- `game/field` puede usar `animation` para la velocidad de paso (`repeatGroundSpeed`) sin tocar `render`.
- El coste en la CPU es una pose por instancia y frame (decenas de huesos por modelo): despreciable para los pocos
  modelos de un mapa. Si un día hacen falta cientos de instancias, el muestreo puede ir a un compute shader sin
  cambiar la API de `render`.

## Alternativas descartadas

- **Skinning en la CPU** (subir vértices ya transformados cada frame): más ancho de banda por frame y código de
  dibujo distinto para modelos con y sin animación.
- **Muestreo en la GPU:** las pruebas de poses necesitarían GPU y el campo no podría leer el movimiento raíz.
- **Poner el reproductor dentro de `render`:** el campo dependería de `render` para saber a qué velocidad anda un
  modelo, cosa que las reglas de dependencias prohíben a `game/*`.
