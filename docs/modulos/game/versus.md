# versus

El protocolo del versus en línea (`dwe::game::versus`, capa `game`): dos jugadores, cada uno en su máquina con los
compañeros registrados de su partida, juegan una serie de combates en lockstep. Depende de `core` y `net`; no conoce
el combate: la sesión ([session](../apps/session.md), `VersusSeries`) lo junta con `battle`, y las pantallas están en
el juego ([game](../apps/game.md), «Versus en línea»).

## Cómo va una serie

0. Los dos se conectan por WebRTC: el anfitrión crea una sala en el servidor de señalización y enseña su código de
   cuatro caracteres, el invitado lo escribe y los dos abren una conexión directa ([net](../engine/net.md),
   [versus-signal](../services/versus-signal.md)).
1. El invitado manda `Hello` con su identidad: versión del protocolo (2), versión de dwe y la suma del contenido.
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

`VersusLink` lleva todo esto sobre un `net::PeerLink`: los mensajes del versus y las sumas por el carril fiable, las
entradas del lockstep por el que no tiene garantías (cada 50 ms y en cada entrada nueva repite las que el otro no ha
confirmado, hasta que el combate acaba). Sus fases son `Connecting`, `Lineups`, `Playing`, `BetweenRounds`, `Finished`
y `Failed`; el fallo dice por qué: los del protocolo (`NoAnswer`, `Silence`, `Left`, `ProtocolMismatch`,
`VersionMismatch`, `ContentMismatch`, `BadLineup`, `Desync`, `Damaged`) y los de la conexión, que vienen de
`net::PeerFailure` (`NoServer`, `ServerUnreachable`, `WrongCode`, `RoomFull`, `TimedOut`, `NoRoute`, `Disconnected`).
Tiempos (`LinkTimeouts`): 10 s esperando la respuesta del anfitrión desde que la conexión abre y 8 s de silencio una
vez hablan.

## La red (`versus/network.yaml`)

`loadNetworkConfig` lee `versus/network` del pack del juego (esquema `versus_network.schema.json`):

```yaml
signal_server: ""                         # ws:// o wss://; vacío: no hay servidor y el juego lo dice
ice_servers: [stun:stun.cloudflare.com:3478]   # stun:, turn: o turns:
```

El servidor está vacío hasta que se despliegue el de señalización. `dwe-game --versus-server wss://…` lo cambia para
una partida (para probar uno). Nada de esto va en la partida guardada.

## La suma del contenido

`session::versusContentHash` es un SHA-1 sobre el id, la versión y la cadena del ruleset, el YAML de cada sección y
los bytes de cada tabla. Si los dos jugadores no tienen el mismo contenido (otro disco extraído, otro mod, otra
versión de las reglas), el anfitrión lo rechaza antes de empezar: nada del contenido viaja, solo la suma.

## Riesgos

- Sin TURN, dos redes sin ruta directa entre ellas no pueden jugar: el juego lo dice («no hay ruta directa») y sugiere
  otra red.

- El combate usa coma flotante en algunas cuentas (posiciones, giros). Con el mismo binario y la misma CPU da lo mismo;
  entre compiladores o sistemas distintos (Linux contra Windows) podría separarse. La suma por tick lo detecta y la
  serie para limpia con «desincronización»; no se ha probado aún entre un Linux y un Windows.

## Tests

`dwe_versus_tests` (etiqueta `versus`): los mensajes de ida y vuelta, la serie (`VS_initializeVS`), la bienvenida y
las alineaciones, el rechazo por versión o contenido, una serie entera en lockstep con un 33 % de pérdida, resultados
que no coinciden, silencio, sin respuesta, abandono, cada fallo de la conexión con su motivo y el `network.yaml`.
