# Navegador de salas cooperativas para cuatro jugadores

Estado: **implementado** (2026-11-20, #352; ver [multijugador](multiplayer.md) §5.9 y §5.10). Una diferencia con este diseño: en vez de un `peerId` en cada mensaje sobre un único socket, el anfitrión abre un socket por plaza (`/v1/host/<code>?token=&taken=`) y la sala empareja a cada invitado con uno; así cada señal solo llega a su pareja y el protocolo de dos jugadores no cambia. Diseño original (2026-11-17): El límite propuesto es cuatro jugadores en total: un anfitrión y hasta tres invitados. La lista contiene salas cooperativas activas, no servidores dedicados; el mundo se sigue simulando en el anfitrión y el tráfico de juego viaja por WebRTC.

## Estado actual y motivo del cambio

El Worker `apps/versus-signal` solo empareja un anfitrión y un invitado mediante un código efímero. No enumera salas. `RtcPeerLink` representa un solo par WebRTC, `CoopLink` asigna únicamente el jugador remoto 1, y `FieldScene::addCoopGuest` agrega un solo actor. Cambiar el número en la UI o aceptar tres sockets en el Worker produciría una sala que el juego no puede simular.

## Contratos que se implementarán

### Sala pública

`RoomListing` llevará `roomId`, `joinCode`, `name`, `message`, `hostName`, `players`, `capacity`, `createdAt` y `state`. `players` estará entre 1 y 4; `state` será `waiting`, `full` o `starting`. El directorio no guardará nombres de guardados, identidad de compañeros ni estado del mundo. Nombre y mensaje tendrán límites de longitud, se tratarán como texto y usarán valores predeterminados. Una sala desaparece al cerrarse o expirar; una sala completa se muestra deshabilitada.

### Autoridad y conexiones

El anfitrión conserva autoridad única. Cada invitado necesita un enlace WebRTC independiente con el anfitrión, una identidad asignada en el rango 1..3 y su propio buffer de comandos. El host procesa comandos de los tres invitados dentro del mismo tick autoritativo y publica el estado a cada enlace. No se usará malla completa entre clientes: no añade autoridad y multiplica conexiones.

La señalización debe enrutar cada oferta/respuesta/candidato por `peerId`; no se difundirá SDP a todos los miembros de la sala. La capacidad se confirma atómicamente al reservar el asiento y se libera en salida, desconexión y expiración. Reintentos usan un `requestId` para no reservar asientos duplicados.

## Archivos y modificaciones previstas

| Área | Archivos | Cambio |
|---|---|---|
| Directorio de salas | `apps/versus-signal/src/worker.ts`, `room.ts`, nuevo `directory.ts`, `wrangler.toml` | DO de directorio; registro/actualización/liberación de salas y asientos; endpoint WebSocket versionado para listar y reservar; limpieza TTL. Migración de binding Durable Object. |
| Señalización cliente | `packages/engine/net/include/dwe/net/signal.hpp`, `rtc_peer_link.hpp`, `src/signal.cpp`, `src/rtc_peer_link.cpp` | DTO/cliente asíncrono de directorio y ruteo de señalización por participante. Mantener el transporte WebRTC y errores de red existentes. |
| Protocolo coop | `packages/game/coop/include/dwe/game/coop/protocol.hpp`, `src/protocol.cpp`, `src/link.cpp` | Protocolo nuevo versionado; `MaxPlayers=4`, asignación explícita de id, identidad/remitente validado por enlace y mensajes de lobby/listo/salida. No aceptar paquetes de un id distinto al asignado. |
| Runtime/app | `apps/game/src/coop_connection.*`, `coop_screen.*`, `coop_gameplay_bridge.*`, `coop_commands.*`, `game.cpp` | Propiedad de tres enlaces invitados, lista/refresh, mensajes del estado de sala, reserva/salida, handshake individual y fan-out del estado. El anfitrión inicia solo cuando cada participante conectado está listo y tiene perfil válido. |
| Simulación | `packages/game/field/include/dwe/game/field/field.hpp`, `src/field*`; `apps/game/src/field_scene.*`, `play_session.*` | Colección de jugadores indexada por `PlayerId`; alta/baja segura de ids 1..3, comandos independientes, cámara local y compañeros asociados al dueño. Sistemas del mundo siguen ejecutándose una vez por tick. |
| UI y textos | `content/packs/game/ui/main_menu/{document.rml,logic.lua,structure.rcss}`, `content/packs/game/localization/{es,en}/main_menu.yaml` | Pantalla de búsqueda, actualización, filas de sala, mensaje, plazas ocupadas, estado/errores, crear sala con nombre/mensaje y vista de lobby para anfitrión/invitados. Acciones visibles según rol. |
| Documentación | `docs/architecture/multiplayer.md`, `apps/versus-signal/README.md`, `docs/modules/services/versus-signal.md` | Reflejar topología, privacidad del directorio, límites, despliegue coordinado y guía manual. |

## Orden de implementación

1. **Contratos y ciclo de vida del directorio:** definir DTOs, límites y transiciones; hacer que creación, reserva, salida y expiración sean atómicas. La sala es la fuente de verdad de capacidad.
2. **Señalización multi-peer:** añadir `peerId` a los mensajes y verificar que cada señal llega solo a su pareja anfitrión/invitado. Mantener compatibilidad aislada con el endpoint actual durante la transición; no mezclar dos versiones dentro de una sala.
3. **Protocolo y runtime host:** cambiar de un `CoopLink` singular a una colección indexada; validar el id asignado y neutralizar por separado el input expirado de cada jugador.
4. **Simulación para cuatro jugadores:** permitir cuatro `FieldPlayer` en un mapa compartido, con propietario y compañero por jugador; mantener cámara y entrada local en cada instancia.
5. **UI del navegador:** listar solo salas `waiting`, mostrar `players/capacity` y mensaje, refrescar bajo demanda y periódicamente; reservar asiento antes del handshake; mostrar errores y permitir reintentar sin perder selección.
6. **Arranque de partida:** anfitrión elige guardado y decide iniciar; cada invitado aporta su perfil de compañero. El mundo/progreso y cambios de mapa siguen perteneciendo al host.
7. **Despliegue:** desplegar el Worker con migración y endpoint nuevo antes de distribuir el cliente. La lista no funcionará contra el Worker anterior; presentar un error de incompatibilidad claro.

## Verificación prevista

- Directorio: crear/listar/actualizar/reservar/liberar; límite 4; dos reservas simultáneas para el último asiento; TTL y sala fantasma.
- Worker: señalización dirigida, aislamiento entre parejas, no filtrar salas cerradas, respuestas acotadas y errores estructurados.
- Protocolo: versión incompatible, id no asignado, paquete malformado, duplicados y salida de cada invitado.
- Runtime/field: tres buffers de comando independientes, jugadores ids 0..3, compañeros correctos, una sola actualización del mundo por tick y desconexión de un invitado sin afectar a los demás.
- Manual: cuatro instancias, lista, sala llena, mensajes, entrada/salida, desconexión durante reserva, inicio autoritativo y guardado local de cada invitado intacto.

## Riesgos y límites explícitos

- La lista hace públicas la existencia, el nombre, el mensaje y la ocupación de una sala. No publicar perfiles ni progreso.
- Un directorio central puede ser punto caliente; limitar frecuencia de refresco, tamaño de respuesta y número de salas anunciadas, con paginación o filtro si crece.
- La capacidad de cuatro no debe habilitarse en UI hasta completar protocolo, runtime y simulación. El orden anterior evita anunciar plazas que no se pueden usar.
- El despliegue del Worker es una operación separada del cambio local y requiere acceso a la cuenta Cloudflare correspondiente.
