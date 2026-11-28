# Auditoría de #359: diagnóstico y pruebas de consistencia del mundo compartido

Revisado el 2026-11-27 sobre `wave/w135-integrate` (oleada w146). Tabla completa con la evidencia en la
[versión en inglés](../../../architecture/audits/359-audit.md).

| # | Criterio | Antes | Después |
|---|---|---|---|
| 1 | Se puede consultar la posición autoritativa de cada jugador | Parcial | Hecho |
| 2 | Se puede comparar el estado recibido por cada cliente | Hecho | Hecho |
| 3 | Se identifican actualizaciones perdidas o fuera de orden | Hecho | Hecho |
| 4 | Se registran los cambios de escenario | Hecho | Hecho |
| 5 | Se pueden reproducir pruebas con dos, tres y cuatro participantes | Hecho | Hecho |
| 6 | Se identifican diferencias persistentes | Hecho | Hecho |
| 7 | Las herramientas pueden desactivarse en builds de producción | Parcial | Hecho |

Antes: 5 Hecho, 2 Parcial, 0 Falta. Después: 7 Hecho.

Cambios: el informe da el sitio de cada invitado según su último estado aceptado por la autoridad (no su cuerpo
dibujado 100 ms por detrás); cada línea del log lleva la hora y el tick del mundo del anfitrión; `coop_sync("trace")`
registra cada informe jugador por jugador; la opción de CMake `DWE_COOP_SYNC` (apagada por defecto en un paquete) deja
el diagnóstico fuera de la build; y hay pruebas nuevas con 2, 3 y 4 jugadores donde solo diverge el invitado que se
queda sin instantáneas, también sobre enlaces reales de un `CoopRoom`.

Pendiente: el flujo de `PlayerState` de los invitados no se vigila por pérdidas (sus números saltan a propósito; se
vigilan los comandos); sin probar entre dos máquinas reales.
