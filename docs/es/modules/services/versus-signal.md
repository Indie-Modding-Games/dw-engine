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

## Salas de hasta cuatro

El cooperativo sienta hasta cuatro jugadores (#352). El anfitrión lo pide con `/v1/host?seats=4` (3 o 4; cualquier
otro valor es la sala de dos de arriba, igual que siempre para los juegos antiguos) y recibe un token en su mensaje
`room`. La sala sigue abierta mientras el anfitrión espera: cada invitado se empareja con un socket del anfitrión que
espera, los dos se conectan como arriba y sus sockets se cierran cuando ambos dicen `done`, y el anfitrión abre su
siguiente socket con `/v1/host/<code>?token=&taken=<n>` para el siguiente invitado. Un invitado de más sobre
`seats - 1` recibe `full`; un anfitrión que perdió a un invitado dice `{"type":"seats","taken":n}` y la plaza vuelve a
quedar libre. La sala comprueba a su anfitrión cada 2 minutos, vive como mucho una hora y se cierra con el último socket
de su anfitrión. `/v1/info` incluye la capacidad `seats`.

Cuando la partida empieza el anfitrión lo dice en su cuenta de plazas, `{"type":"seats","taken":n,"playing":true,"open":true}`
(#352, #356). La sala sigue entonces mientras dure la partida (un día como mucho), su entrada de la lista muestra la
partida en curso (fase `playing`, estado `waiting` mientras tenga plaza libre y admita jugadores nuevos), y cuando cae el
último socket del anfitrión espera un minuto a que el anfitrión vuelva a abrir una plaza antes de cerrarse. Los
anfitriones que nunca mandan `playing` tienen la sala como antes.

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

El anfitrión manda `connected`, `ready`, `playing` y `closed` a `POST /v1/rooms/<código>/state` con su token (el juego
lo manda como un WebSocket que recibe una respuesta), y repite el estado de la sala cada minuto mientras dura. Una sala
que se cierra antes de que llegue un invitado sale de la lista con ella; una cuyos jugadores se encontraron sigue hasta
que el anfitrión dice `closed`, o 3 minutos después de su último aviso, así que un anfitrión que se colgó no deja salas
fantasma (#416). Un aviso tardío del vestíbulo nunca devuelve al vestíbulo una partida en marcha. Una sala de más
plazas cuyo anfitrión dejó su vestíbulo con invitados dentro sigue en la lista solo como llena: nadie puede entrar en
una sala que ya no existe.
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
