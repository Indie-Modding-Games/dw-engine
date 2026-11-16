# Plan por fases

Cada fase tiene un criterio de cierre que se puede comprobar. Una fase no se da por terminada hasta que se cumple.

Este documento es el resumen con el estado del día. Los diseños que se acuerdan antes de programar están en las
[propuestas de diseño](plan/index.md).

| Fase | Nombre | Tamaño | Estado |
|---|---|---|---|
| 0 | Base | S | Cerrada (2026-10-20): Linux y Windows pasan en la CI de GitHub; la limpieza de los repositorios antiguos espera una decisión del equipo |
| 1 | Tubería de assets | M | Cerrada (2026-10-20): el disco US se extrae entero (0 pendientes, 0 parciales) y el motor carga el resultado, extraído y cocinado; el formato cocinado de los vídeos pasa a la fase 5 |
| 2 | Prueba vertical | M | Cerrada (2026-10-20) |
| 3 | Núcleo jugable | L | Cerrada (2026-10-21) |
| 4 | El juego original entero | XL | En curso: la historia entera se juega, del título a los créditos, y la beta cerrada llega por el lanzador; falta la primera estable ([qué la bloquea](#fase-4--el-juego-original-entero)) |
| 5 | Mods, modos y mejoras | Continuo | Empezada: el versus en línea y en la Arena, los modos de juego y los hooks de mods ya están en `main` |

## Dónde estamos (2026-11-16)

- **La fase 4 es solo el juego original.** Desde el 2026-10-22 la fase 4 es el juego original (vanilla) entero y
  jugable. Lo que no es del original pasa a la fase 5: modos de juego, el modo versus, la API de mods completa, la
  historia reescrita y el formato cocinado de los vídeos.
- **La historia, por etapas.** El port se entrega etapa a etapa detrás del candado de historia
  (`content/packs/game/rules/original/story_lock.yaml`, opción `--story-stage`). Las nueve etapas están abiertas
  (`active_stage: stage_9`) y las rutas `tests/autotest/route-full-01` a `route-full-09` juegan la historia entera
  encadenadas ([prueba vertical](guides/vertical-slice.md)).
- **Cobertura del port:** 1391 de las 1456 funciones del original que hay que portar están portadas y 5 a medias.
  Las 60 que faltan son del versus y la demo del título, de la fase 5 ([cobertura](reference/port-coverage.md)).
- **Fase 5 empezada.** El versus en línea y en la Arena, los modos de juego y los hooks de mods ya están en `main`.
- **Beta cerrada por el lanzador.** Cada fusión en `main` que cambia código publica una versión de prueba
  (`<versión>-beta.<commits en main>`, canal «Pruebas» del lanzador, con la clave de la beta a través de la
  [pasarela](modules/services/beta-gateway.md)). Todavía no se ha promovido ninguna estable. Paquetes para Linux,
  Windows y macOS ([lanzador](modules/apps/launcher.md), [beta para Linux](guides/beta-linux.md),
  [beta para Windows y macOS](guides/beta-windows-macos.md)). La primera ronda de testers empezó el 2026-11-06.
- **Lo siguiente:** arreglar lo que encuentren los testers (reportes F12), cerrar los huecos que quedan
  ([huecos que el jugador notará](reference/port-coverage.md#huecos-que-el-jugador-notará)), promover la primera
  versión estable y cerrar la fase 4. Después, la fase 5.

## Fase 0 — Base

Cerrada (2026-10-20). Repositorio con CMake, presets, vcpkg y la CI en Linux y Windows, y un motor mínimo: ventana y
dispositivo Vulkan, entrada por acciones, lógica a paso fijo, Lua con recarga en caliente, log y consola, y la
comprobación del disco por SHA-1 (`dwx verify`). Cómo compilarlo: [preparar el entorno](getting-started/setup.md).

## Fase 1 — Tubería de assets

Cerrada (2026-10-20). La librería `packages/formats/`, `dwx extract`, `validate`, `cook` y `pack`, el sistema de
ficheros virtual con capas, los esquemas y las primeras fichas de referencia. El disco US se extrae entero (0
pendientes, 0 parciales) y el motor carga el resultado, extraído y cocinado. Dónde está documentado: [tubería de
assets](architecture/assets-pipeline.md), [formatos](formats/index.md) y [fichas de referencia](reference/index.md).

## Fase 2 — Prueba vertical

Cerrada (2026-10-20). Fondos prerenderizados con capas de profundidad, modelos con skinning, el campo (movimiento,
colisión, warps, el partner siguiendo al jugador), RmlUi con un primer menú y el minimapa. Cómo jugarla y correr sus
rutas: [prueba vertical](guides/vertical-slice.md).

## Fase 3 — Núcleo jugable

Cerrada (2026-10-21). Rulesets (el formato, estrategias para las reglas que son lógica, y el ruleset `original`, que
reproduce el original; el diseño nuevo llega después como rulesets que aprueba el responsable de diseño, [ADR
0010](adr/0010-own-rules-with-rulesets.md)), el partner, la batalla, el inventario y el guardado, el panel de ajuste en
vivo, las simulaciones de balance y el cambio de mapa como el original. Cada sistema sigue el [ciclo de
diseño](conventions/design-cycle.md). El bucle de criar, entrenar, combatir y evolucionar lo comprueban una prueba sin
ventana y la ruta `route-loop-keyboard` ([prueba vertical](guides/vertical-slice.md)).

## Fase 4 — El juego original entero

- Historia: los scripts de eventos del disco convertidos a Lua al extraer, y el intérprete de historia que los
  ejecuta (`packages/game/story`).
- Todos los sistemas del original que pide la historia: tiendas, torneos, pesca, minijuegos, muerte y renacer,
  películas y final.
- Entrega por etapas detrás del candado de historia, cada una con sus rutas de autotest.
- Beta cerrada: lanzador, paquetes por sistema, versiones de prueba y estables.

**Hecho cuando:** el juego original se juega entero, del título a los créditos, y hay una versión estable publicada.

**Estado (2026-11-16): en curso.** Las nueve etapas están abiertas, la cadena `route-full-01` a `route-full-09` juega
la historia entera y la beta tiene versiones de prueba. Lo que bloquea el cierre de la fase:

1. Todavía no hay versión estable, solo de prueba. Tarjeta: «Promover la primera estable cuando acabe la ronda de
   testers».
2. La cadena de la historia no se ha corrido sobre el `main` de hoy: estaba en verde el 2026-11-04 y han entrado más
   cambios desde entonces. Tarjeta: «Correr `route-full-01` a `route-full-09` sobre el commit que se promueva a
   estable».

Al principio la fase 4 era «Contenido y modos». De su fase 4a
ya están en `main` la conversión de los scripts a Lua, los paquetes de mods por ID, la API de Lua versionada, los
aspectos de la interfaz y los tests de ida y vuelta del editor de mapas externo. El resto pasó a la fase 5.

## Fase 5 — Mods, modos y mejoras

- Historia reescrita sobre los scripts convertidos.
- Profundidad de personajes: árboles de diálogo, afinidad y misiones.
- Framework de modos de juego, cada uno con su ruleset, y el modo versus.
- API de mods completa: paquetes que añaden o sobrescriben contenido por ID, y hooks de Lua.
- Más especies y mapas, fondos HD.
- El formato cocinado de los vídeos.
- El editor de mapas es la herramienta externa de la organización (`dw1fe`), con tests de ida y vuelta de nuestro
  formato; el motor no tiene editor propio.

**Hecho cuando:** se publica la primera versión del mod. A partir de ahí la fase sigue abierta.

## Riesgos

| Riesgo | Cómo se mitiga |
|---|---|
| Alcance enorme | Prueba vertical pronto, criterios de cierre por fase y mejoras visibles desde la fase 2 |
| Perder la identidad del juego sin querer | Pilares de diseño escritos y fichas de referencia antes de cada sistema |
| Un diseño distinto en la cabeza de cada persona | El GDD es la especificación, y una persona responsable de diseño tiene la última palabra |
| El balance | Rulesets editables, ajuste en vivo, simulaciones y pruebas de juego frecuentes |
| Digimon de otros juegos | Importador desde el disco del usuario, o modelos propios del equipo |
| Riesgo legal | [Política legal de assets](legal/assets.md) |
