# game

`dwe-game`, el juego: abre la ventana, monta los paquetes, empieza la partida (`apps/session`), dibuja el campo
(`FieldScene`) y las pantallas (`ui/*` del paquete `game`, con los modelos de vista de `apps/screens`) y corre los
autotests. Las opciones, los controles y cómo se recorren las rutas están en la
[guía de la prueba vertical](../../guides/prueba-vertical.md); la UI y sus comandos, en
[ui.md](../../architecture/ui.md).

## Semilla

`--seed <n>` da la semilla de una partida nueva (`SessionDesc::seed`): los números aleatorios del partner y los de los
sistemas salen de ella, así que una ruta con la misma semilla juega igual cada vez. Sin la opción, el juego toma una del
reloj del sistema y la escribe en el log (`game: new game, seed <n>`) para poder repetir la partida de un informe de
fallo. Con `--load` la opción no cambia nada: la partida cargada sigue con el estado de los números que guardó.

## El compañero en el campo

El campo dibuja al compañero con el modelo de su especie: `digimon/<especie>/model` del paquete base, con las
animaciones de `models/partner` del paquete `game`, que tienen los mismos ids en todos los compañeros
([modelos](../../architecture/models.md)). Al empezar, una partida cargada lo dibuja con la especie guardada; después,
el juego mira cada fotograma los eventos del compañero (`GameSession::takePartnerEvents`, en `partner_events.cpp`):
`Evolved` (una evolución, la forma virus o su vuelta) y `Reborn` cargan el modelo de la especie nueva
(`FieldScene::showPartnerSpecies`). El resto de eventos aún no se ven en pantalla (las reacciones del HUD llegan con
los guiones de los mapas) y solo se escriben en el log de depuración. Si el modelo de una especie no se puede cargar,
el compañero sigue con el que tenía y el log lo avisa.

`evolve("<especie>")`, en la consola y en los pasos `script` de las rutas, hace evolucionar al compañero como un guion
de mapa (`EvolutionAdapter::evolveByScript`); es una ayuda de depuración hasta que corran los guiones. La ruta
`tests/autotest/route-evolve.yaml` lo usa para comprobar que el modelo cambia (`wait: {partner_model: ...}`).

Hueco: el radio de colisión y las velocidades de andar y correr del compañero siguen siendo los de `field/rules`
(medidos en el compañero inicial), no los de la especie nueva (`DIGIMON_DATA[tipo].radius` y la raíz de sus
animaciones).

## Exploración libre

`free_exploration.cpp` lleva la [exploración libre](../../architecture/warps-de-script.md#exploración-libre):
la enciende el ajuste `free_exploration` (menú de pausa, guardado en los ajustes del usuario) o `--explore` (solo en
esa ejecución). Cuando el campo para en una casilla con varios sitios, abre `ui/destination_choice` con los sitios
(`exploration_place_name.N`, `exploration_place_map.N`, `exploration_places`, `exploration_story`) y pasa al campo
lo que se elige (`choose_destination`) o quedarse (`stay_here`).
