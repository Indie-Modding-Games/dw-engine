# Servidor de señalización del versus

Un Worker de Cloudflare con un Durable Object por sala que presenta a los dos jugadores del versus en línea
(`docs/modulos/game/versus.md`). Solo hace de intermediario mientras los dos juegos se conectan: el anfitrión crea una
sala y recibe un código de cuatro caracteres, el invitado lo escribe, y el servidor reenvía entre los dos la oferta, la
respuesta y los candidatos de WebRTC hasta que abren su conexión directa. Después cierra la sala; la partida no pasa por
el servidor. TypeScript sin dependencias.

```
anfitrión ──WebSocket /v1/host──►  sala K7Q2  ◄──WebSocket /v1/join/K7Q2── invitado
    ▲                              (reenvía)                                   ▲
    └──────────────── conexión directa WebRTC (canales de datos) ──────────────┘
```

## Protocolo

Mensajes de texto JSON por el WebSocket. La versión va en la ruta (`/v1/`).

| Quién | Mensaje | Qué significa |
|---|---|---|
| servidor → anfitrión | `{"type":"room","code":"K7Q2"}` | La sala está creada; el anfitrión enseña el código. |
| servidor → invitado | `{"type":"joined"}` | Ha entrado en la sala. |
| servidor → anfitrión | `{"type":"peer"}` | Ha llegado el invitado: el anfitrión empieza la oferta. |
| cualquiera → servidor | `{"type":"signal","data":{…}}` | Se reenvía tal cual al otro (`data` es la descripción `{"description","type"}` o un candidato `{"candidate","mid"}`). |
| cualquiera → servidor | `{"type":"done"}` | Ya está conectado. Cuando lo dicen los dos, el servidor cierra la sala; si solo lo dice uno, la cierra a los 10 s. |
| servidor → cualquiera | `{"type":"error","reason":…}` | Y cierra: `no-room` (no hay sala con ese código), `full` (ya hay dos), `left` (el otro se fue antes de conectar), `expired` (pasaron 5 minutos). |

- Los códigos usan `23456789ABCDEFGHJKLMNPQRSTUVWXYZ` (sin 0, 1, O ni I) y se leen sin mayúsculas, espacios ni guiones.
  Si el código que sale ya está en uso, el Worker prueba otro.
- Una sala vive como mucho 5 minutos (alarma del Durable Object); al cerrarse borra su estado y el código queda libre.
- Un mensaje de más de 64 KiB, binario o que no sea JSON se ignora.

El cliente del juego está en `packages/engine/net/signal.hpp` (protocolo) y `rtc_peer_link.hpp` (WebRTC con
libdatachannel).

## Pruebas

```sh
cd apps/versus-signal && node --test     # Node 22.18 o posterior: ejecuta el TypeScript sin compilarlo
```

`test/room.test.ts` prueba las salas con sockets y estado falsos; `test/worker.test.ts`, las rutas del Worker. El
juego tiene su propio servidor falso en memoria (`packages/engine/net/tests/fake_signal_server.hpp`) con las mismas
reglas, con el que dos pares reales de libdatachannel se conectan en una prueba sin red.

## Desplegarlo

Está desplegado en `wss://dwe-versus-signal.regamelabs.workers.dev`. `wrangler.toml` no lleva cuenta, ruta ni secretos;
se despliega con la cuenta de quien lo hace:

```sh
cd apps/versus-signal
npx wrangler dev            # local: ws://localhost:8787
npx wrangler deploy         # con la cuenta y la ruta que toque
```

y pone la dirección (`wss://…`) en `content/packs/game/versus/network.yaml` (`signal_server`), o la da al juego con
`--versus-server wss://…` para probarla. Sin dirección el juego dice que no hay servidor configurado.

Más adelante: servidores TURN para las redes en las que no hay ruta directa. El servidor daría credenciales de corta
vida junto con `room`/`joined`, y el juego las añadiría a `ice_servers`.
