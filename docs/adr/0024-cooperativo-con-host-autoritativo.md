# 0024. Cooperativo en línea con un host autoritativo y jugadores movidos por controladores

- **Estado:** Propuesta
- **Fecha:** 2026-11-16

## Contexto

Se quiere que un segundo jugador se conecte a la partida de otro y jueguen juntos en el mismo mundo. Hoy el campo
tiene un solo jugador fijo, las pantallas leen el teclado directamente, la cámara del render alimenta la simulación y
las posiciones del campo son floats, que no está garantizado que den lo mismo en dos máquinas. El combate sí es
determinista, y la red del versus (WebRTC, señalización con códigos de sala, saludo con hash de contenido) ya existe.
El detalle está en [Multijugador cooperativo en línea](../architecture/multijugador.md).

## Decisión

Un PC, el **host**, simula el mundo entero y es la autoridad. El otro, el **cliente**, manda sus comandos (y, en la
primera versión, la posición de su propio jugador, que el host valida) y dibuja lo que el host le replica: snapshots
no fiables a 20 Hz y eventos fiables. Los dos jugadores son los mismos datos (`FieldPlayer`). Lo que cambia es el
`PlayerController` que les da `PlayerCommands` cada tick: local, remoto o scripteado. El transporte, la señalización y
el saludo son los del versus.

## Consecuencias

- Hay que sacar el estado por jugador de `Field`, pasar todas las pantallas a comandos y cortar la dependencia de la
  simulación con la cámara dibujada. Ese trabajo (fase C0) también mejora el juego de un jugador: autotests,
  repeticiones y entrada más limpia.
- El cliente no necesita simular el mundo ni coincidir bit a bit con el host. Las diferencias de floats entre
  máquinas no rompen la partida.
- La latencia del host se nota en todo lo que no es el movimiento propio (NPCs, diálogos, combate). Con dos jugadores
  y una conexión directa es aceptable.
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
