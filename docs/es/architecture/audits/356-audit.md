# Auditoría de #356: escenarios y posiciones por participante

Versión corta; el detalle está en [la auditoría en inglés](../../../architecture/audits/356-audit.md).

| Criterio | Antes | Después |
|---|---|---|
| Cada participante tiene un escenario actual | Hecho | Hecho |
| Las transiciones se comunican | Hecho | Hecho |
| Los puntos de aparición se sincronizan | Hecho | Hecho |
| No se aplican posiciones de escenarios anteriores | Hecho | Hecho |
| Jugadores en escenarios distintos sin corromper la sesión | Hecho | Hecho |
| La llegada actualiza las representaciones | Hecho | Hecho |
| Punto 21 de UX: aviso «el anfitrión cambió de zona» antes del fundido | Falta | Hecho (`CoopGuestWorld::ZoneLeadTicks`, `online.coop.host_changed_zone`) |

Sin verificar: el momento del aviso frente al fundido real con dos máquinas.
