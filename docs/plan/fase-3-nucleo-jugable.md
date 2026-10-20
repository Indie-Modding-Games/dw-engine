# Fase 3: Núcleo jugable

## Objetivo

El bucle principal del juego funciona con el diseño nuevo: criar al partner, cuidarlo, entrenarlo, combatir y
evolucionar. Con guardado, inventario y herramientas para ajustar el balance mientras se juega.

## Por qué

Es el corazón del juego y lo que más va a cambiar respecto al original. Va después de la prueba vertical porque
necesita el mundo funcionando (campo, render, interfaz). Va antes que la historia porque la historia se apoya en estos
sistemas.

## Condición para empezar

El repositorio de diseño tiene escritos los **pilares de diseño** y hay una persona responsable de diseño con la
última palabra ([ADR 0010](../adr/0010-reglas-propias-con-rulesets.md)). Sin eso, cada sistema se diseñaría con un
criterio distinto.

## Cómo se especifica cada sistema

Esta fase no define cómo funciona la batalla ni la evolución. Eso lo decide el GDD. Cada sistema sigue el
[ciclo de diseño](../conventions/design-cycle.md):

1. ficha de referencia del original en [`docs/reference/`](../reference/README.md);
2. diseño en el GDD;
3. aprobación;
4. implementación con las reglas en el ruleset;
5. tests;
6. ajuste en vivo y pruebas de juego.

Esta especificación fija lo que el motor tiene que ofrecer para que eso sea posible.

## Alcance

- **Rulesets:** el formato y el primer ruleset, sembrado con los valores del original sacados de las tablas del disco.
- **Partner:** stats, cuidado (hambre, sueño, aseo, ánimo…), vida y evolución.
- **Batalla.**
- **Inventario y guardado:** ranuras y autoguardado, en ficheros, sin tarjetas de memoria.
- **Panel de ajuste en vivo** y simulaciones de balance.

## Fuera de alcance

- Historia, diálogos y misiones → fase 4.
- Modos de juego distintos del principal → fase 4 (aunque el ruleset ya los permite).

## Requisitos

| ID | Requisito | Cómo se comprueba |
|---|---|---|
| R3.1 | Las reglas (fórmulas y valores) viven en un ruleset de YAML y Lua dentro de un paquete, nunca fijas en C++ | Revisión; test que cambia un valor del ruleset y ve el efecto |
| R3.2 | El primer ruleset se genera a partir de las tablas extraídas del disco | Test que lo compara con las tablas de la fase 1 |
| R3.3 | Cada sistema (partner, cuidado, evolución, batalla, inventario) cumple su documento del GDD | Tests escritos a partir del GDD |
| R3.4 | Un ruleset se puede cambiar en caliente desde el panel de depuración, sin reiniciar | Prueba manual |
| R3.5 | Hay simulaciones que juegan miles de partidas sin gráficos para medir el balance | Herramienta de simulación con informe |
| R3.6 | El guardado usa ranuras y autoguardado; una partida guardada se carga igual tras actualizar el juego, o se migra | Tests de guardado y migración |
| R3.7 | La batalla y los menús del partner se usan con ratón, teclado y mando | Prueba manual con los tres dispositivos |
| R3.8 | El cambio de mapa se ve como en el original: fundido sustractivo a gris y nombre del mapa al entrar (pendientes de la fase 2, ver [campo](../reference/campo.md)) | Test del fundido y captura comparada con el original |

## Diseño

- Módulos: `game/partner`, `game/battle`, `game/inventory`, `game/save` y el formato de ruleset en un paquete de
  contenido.
- La interfaz de estos sistemas usa RmlUi, como en la fase 2.

## Criterio de cierre

El bucle de criar, entrenar, combatir y evolucionar se juega con el diseño nuevo.

## Dependencias y riesgos

- Depende de la fase 2 y de los pilares de diseño.
- Riesgo: el balance. Se mitiga con rulesets editables, ajuste en vivo, simulaciones y pruebas de juego frecuentes.
- Riesgo: perder la identidad del juego. Las fichas de referencia permiten decidir a conciencia qué se conserva.

## Estado (2026-10-19)

Pendiente. Las fichas de referencia de cuidado, evolución y batalla ya están escritas (fase 1).
