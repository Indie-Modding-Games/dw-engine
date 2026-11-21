# 0026. Variantes de juego como modos de juego con opciones fijas

- **Estado:** Aceptada
- **Fecha:** 2026-11-20

## Contexto

Los jugadores pidieron dos experiencias sobre un motor: el original tal como era, con las mejoras y arreglos del motor
(Clásico Mejorado), y el juego ampliado con todo lo que el proyecto añadió (Modo Folder). El motor ya tenía las piezas:
modos de juego (`modes/<id>/mode.yaml`) que la partida nueva pregunta en cada skin y que la partida guardada registra,
rulesets para el contenido oculto, el paso a Mega y todas las especies como compañero, y una página de opciones cuyas
filas dicen cada una su valor original. Le faltaba una forma de que un modo mantuviera en el valor del original las
opciones que cambian las reglas.

## Decisión

Una variante es un modo de juego. Clásico Mejorado (`modes/classic`) admite solo el ruleset `original` y fija, con una
clave opcional nueva `options`, las opciones que cambian el balance o el contenido (dificultad, objetos raros,
exploración libre, baños extra, el huevo tras perder todas las vidas). El Modo Folder (`modes/folder`) admite todos los
rulesets de la pantalla de reglas y no fija nada. El juego aplica los valores fijos de un modo sobre la configuración
del jugador mientras se juega una de sus partidas, sin escribir el archivo de configuración, los publica a la UI como
bloqueados (API de UI 0.28.0: `options.variant()`, `options.locked(name)`) y rechaza los comandos que los cambian. La
partida guardada conserva el modo como antes, y el formato 4 lo añade al resumen de la cabecera para que la lista de
ranuras lo muestre. Las partidas del antiguo modo único, `original`, se cargan como Modo Folder. El comportamiento se
describe en [Variantes de juego](../architecture/game-variants.md).

## Consecuencias

- Una build, un conjunto de packs y un solo camino de código: los sistemas nunca preguntan qué variante se juega.
- Todo lo que los jugadores tienen hoy sigue al alcance: las partidas antiguas conservan todo lo que tenían, y una
  partida nueva elige cualquiera de las dos.
- Una partida siempre juega la variante con la que empezó. Convertir Clásico Mejorado en Modo Folder queda para más
  adelante.
- Los invitados del cooperativo reciben la variante del host a través de la partida del host, sin cambio de protocolo.
- Un mod añade o sustituye una variante con un `mode.yaml`, como cualquier otro recurso.
- Solo se pueden fijar las opciones que una partida aplica desde su propia configuración (`FixableOptions`); fijar
  opciones de gráficos o de interfaz necesitaría más trabajo.

## Alternativas descartadas

- **Un ajuste del launcher o una build aparte por variante:** una elección por instalación en lugar de por partida, y
  una segunda descarga o un segundo conjunto de packs que mantener a la par.
- **Un flag que comprueban los sistemas:** cada sistema tendría que saber de variantes, y un mod no podría añadir una.
- **Reescribir la configuración del jugador al empezar una partida del Clásico Mejorado:** se perderían las elecciones
  del jugador para sus otras partidas.
