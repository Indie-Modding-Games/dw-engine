# Online: salas, búsqueda de partida y perfil (propuesta)

Propuesta de diseño para el issue #289 («Modo Online»). Nada de esto está hecho: es lo que se acordaría antes de
programar, y termina con las tarjetas en que se divide. Es de la fase 5 (lo que no es del original) y todo va como
**Mejora opcional**: el versus de hoy, con código de sala, sigue igual y es el de siempre.

## Resumen

- Se propone **salas públicas con lista y «Buscar partida»**, no un modo campaña en línea. La campaña compartida
  necesitaría sincronizar el mundo entero entre dos máquinas, algo que el juego no hace de forma determinista; queda
  fuera.
- El servidor de señalización gana **un directorio de salas públicas** (un Durable Object más) con límites contra el
  abuso. La partida sigue siendo de par a par: el servidor solo presenta a los jugadores.
- El **perfil en línea es local**: un apodo, un icono y un récord guardados en la configuración del juego, sin
  cuentas ni datos en el servidor.
- Los **estados de la sala** (Conectado, Listo, Jugando) se modelan como un enum explícito en el cliente y en el
  servidor cuando llegue el directorio. Espectador se deja para el final, como investigación.
- Cabe en el plan gratuito de Cloudflare con mucho margen (cuentas abajo). TURN, para redes que no dejan ruta
  directa, tiene 1000 GB gratis al mes y va en su propia tarjeta.

## Estado actual

