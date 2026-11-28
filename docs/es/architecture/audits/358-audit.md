# Auditoría de #358: interpolación y suavizado de estados remotos

Revisado el 2026-11-27 sobre `wave/w135-integrate` (oleada w146). Tabla completa con la evidencia en la
[versión en inglés](../../../architecture/audits/358-audit.md).

| # | Criterio | Antes | Después |
|---|---|---|---|
| 1 | Se mantiene un búfer de estados recientes | Hecho | Hecho |
| 2 | Las posiciones remotas se interpolan entre actualizaciones | Hecho | Hecho |
| 3 | Se manejan los paquetes retrasados | Parcial | Hecho |
| 4 | Se evitan saltos visuales innecesarios | Parcial | Hecho |
| 5 | Se contempla la pérdida temporal de actualizaciones | Parcial | Hecho |
| 6 | Los movimientos bruscos y teletransportes legítimos se dibujan bien | Parcial | Hecho |
| 7 | La interpolación no altera la autoridad | Hecho | Hecho |

Antes: 3 Hecho, 4 Parcial, 0 Falta. Después: 7 Hecho.

Cambios: un estado tardío que aún va por delante de la reproducción ocupa su sitio; uno rezagado del mapa anterior ya
no devuelve la pista ni el dibujo del invitado a ese mapa; tras una pérdida la pista alcanza en los 100 ms de retraso
en vez de saltar; y un paso más rápido de lo que nada se mueve (2500 unidades por segundo más 64, en el reloj del
emisor) se dibuja como salto, así que un salto dentro de un mapa o un script que coloca a un personaje ya no se
desliza.

Pendiente: un teletransporte escondido en una pérdida larga se dibuja como la recuperación de 100 ms; un
teletransporte propio del invitado más lejos de lo que permite la autoridad lo rechaza `WorldAuthority::accept` (fuera
de #358); sin probar entre dos máquinas reales.
