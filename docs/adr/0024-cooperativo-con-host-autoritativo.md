# 0024. Cooperativo en línea con un host autoritativo y jugadores movidos por controladores

- **Estado:** Aceptada para implementación
- **Fecha:** 2026-11-16

## Contexto

Se quiere que un segundo jugador se conecte a la partida de otro y jueguen juntos en el mismo mundo. Hoy el campo
tiene un solo jugador fijo, las pantallas leen el teclado directamente, la cámara del render alimenta la simulación y
las posiciones del campo son floats, que no está garantizado que den lo mismo en dos máquinas. El combate sí es
determinista, y la red del versus (WebRTC, señalización con códigos de sala, saludo con hash de contenido) ya existe.
El detalle está en [Multijugador cooperativo en línea](../architecture/multijugador.md).

## Decisión

Un PC, el **host**, simula el mundo entero y es la autoridad. El cliente manda comandos numerados; el host simula el
movimiento de ambos jugadores. El cliente puede predecir visualmente solo su propio movimiento y reconciliarlo contra
snapshots completos no fiables a 20 Hz. La predicción no activa efectos de juego. Eventos y acciones críticas viajan
fiables, con acciones identificadas para deduplicación. Los dos jugadores son los mismos datos (`FieldPlayer`). Lo
que cambia es el `PlayerController` que les da `PlayerCommands` cada tick: local, remoto o scripteado. El transporte,
la señalización y el saludo son los del versus.

## Consecuencias

- Hay que sacar el estado por jugador de `Field`, pasar todas las pantallas a comandos y cortar la dependencia de la
  simulación con la cámara dibujada. Ese trabajo (fase C0) también mejora el juego de un jugador: autotests,
  repeticiones y entrada más limpia.
- El cliente no necesita simular el mundo ni coincidir bit a bit con el host. Las diferencias de floats entre
  máquinas no rompen la partida.
- La latencia del host se nota en interacciones y acciones críticas (NPCs, diálogos, combate); el movimiento local
  puede ocultarla visualmente, sin adelantar efectos de juego.
- Los snapshots iniciales son completos para evitar una base delta perdida. Los deltas se reconsideran solo con
  mediciones de ancho de banda y un protocolo explícito de recuperación.
- `packages/game/*` no se depende entre sí: `game/coop` contiene DTOs/códecs y máquina de enlace sobre `core`/`net`;
  `apps/game` compone el puente con `field`, `packages/session` y `FieldScene`. No se reutiliza directamente el tipo
  C++ `Identity` de `versus`, aunque se mantengan campos y validación de identidad compatibles.
- La cámara de seguimiento sigue siendo local; para el rescate del compañero cada cliente reporta al host un
  `PlayerViewState` lógico (rectángulo visible del fondo, mapa y secuencia). El host valida y usa ese metadato, nunca
  una posición propuesta por el cliente. Para escenas compartidas deriva el ancla del centro de la vista y los datos
  del mapa. Frecuencia acordada: hasta 20 Hz; una vista caduca tras 500 ms sin actualización. Los límites geométricos
  exactos se cierran antes de C0b.
- Para unirse hace falta un punto seguro (campo libre), porque el guardado no lleva el estado pasajero.
- Las tarjetas de salas y perfil online (#291–#295) pasan a ser base común del versus y del cooperativo.

## Alternativas descartadas

- **Lockstep del mundo entero (como el combate del versus):** el campo usa floats y aleatorios fuera de
  `ruleset::Random`; una desincronización entre máquinas rompería la partida y obligaría a volver el campo entero
  determinista.
- **Servidor dedicado:** coste de alojamiento y operación para un juego de dos jugadores; el proyecto quiere que los
  PCs hablen directamente, con un servidor solo para el primer contacto.
- **Multijugador local en una pantalla:** descartado por el proyecto.
- **Clase de jugador polimórfica (`IPlayer` local y remoto):** duplica la lógica del jugador; la diferencia está solo
  en de dónde salen los comandos.
