# Auditoría de #365: entrenar en un mundo compartido

Auditado el 2026-11-27 sobre `wave/w135-integrate` (protocolo 14), con el arreglo de #411 que hace que una sesión dure
su hora del reloj del mundo. La tabla completa, con la evidencia, está en la
[versión en inglés](../../../architecture/audits/365-audit.md).

## Entregables

| Entregable | Antes | Después |
|---|---|---|
| Análisis del entrenamiento original | Parcial | Hecho |
| Comparativa de modelos multijugador | Hecho | Hecho |
| Propuesta para 2 a 4 jugadores | Parcial | Hecho |
| Integración con el reloj global | Hecho | Hecho |
| Autoridad y sincronización | Parcial | Hecho |
| Reglas de progresión individual | Hecho | Hecho |
| Diagramas de flujo | Parcial | Hecho |
| Riesgos y casos límite | Parcial | Hecho |
| Propuesta técnica | Hecho | Hecho |
| Desglose en tickets | Falta | Hecho |

## Criterios de aceptación

De nueve criterios, seis estaban Hechos y tres Parciales (ganancias individuales y su sincronización, desconexiones e
interrupciones, plan sin tocar Classic Enhanced). Los tres quedan Hechos con §4.7, la tabla de casos de §5 y los
tickets de §9 del [diseño](../coop-training.md).

## Código hecho aquí

Una sesión cooperativa cortada (cancelar o los rodillos del intento extra) cobraba una hora al compañero mientras el
mundo corría segundos. Ahora el curso espera la hora del mundo (`GameSession::courseHourRuns`, `GymPorts::hourRuns`),
lo mismo que #411 §5.

## Pendiente

Los tickets T7 a T10 del diseño, ninguno necesario para los criterios. Sin verificar en dos máquinas reales.
