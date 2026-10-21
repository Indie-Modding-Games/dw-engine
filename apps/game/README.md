# game

`dwe-game`, el juego: abre la ventana, monta los paquetes, empieza la partida (`apps/session`), dibuja el campo
(`FieldScene`) y las pantallas (`ui/*` del paquete `game`, con los modelos de vista de `apps/screens`) y corre los
autotests. Las opciones, los controles y cómo se recorren las rutas están en la
[guía de la prueba vertical](../../docs/guides/prueba-vertical.md); la UI y sus comandos, en
[ui.md](../../docs/architecture/ui.md).

## Semilla

`--seed <n>` da la semilla de una partida nueva (`SessionDesc::seed`): los números aleatorios del partner y los de los
sistemas salen de ella, así que una ruta con la misma semilla juega igual cada vez. Sin la opción, el juego toma una del
reloj del sistema y la escribe en el log (`game: new game, seed <n>`) para poder repetir la partida de un informe de
fallo. Con `--load` la opción no cambia nada: la partida cargada sigue con el estado de los números que guardó.