| Pieza | Cómo es hoy |
|---|---|
| Emparejar | Por código de sala: el anfitrión crea la serie y recibe un código de cuatro caracteres; el invitado lo escribe ([versus-signal](../modulos/services/versus-signal.md)) |
| Servidor | Un Worker de Cloudflare con un Durable Object por sala (`apps/versus-signal`). Reenvía la oferta, la respuesta y los candidatos de WebRTC y cierra la sala cuando los dos están conectados. Una sala vive como mucho 5 minutos; no hay lista de salas ni datos guardados |
| Conexión | WebRTC de par a par con libdatachannel ([net](../modulos/engine/net.md)): un carril fiable y otro sin garantías, STUN de Cloudflare, sin TURN. El certificado del servidor se comprueba (#273) |
| Partida | Lockstep: cada máquina manda solo sus entradas y las dos simulan lo mismo. Antes de empezar se comprueban la versión del protocolo y una suma del contenido (`versusContentHash`): dos jugadores con distinto contenido no juegan |
| Cliente | `VersusFlow` (`apps/game/src/versus_flow.hpp`) es el flujo sin UI, con un enum explícito de pantallas (`View`: menú, preparar la sala, esperar, código, elegir partida y compañeros, combate, resultado, error). `VersusScreen` lo pone en el documento `ui/versus` |
| Perfil | No hay: cada jugador lleva su nombre y sus compañeros de la partida que elige. El versus no escribe nada en la partida, como el original |

El versus del original era de dos mandos en una consola; aquí es de dos máquinas por internet con las mismas reglas
([game](../modulos/apps/game.md), «Versus en línea»). Esa es la base que no se toca.

## Las propuestas de la comunidad

De los comentarios del issue:

1. **Pantalla de búsqueda de partida y de salas**, con maquetas: una lista de salas abiertas con su anfitrión y su
   estado, y un botón para buscar partida.
2. **Un perfil de jugador en línea** común a todas las partidas («que conserve la experiencia sin importar el
   mundo»), algo personalizable para que no todos se vean iguales. Sin prioridad: primero salas y un en línea estable.
3. **Estados de la sala separados**: Conectado, Listo, Jugando y Espectador, para añadir modos sin convertir las
   salas en un montón de condiciones.
4. **Redes malas** (universidades, cafeterías): WebRTC sin TURN no siempre conecta; se mencionó coturn.

## Qué se propone

### Salas públicas y «Buscar partida»

- Al crear una sala, el anfitrión elige si es **privada** (como hoy: solo con el código) o **pública** (sale en la
  lista). Privada es lo que sale marcado.
- La pantalla «Salas públicas» lista las salas **compatibles** (misma versión del protocolo y mismo contenido) que
  esperan invitado: apodo e icono del anfitrión, número de combates, arena y cuánto lleva esperando. Se refresca cada
  pocos segundos y se entra con aceptar; por debajo es el mismo `join` con el código de la sala.
- **«Buscar partida»** entra en la sala pública compatible más antigua o, si no hay ninguna, crea una pública y espera.
  No hay emparejamiento por nivel: no hay datos fiables para hacerlo (el perfil es local) y con pocos jugadores
  dividirlos deja salas vacías.
- Las salas que ya juegan no salen en la lista (o salen como «Jugando», sin poder entrar) mientras no haya
  espectadores.

### Estados de la sala

Un enum explícito en el cliente (`RoomPhase` o similar, dentro de una clase de sesión de sala que use `VersusFlow`)
y el mismo en el directorio del servidor:

| Estado | Qué significa | Quién lo cambia |
|---|---|---|
| Abierta | El anfitrión espera invitado (en la lista si es pública) | El servidor al crear la sala |
| Conectado | Los dos están en la sala y abren su ruta WebRTC | El servidor cuando llega el invitado |
| Listo | Cada jugador eligió partida y compañeros (hoy, el paso `WaitingLineup`) | Cada cliente, por el carril fiable; el anfitrión avisa al directorio |
| Jugando | La serie está en marcha | El anfitrión avisa al directorio; la sala sale de la lista |
| Cerrada | Terminó, alguien se fue o caducó | El servidor (o su alarma) |
| Espectador (después) | Un tercero mira la serie | Ver «Espectadores» |

Hoy el servidor cierra la sala en cuanto los dos conectan; con el directorio, el anfitrión mantendría un WebSocket
ligero abierto (con hibernación, que no cuesta duración) solo para avisar de «Jugando» y «Cerrada», o lo avisaría con
una petición HTTP corta en cada cambio. La segunda opción es más simple y es la recomendada.

`VersusFlow::View` ya es un enum explícito por pantalla y hoy no hay banderas sueltas que sobren, así que no se
refactoriza nada ahora: el estado de la sala se separa de la pantalla cuando llegue el directorio (tarjeta 2).

### Perfil en línea

- **Local**: apodo (hasta 16 caracteres de un alfabeto limitado), un icono de una lista fija y un récord de victorias
  y derrotas en línea, guardados en la configuración del juego, no en la partida (el original no guarda nada del
  versus). Se manda al otro jugador al empezar y al directorio en las salas públicas.
- **Sin cuentas ni datos en el servidor**: una experiencia guardada en el servidor necesitaría identidad, contraseñas o
  inicio de sesión, protección de datos y moderación; y aun así un récord que manda el propio cliente se puede
  falsear. No compensa para un versus entre amigos.
- La «experiencia común a todos los mundos» se queda en el récord local. Si un día hace falta un perfil que no se
  pueda falsear, se parte de una clave por instalación (sin cuenta) y de resultados firmados por los dos jugadores;
  queda fuera de esta propuesta.

### Espectadores

Mirar una serie significa recibir las entradas de los dos jugadores y simular lo mismo. Con lockstep de par a par
hay dos caminos: que el anfitrión reenvíe las entradas a un tercer par (más carga y más subida para el anfitrión) o un
relevo en el servidor (un WebSocket por espectador que recibe lo que el anfitrión manda; cuesta peticiones y duración
de Durable Object mientras dura la serie). Ninguno es pequeño, así que va como investigación en la última tarjeta.

### Redes difíciles (TURN)

Cloudflare Realtime TURN da credenciales de corta vida desde una API con una clave que solo tiene el servidor: el
Worker las pediría al crear o unirse a la sala y las mandaría con `room` y `joined`; el juego las añade a sus
`ice_servers` (ya es una lista de datos en `versus/network.yaml`). Cuesta 0,05 USD por GB que sale de Cloudflare al
cliente, con 1000 GB gratis al mes; una serie mueve muy poco (entradas de pocos bytes, 20 por segundo), así que
cientos de miles de series caben en lo gratuito. coturn propio necesitaría un servidor pagado y mantenido; no se
recomienda. Va en su propia tarjeta porque no depende de las salas.

## Lo que necesita el servidor

### Directorio de salas

- Un Durable Object **`Lobby`** (uno solo; si un día no basta, uno por clave de compatibilidad) que guarda las salas
  públicas abiertas: código, apodo e icono del anfitrión, clave de compatibilidad (versión del protocolo y los primeros
  bytes de la suma del contenido), número de combates, arena, estado y hora de creación.
- Rutas nuevas, sin tocar las de hoy:
  - `GET /v1/rooms?compat=<clave>`: las salas abiertas compatibles, como mucho 50, las más antiguas primero.
  - `/v1/host?public=1&...`: crea la sala como hoy y la apunta en el directorio.
  - `POST /v1/rooms/<código>/state` (solo el anfitrión, con un token que recibe al crear la sala): «Jugando» o
    «Cerrada».
  - `GET /v1/info`: qué sabe hacer el servidor (`{"features":["public-rooms"]}`), para que el juego enseñe la opción
    solo si el servidor la tiene.
- Las salas de hoy siguen igual: la sala avisa al directorio al cerrarse o caducar (5 minutos esperando, como ahora);
  una sala que juega se borra de la lista y su entrada caduca sola si el anfitrión no vuelve a avisar.

### Límites contra el abuso

- **Por IP** (`CF-Connecting-IP`) con el binding de Rate Limiting de Workers: crear salas, como mucho unas pocas por
  minuto; pedir la lista, una vez cada pocos segundos. El binding cuenta por ubicación de Cloudflare y es aproximado,
  suficiente para esto.
- **Una sala pública abierta por IP** a la vez, que el directorio comprueba.
- **Textos acotados**: el apodo, de un alfabeto y una longitud fijos, y validado en el servidor; sin títulos de sala
  libres ni chat, para no tener que moderar nada. El cliente puede ocultar un apodo que el jugador marque.
- **Tamaños y tiempos** como hoy: mensajes de 64 KiB como mucho, salas que caducan, lista de 50 como máximo.
- **Privacidad**: WebRTC de par a par enseña la IP de cada jugador al otro, ya pasa con el código de sala. Con salas
  públicas se juega con desconocidos, así que la pantalla lo avisa, y con TURN se podría ofrecer «solo por relevo»
  (`iceTransportPolicy: relay`) para quien no quiera enseñar su IP.

### Coste en el plan gratuito de Cloudflare

El plan gratuito da 100 000 peticiones al día a Workers y otras 100 000 a Durable Objects (los mensajes de WebSocket
entrantes cuentan 1 de cada 20) y 13 000 GB-s de duración al día; las salas con hibernación solo cuentan duración
mientras procesan un mensaje.

| Qué | Peticiones (aprox.) |
|---|---|
| Emparejar dos jugadores (dos WebSocket, unos 30 mensajes de señalización, el aviso de estado) | ≈ 10 |
| Un jugador mirando la lista 2 minutos, refrescando cada 5 s | ≈ 50 (Worker y Durable Object) |
| 500 series al día con 2 minutos de lista por jugador | ≈ 55 000 |

Hay margen para cientos de series diarias. Si se acerca al límite, primero se alarga el refresco de la lista o se
cachea unos segundos en el Worker; el plan de pago (5 USD al mes) sube los límites mucho.

## Cómo convive con el versus de dos jugadores de siempre

- **Nada cambia por defecto.** «Versus en línea» sigue con «Crear sala» y «Unirse con código». Las filas «Salas
  públicas» y «Buscar partida» solo salen con la Mejora **«Salas públicas en línea»** activada (desactivada por defecto,
  documentada en `docs/modulos/apps/game.md` como las otras Mejoras) y si el servidor dice que las sabe hacer
  (`/v1/info`).
- **Mismo protocolo de partida.** Las salas públicas solo cambian cómo se encuentran los jugadores: el `join` por código,
  la comprobación de versión y contenido, el lockstep y las reglas del versus (las del original) no se tocan.
- **Protocolo de señalización compatible.** `/v1/host` y `/v1/join/<código>` siguen igual; lo nuevo son rutas y
  parámetros que un juego antiguo no usa. Un servidor viejo sin `/v1/info` se trata como «sin salas públicas».
- **El perfil es opcional**: sin apodo, se usa el nombre del jugador de la partida elegida, como hoy.

## Fuera de alcance

- Campaña o mundo compartido en línea.
- Cuentas, ranking global y emparejamiento por nivel.
- Chat y títulos de sala libres.
- Más de dos jugadores por serie.

## Tarjetas

1. **Directorio de salas públicas en el servidor**
   - Durable Object `Lobby` con las salas abiertas por clave de compatibilidad; rutas `GET /v1/rooms`,
     `/v1/host?public=1`, `POST /v1/rooms/<código>/state` (con token del anfitrión) y `GET /v1/info`.
   - La sala avisa al directorio al cerrarse o caducar; la lista quita las que juegan.
   - Límites: Rate Limiting por IP al crear y al listar, una sala pública abierta por IP, apodo validado, 50 salas como
     máximo.
   - Cierre: tests de `apps/versus-signal` (`node --test`) para las rutas nuevas, la caducidad y cada límite; las rutas
     de hoy no cambian (sus tests siguen pasando) y el README del servidor documenta el protocolo nuevo.
2. **Estado de sala explícito en el cliente**
   - Una clase de sesión de sala con un enum (`RoomPhase`: Abierta, Conectado, Listo, Jugando, Cerrada) que
     `VersusFlow` usa en lugar de deducir la fase de la pantalla y del enlace.
   - El cliente de señalización aprende `/v1/info`, la lista y el aviso de estado (`signal.hpp`), con el servidor en
     memoria de los tests al día.
   - Cierre: tests de `net` y `game` con las transiciones (incluidas las de error) sobre el servidor en memoria; el
     versus por código se juega igual (el test de las dos máquinas sigue pasando).
3. **Pantalla «Salas públicas» y «Buscar partida» (Mejora opcional)**
   - Opción «Salas públicas en línea», desactivada por defecto; las filas nuevas solo con ella y con un servidor que
     las sepa hacer.
   - Lista refrescada de salas compatibles, entrar con aceptar, crear sala pública o privada, «Buscar partida»; el aviso
     de privacidad.
   - Cierre: test del flujo sobre la red simulada (listar, entrar, buscar sin salas y crear) y capturas de la pantalla;
     documentado en `docs/modulos/apps/game.md` como Mejora.
4. **Perfil en línea local**
   - Apodo, icono de una lista fija y récord de victorias y derrotas, en la configuración del juego (no en la
     partida).
   - Se manda al otro jugador en la bienvenida del versus y al directorio en las salas públicas; sin apodo, el nombre
     de la partida como hoy.
   - Cierre: test de guardar y leer el perfil y de que viaja en la bienvenida; la partida no cambia (test de que el
     versus no escribe la partida).
5. **TURN con credenciales de corta vida**
   - El Worker pide credenciales a Cloudflare Realtime TURN (clave como secreto de Wrangler) y las manda con `room` y
     `joined`.
   - El juego las añade a sus `ice_servers`; opción de «solo por relevo» para quien no quiera enseñar su IP.
   - Cierre: tests del Worker con la API de TURN simulada y del cliente con las credenciales; una prueba manual entre
     dos redes que hoy acaban en `NoRoute`.
6. **Espectadores (investigación)**
   - Comparar reenvío por el anfitrión y relevo en el servidor: carga, coste en el plan gratuito y retraso.
   - Prototipo del que salga mejor detrás de una opción de desarrollo.
   - Cierre: nota de diseño con la decisión y sus números; prototipo que mira una serie de principio a fin sobre la red
     simulada.
