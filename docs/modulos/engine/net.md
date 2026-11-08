# net

La red entre dos máquinas (`dwe::net`, capa `base`): un enlace entre dos pares con un carril fiable y otro sin
garantías, WebRTC para conectarlos por internet, lockstep para simulaciones deterministas y el cliente del servidor de
señalización. Depende de `core` y, por dentro, de libdatachannel; no sabe nada del juego. Lo usa el versus en línea
([versus](../game/versus.md)).

## Piezas

| Pieza | Qué hace |
|---|---|
| `PeerLink` | Un extremo de la conexión con el otro par, con dos carriles (`Lane`): `Reliable` (cada mensaje llega una vez y en orden) y `Unreliable` (puede llegar tarde, desordenado o nunca). Nada bloquea: `poll(now)` lo mueve y los mensajes esperan en él. Dice su estado (`Connecting`, `Open`, `Failed`), por qué falló (`PeerFailure`), cuándo oyó al otro por última vez, el código de la sala y si el otro ya llegó a ella |
| `RtcPeerLink` | El enlace de verdad: WebRTC con canales de datos (libdatachannel). `host(settings, open)` crea una sala en el servidor de señalización y ofrece; `join(settings, code, open)` entra con el código y responde. Intercambian sus candidatos ICE por el servidor y, cuando los dos canales están abiertos, le dicen al servidor que cierre la sala y siguen de par a par. Carril fiable: canal `control` (fiable y ordenado); sin garantías: canal `inputs` (desordenado, sin reenvíos). Manda un byte `0x00` cada 250 ms por `inputs` para que el otro sepa que sigue ahí |
| `RtcSettings` | El servidor de señalización (`ws://` o `wss://`; vacío = no hay), los servidores ICE (`stun:`, más adelante `turn:`) y cuánto pueden tardar los dos en abrir una ruta una vez están en la sala (30 s) |
| `SignalClient`, `SignalSocket`, `openWebSocket` | El lado del juego del protocolo de señalización ([versus-signal](https://github.com/Indie-Modding-Games/dw-engine/tree/main/apps/versus-signal)): rutas `/v1/host` y `/v1/join/<código>`, mensajes JSON `room`, `peer`, `joined`, `signal`, `done` y `error`. El socket es el cliente WebSocket de libdatachannel; los tests ponen uno en memoria |
| `normalizedRoomCode` | Los códigos de sala: cuatro caracteres de `23456789ABCDEFGHJKLMNPQRSTUVWXYZ` (sin 0, 1, O ni I, que se confunden). Acepta minúsculas, espacios y guiones; lo que no puede ser un código no se manda |
| `DatagramLink`, `ChannelPeerLink` | Un enlace de datagramas sin garantías (ninguno pasa de `MaxDatagram`, 1200 bytes) y un `PeerLink` encima: un byte de carril delante y un `ReliableChannel` para el carril fiable. Lo usa el enlace de pruebas |
| `LoopbackNetwork` | Dos pares simulados en un proceso para los tests (`link(side)`, código de sala `TEST`), con su reloj (`advance`) y `LinkConditions`: latencia, variación (que desordena), pérdida y duplicados con semilla, y `cut()` para un par que se va sin avisar |
| `ReliableChannel` | Mensajes que llegan una vez y en orden sobre un `DatagramLink`: número de secuencia, acuse del último en orden más una máscara de los 32 siguientes, reenvío a los 120 ms sin acuse y un acuse vacío cada 250 ms en silencio |
| `Lockstep` | Una simulación determinista en dos máquinas: cada lado manda solo sus entradas, programadas `inputDelay` ticks por delante; un tick se juega cuando se conocen las dos. Las entradas van por el carril sin garantías y cada mensaje repite todas las que el otro aún no ha confirmado (hasta 32) junto con el acuse de las suyas, así que perder o desordenar mensajes no para el combate. Tras cada tick los dos mandan una suma de su estado por el carril fiable y el primer tick en que no coinciden es una desincronización que para la partida. Los mensajes llevan el número de combate (`run`): los de otro combate se ignoran |
| `WireWriter` / `WireReader` | Enteros little-endian, textos con longitud y lectura que falla limpio si el mensaje es corto |

Los mensajes del lockstep empiezan con `0x40` (entradas) y `0x41` (suma); los del versus usan `0x10`-`0x14`.

## Fallos

`PeerFailure` dice por qué un enlace no abrió o se cerró, y el versus enseña un mensaje para cada uno:

| Fallo | Cuándo |
|---|---|
| `NoServer` | El juego no tiene servidor de señalización configurado |
| `ServerUnreachable` | No se pudo conectar con el servidor, o cortó la conexión antes de tiempo |
| `WrongCode` | No hay sala con ese código (o ya caducó) |
| `RoomFull` | La sala ya tiene a sus dos jugadores |
| `TimedOut` | La sala caducó esperando al invitado, o los dos no abrieron una ruta en 30 s |
| `NoRoute` | ICE falló: no hay ruta directa entre las dos redes (hace falta TURN, que aún no hay) |
| `Closed` | La conexión se cerró después de abrirse |

Un aviso de «desconectado» de WebRTC no cierra el enlace: puede volver solo, y el silencio del versus (8 s) decide.

## Por qué WebRTC y libdatachannel

La primera versión usaba UDP directo con la dirección IP en el código de unión: por internet había que abrir un puerto
en el router. WebRTC atraviesa la mayoría de los NAT con STUN (`stun:stun.cloudflare.com:3478`, en
`versus/network.yaml`), cifra con DTLS y da los dos tipos de canal que hacen falta. libdatachannel (MPL-2.0) es una
librería pequeña en C++ con API en C, sin la pila de medios de un navegador, y trae su propio cliente WebSocket para
la señalización. Se usa su API en C (`rtc.h`) porque el proyecto compila sin excepciones.

- **TLS con Mbed TLS.** El port de vcpkg usa OpenSSL, que no compila en la imagen de Linux (le faltan las cabeceras
  del kernel) ni en la compilación cruzada a Windows. `tooling/vcpkg-ports/libdatachannel` es el port de vcpkg con
  `USE_MBEDTLS=ON`, y `tooling/vcpkg-ports/mbedtls` el de Mbed TLS con DTLS-SRTP activado (lo pide libdatachannel).
  Solo cambian los ficheros de build de los ports; el código de las librerías no se toca.
- **Licencias.** libdatachannel y libjuice son MPL-2.0: se pueden enlazar sin cambiar la licencia del proyecto
  mientras no se modifiquen sus ficheros, y el aviso de terceros dice dónde está su código. usrsctp (BSD-3), plog
  (MIT), nlohmann-json (MIT) y Mbed TLS (Apache-2.0) entran en el mismo aviso. Los textos están en
  `scripts/package/licenses/` y los junta `scripts/package/third_party_notices.py`.
- **El cliente WebSocket no comprueba el certificado del servidor** con Mbed TLS (libdatachannel no lo hace con esa
  librería). Lo peor que puede hacer un servidor falso es presentar a los dos jugadores a otro par o no presentarlos:
  la partida va cifrada por DTLS de par a par, y el versus comprueba versión y contenido antes de empezar. Si hace
  falta más, se puede fijar el certificado o cambiar de librería TLS.

## Límites

- Sin TURN: dos redes que no dejan ruta directa (algunos NAT simétricos, redes de empresa) acaban en `NoRoute`. El
  servidor podría dar más adelante credenciales TURN de corta vida, y `ice_servers` ya es una lista de datos.
- El servidor de señalización no está desplegado todavía: hasta que alguien ponga su dirección en
  `versus/network.yaml` (o el jugador use `--versus-server`), el versus en línea dice que no hay servidor.

## Tests

`dwe_net_tests` (etiqueta `net`): el canal sobre un enlace limpio y sobre uno malo (latencia, pérdida, duplicados y
desorden), reenvío, silencio y corte, mensajes ajenos y demasiado grandes; el lockstep de dos pares con un 25 % de
pérdida, la repetición y el acuse de las entradas, mensajes de otro combate, los ticks sin entrada, la
desincronización en su tick y los mensajes dañados; los códigos de sala; el cliente de señalización contra un servidor
en memoria con las reglas del de verdad (pareja y reenvío, código que no existe, sala llena, servidor caído, invitado
que se va); y dos `RtcPeerLink` de verdad que se conectan en la misma máquina por ese servidor en memoria (sin red ni
STUN) y hablan por los dos carriles.
