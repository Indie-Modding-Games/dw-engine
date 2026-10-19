# 0014. Mandos y entrada por acciones

- **Estado:** Propuesta
- **Fecha:** 2026-10-18

## Contexto

El juego tiene que funcionar con cualquier mando que se conecte al PC, además de con teclado y ratón
([ADR 0013](0013-juego-nativo-de-pc.md)). Los jugadores mezclan dispositivos, los conectan y desconectan en plena
partida y esperan poder reasignar todos los controles.

## Decisión

**Dispositivos.** Usamos la API de mandos de SDL3:

| Familia | Ejemplos |
|---|---|
| Xbox | Mandos de Xbox One y Series, y compatibles XInput |
| PlayStation | DualShock 4, DualSense y DualSense Edge |
| Nintendo | Switch Pro Controller y Joy-Con |
| Steam | Steam Deck (controles integrados) y Steam Controller, también a través de Steam Input |
| Genéricos | 8BitDo y otros, mediante la base de datos de mapeos de SDL; el usuario puede ampliarla con un fichero propio |

- **Conexión en caliente**: un mando se puede conectar o desconectar en cualquier momento. Si se desconecta en plena
  partida, el juego se pausa.
- **Varios mandos a la vez**, para que un modo de juego local pueda tener más de un jugador.

**Entrada por acciones.** El juego nunca lee botones, sino **acciones** (`confirm`, `cancel`, `menu`, `move`,
`camera`…), agrupadas en **contextos** (campo, batalla, menú). El módulo `input` traduce teclas, ratón y mandos a
acciones según los perfiles de cada dispositivo.

- Todas las acciones se pueden reasignar por dispositivo, y los perfiles se guardan en los ajustes.
- Movimiento analógico en 360°, con zonas muertas configurables.
- Opción de intercambiar confirmar y cancelar (la disposición de Nintendo frente a la de Xbox).
- **Avisos de botón automáticos**: los iconos siguen al último dispositivo usado y al tipo de mando. Los iconos son
  propios o con licencia libre (por ejemplo, el pack *Input Prompts* de Kenney, CC0), nunca los oficiales de ningún
  fabricante.
- Vibración. Más adelante, opcional: giroscopio, gatillos adaptativos y luz del DualSense, que SDL3 soporta.
- La API de Lua expone acciones, no botones, para que los modos de juego y los mods funcionen con cualquier
  dispositivo.

## Consecuencias

- Se añade el módulo `input` a la capa de sistemas. `platform` entrega los eventos de los dispositivos; `input` los
  convierte en acciones.
- Toda la UI y todos los avisos se diseñan para ratón y para mando a la vez.
- La CI prueba la entrada con los mandos virtuales de SDL, porque no tiene mandos físicos.

## Alternativas descartadas

- **Leer botones directamente en el juego:** ata cada sistema a un dispositivo concreto e impide reasignar controles.
- **Solo XInput:** deja fuera los mandos de PlayStation, Nintendo y genéricos.
