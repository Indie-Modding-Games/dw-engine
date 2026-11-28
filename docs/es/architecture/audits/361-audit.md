# Auditoría de #361: dificultad cooperativa

Auditado el 2026-11-27 sobre `wave/w135-integrate` con la propuesta de #426 (+20 % plano por jugador, §12). La tabla
completa, con la evidencia, está en la [versión en inglés](../../../architecture/audits/361-audit.md).

## Entregables

| Entregable | Antes | Después |
|---|---|---|
| Análisis de la dificultad original | Hecho | Hecho |
| Sistemas afectados por el multijugador | Hecho | Hecho |
| Comparativa de estrategias de escalado | Hecho | Hecho |
| Parámetros de dificultad para 1 a 4 jugadores | Parcial | Hecho |
| Encuentros y jefes con ajustes especiales | Parcial | Parcial |
| Propuesta de implementación técnica | Hecho | Hecho |
| Riesgos para la progresión y la economía | Hecho | Hecho |
| Escenarios de prueba | Hecho | Hecho |

## Criterios de aceptación

| Criterio | Antes | Después |
|---|---|---|
| Análisis fundamentado de la dificultad original | Hecho | Hecho |
| Desequilibrios del multijugador identificados | Hecho | Hecho |
| Modelo de escalado para hasta cuatro jugadores | Hecho | Hecho |
| Encuentros normales, jefes y eventos especiales distinguidos | Hecho | Hecho |
| Impacto en la progresión individual y compartida | Hecho | Hecho |
| Parámetros modificables sin alterar el original | Hecho | Hecho |
| Recomendación técnica para implementarlo y probarlo | Parcial | Hecho |

Queda por hacer: verificar en el original los cuatro jefes de §8 (Myotismon, Devimon, Mamemon, Piximon) antes de
ajustarlos y construir las sobrescrituras por combate (§9). La sala ya muestra el nivel del anfitrión; no se tocó código
de balance.
