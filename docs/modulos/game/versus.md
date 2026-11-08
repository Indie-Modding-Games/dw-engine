# versus

El protocolo del versus en línea (`dwe::game::versus`, capa `game`): dos jugadores, cada uno en su máquina con los
compañeros registrados de su partida, juegan una serie de combates en lockstep. Depende de `core` y `net`; no conoce
el combate: la sesión ([session](../apps/session.md), `VersusSeries`) lo junta con `battle`, y las pantallas están en
el juego ([game](../apps/game.md), «Versus en línea»).

## Cómo va una serie

1. El invitado manda `Hello` con su identidad: versión del protocolo (1), versión de dwe y la suma del contenido.
2. El anfitrión la compara con la suya. Si algo difiere responde `Leave` con el motivo (versión, protocolo o
   contenido) y los dos lo enseñan; si no, `Welcome` con la serie: número de combates, arena, semilla y retraso de
   entrada (3 frames, 150 ms).
3. Cada lado manda su `Lineup`: un registro por combate (nombre, especie, PV, PM, ataque, defensa, velocidad,
   inteligencia, disciplina y tres técnicas), tal como lo guarda la máquina de registro.
4. Cada combate corre en las dos máquinas a la vez con `net::Lockstep`: solo viajan las órdenes de los jugadores. Con
   la misma semilla, los mismos registros y las mismas órdenes las dos simulaciones son iguales; la suma por tick lo
   comprueba y una diferencia para la serie como desincronización.
5. Al acabar cada combate los dos mandan `RoundOver` con su resultado, que tiene que coincidir. `Series` cuenta: se
   juegan todos los combates y gana quien gane más (`VS_initializeVS`); un combate en que caen los dos o se acaba el
   tiempo sin ganador cuenta como que pierden los dos.
6. `Leave` en cualquier momento termina la serie para el otro, con el motivo.

`VersusLink` lleva todo esto sobre un `net::ReliableChannel`. Sus fases son `Connecting`, `Lineups`, `Playing`,
`BetweenRounds`, `Finished` y `Failed`; el fallo dice por qué (`NoAnswer`, `Silence`, `Left`, `ProtocolMismatch`,
`VersionMismatch`, `ContentMismatch`, `BadLineup`, `Desync`, `Damaged`). Tiempos (`LinkTimeouts`): 10 s esperando la
respuesta del anfitrión y 8 s de silencio una vez hablan.

## La suma del contenido

`session::versusContentHash` es un SHA-1 sobre el id, la versión y la cadena del ruleset, el YAML de cada sección y
los bytes de cada tabla. Si los dos jugadores no tienen el mismo contenido (otro disco extraído, otro mod, otra
versión de las reglas), el anfitrión lo rechaza antes de empezar: nada del contenido viaja, solo la suma.

## Riesgos

- El combate usa coma flotante en algunas cuentas (posiciones, giros). Con el mismo binario y la misma CPU da lo mismo;
  entre compiladores o sistemas distintos (Linux contra Windows) podría separarse. La suma por tick lo detecta y la
  serie para limpia con «desincronización»; no se ha probado aún entre un Linux y un Windows.

## Tests

`dwe_versus_tests` (etiqueta `versus`): los mensajes de ida y vuelta, la serie (`VS_initializeVS`), la bienvenida y
las alineaciones, el rechazo por versión o contenido, una serie entera en lockstep sobre un enlace malo, resultados que
no coinciden, silencio, sin respuesta y abandono.
