# versus-signal

El servidor de señalización del [versus en línea](../game/versus.md): un Worker de Cloudflare con un Durable Object por
sala (`apps/versus-signal`, TypeScript sin dependencias). Solo presenta a los dos jugadores: reenvía la oferta, la
respuesta y los candidatos de WebRTC entre los dos hasta que abren su conexión directa, y cierra la sala. La partida no
pasa por él.

Protocolo completo, pruebas y cómo desplegarlo: el
[README del servidor](https://github.com/Indie-Modding-Games/dw-engine/blob/main/apps/versus-signal/README.md).

```text
anfitrión ──WebSocket /v1/host──►  sala K7Q2  ◄──WebSocket /v1/join/K7Q2── invitado
    ▲                              (reenvía)                                   ▲
    └──────────────── conexión directa WebRTC (canales de datos) ──────────────┘
```

## Salas

- El anfitrión abre `/v1/host` y recibe `room` con un código de cuatro caracteres (`23456789ABCDEFGHJKLMNPQRSTUVWXYZ`,
  sin 0, 1, O ni I). Si el código que sale está en uso, el Worker prueba otro.
  La sala es privada: solo `/v1/host?public=1` la pone en la lista pública (`GET /v1/rooms`).
- El invitado abre `/v1/join/<código>` (sin mayúsculas, espacios ni guiones) y recibe `joined`; el anfitrión recibe
  `peer` y empieza la oferta.
- `signal` se reenvía tal cual al otro. Cuando los dos mandan `done` (ya están conectados) la sala se cierra; si solo
  lo manda uno, a los 10 s.
- Errores, y la sala se cierra: `no-room`, `full`, `left` (el otro se fue antes de conectar) y `expired` (una sala vive
  como mucho 5 minutos). Un código que no puede ser uno se rechaza sin buscar sala.

El Durable Object usa la API de hibernación de WebSockets: una sala esperando no gasta mientras nadie habla.

## Salas públicas

Un segundo Durable Object, el directorio, guarda las salas públicas. Un anfitrión público dice quién es
(`/v1/host?public=1&name=…&icon=…&compat=…&mode=…`) y recibe un token con `room`. La lista
(`GET /v1/rooms?compat=…`) solo trae salas de la misma clave de compatibilidad (versión del juego y contenido), nunca el
token ni la dirección, y deja fuera las que ya juegan. La fase de cada sala:

| Fase | Cuándo |
|---|---|
| `open` | el anfitrión espera a un invitado |
| `connected` | llegó el invitado (la sala sale llena) |
| `ready` | el anfitrión dice que los dos eligieron con qué juegan |
| `playing` | el anfitrión dice que empezó la partida: fuera de la lista |

El anfitrión manda `ready`, `playing` y `closed` a `POST /v1/rooms/<código>/state` con su token (el juego lo manda como
un WebSocket que recibe una respuesta). Una sala que se cierra antes de que llegue un invitado sale de la lista con ella;
una cuyos jugadores se encontraron sigue hasta que el anfitrión dice `closed`, o 10 minutos después de su último aviso.
`GET /v1/info` dice lo que sabe hacer el servidor (`public-rooms`, `room-state`, y `relay` cuando tiene TURN).

Límites: 50 salas públicas abiertas como mucho, una por dirección, seis salas por minuto y cinco lecturas de la lista
cada diez segundos por dirección (rate limiting de Workers), y un apodo de letras, cifras, espacios y `. _ ' -` (16 como
mucho). Un anfitrión que los pasa recibe `error` con `limit` o `bad-name`; el juego lo dice como «el servidor rechazó la
sala».

## Relevo (TURN)

Con los secretos `TURN_KEY_ID` y `TURN_KEY_API_TOKEN` (una clave TURN de Cloudflare Realtime), `TURN_ANALYTICS_TOKEN`
(lectura de Account Analytics) y `CLOUDFLARE_ACCOUNT_ID`, el Worker pide a Cloudflare credenciales que duran 2 horas,
marcadas con el código de la sala (`customIdentifier`), y las manda con `room` y `joined` como `iceServers`. El juego
las suma a sus servidores ICE, así dos jugadores sin ruta directa se conectan por el relevo. Solo se usan relevos UDP:
la biblioteca WebRTC del juego no hace relevo por TCP ni TLS. El «solo por relevo» del perfil en línea conecta solo por
el relevo, así el otro jugador nunca sabe la dirección de esta máquina; sin relevo esa sala falla con `no_relay`.

El relevo nunca cuesta dinero: antes de dar credenciales el Worker lee los bytes relevados este mes en el GraphQL
Analytics de la cuenta (un solo agregado de `callsTurnUsageAdaptiveGroups`, guardado unos 10 minutos en el
almacenamiento del directorio) y deja de darlas cuando llegan a `TURN_MONTHLY_CAP_GB` (`wrangler.toml`, 800 de los 1000
GB gratis al mes). Falla cerrado: un secreto que falta, un error o un timeout de la analítica significa sin
credenciales. Entonces, como sin TURN o si Cloudflare no responde, las salas se conectan directo (STUN) igual que
antes. Cómo activarlo (los pasos del panel, los comandos `wrangler secret put` y un aviso de facturación como alerta de
respaldo) está en el README del servidor.

## Estado

Desplegado en Cloudflare (Workers con Durable Objects) en `wss://dwe-versus-signal.regamelabs.workers.dev`, la dirección que trae `content/packs/game/versus/network.yaml` (`signal_server`). `wrangler.toml` no lleva cuenta, ruta ni
secretos: el workflow Signal server (`.github/workflows/signal.yml`) lo despliega cuando un cambio suyo entra en main, o
a mano desde Actions. Para trabajar en él, `npx wrangler dev` en `apps/versus-signal` y el juego con
`--versus-server ws://localhost:8787`; versus y co-op usan esa dirección. Los secretos de TURN siguen entre
despliegues, así que el workflow no cambia por ellos.

## Tests

`node --test` en `apps/versus-signal` (Node 22.18 o posterior; en CI, en el trabajo del lanzador): las salas con
sockets y estado falsos (pareja, reenvío, cierre, caducidad, sala llena, código que no existe, invitado que se va), las
rutas del Worker, el directorio (lista, avisos de estado, caducidad, límites) y TURN con la API de Cloudflare simulada. El juego prueba su cliente contra un servidor en memoria con las mismas reglas
([net](../engine/net.md#tests)).
