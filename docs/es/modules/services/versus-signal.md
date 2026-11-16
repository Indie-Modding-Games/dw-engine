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
- El invitado abre `/v1/join/<código>` (sin mayúsculas, espacios ni guiones) y recibe `joined`; el anfitrión recibe
  `peer` y empieza la oferta.
- `signal` se reenvía tal cual al otro. Cuando los dos mandan `done` (ya están conectados) la sala se cierra; si solo
  lo manda uno, a los 10 s.
- Errores, y la sala se cierra: `no-room`, `full`, `left` (el otro se fue antes de conectar) y `expired` (una sala vive
  como mucho 5 minutos). Un código que no puede ser uno se rechaza sin buscar sala.

El Durable Object usa la API de hibernación de WebSockets: una sala esperando no gasta mientras nadie habla.

## Estado

Desplegado en Cloudflare (Workers con Durable Objects) en `wss://dwe-versus-signal.regamelabs.workers.dev`, la dirección que trae `content/packs/game/versus/network.yaml` (`signal_server`). `wrangler.toml` no lleva cuenta, ruta ni
secretos: se despliega con `npx wrangler deploy` y la cuenta de quien despliega. Más adelante podrá dar credenciales TURN
de corta vida para las redes sin ruta directa.

## Tests

`node --test` en `apps/versus-signal` (Node 22.18 o posterior; en CI, en el trabajo del lanzador): las salas con
sockets y estado falsos (pareja, reenvío, cierre, caducidad, sala llena, código que no existe, invitado que se va) y las
rutas del Worker. El juego prueba su cliente contra un servidor en memoria con las mismas reglas
([net](../engine/net.md#tests)).
