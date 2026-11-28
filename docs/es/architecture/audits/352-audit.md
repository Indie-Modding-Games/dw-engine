# Auditoría de #352: sesiones de hasta cuatro jugadores

Revisado el 2026-11-27 sobre `feat/battle-modes` (oleada w144). Tabla completa con la evidencia en la
[versión en inglés](../../../architecture/audits/352-audit.md).

| # | Criterio | Antes | Después |
|---|---|---|---|
| 1 | Crear salas de capacidad cuatro | Hecho | Hecho |
| 2 | Unirse con una, dos o tres plazas ocupadas | Hecho | Hecho |
| 3 | Rechazar conexiones con la sala completa | Hecho | Hecho |
| 4 | La interfaz muestra los cuatro espacios | Hecho | Hecho |
| 5 | Los estados de conexión se actualizan | Hecho | Hecho |
| 6 | Los cuatro entran en la misma partida | Hecho | Hecho |
| 7 | Identidad y control propios de cada jugador | Hecho | Hecho |
| 8 | Los cuatro coexisten en un mismo mapa | Hecho | Hecho |
| 9 | La sincronización funciona entre los cuatro | Hecho | Hecho |
| 10 | La salida de cualquiera no rompe la sesión de los demás | Parcial | Hecho |
| 11 | Ningún sistema depende de exactamente dos jugadores | Parcial | Hecho |
| 12 | Las partidas de uno y dos jugadores siguen funcionando | Hecho | Hecho |
| 13 | Las funciones multijugador funcionan con cuatro | Parcial | Hecho |

Antes: 10 hechos y 3 parciales. Después: 13 hechos.

Qué cambió: el `BattleBegin` acepta como quien empezó el combate a cualquier jugador (antes el anfitrión no podía
enviar un combate empezado por el jugador 2 o 3), y un invitado conserva los cuerpos de otro jugador que se fue
mientras su compañero sigue peleando en el combate del anfitrión.

Pendiente: varios objetos lanzados a la vez se dibujan como uno solo (interfaz de combate); el objeto de un ayudante sale
de su bolsa antes de que el anfitrión acepte el lanzamiento (combates compartidos, #351); un invitado sin nombre de
perfil se numera por fila en la sala y por id en la etiqueta del campo; falta probar con cuatro máquinas reales.
