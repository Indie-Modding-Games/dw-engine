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

## Muerte y renacer

`DeathScene` juega la vida perdida (`murd.c: MURD_tickScene`), la muerte (`dooa.c: DOOA_tickDissolve`) y el renacer
(`DOOA_tickRebirth`). Sin historia, o si la historia no tiene `story_scenes.death`, empieza con los eventos del compañero
y retiene el campo. Con ella, la historia pide cada escena con `call_partner_routine` (9 y 7, vida perdida; 3, muerte;
4, renacer) y `StorySceneGlue` se la pasa: el campo sigue y lo retiene el guion. La muerte acaba con el compañero
ya ido, en negro. Entonces corre la sección del mapa de despertar: los textos, la caja de los huevos (`EggBoxScreen`,
documento `ui/egg_box`) y la rutina 4, que da al compañero su forma nueva y juega el renacer. Izquierda y derecha cambian
de huevo y confirmar elige uno; no se puede salir sin elegir. Lo que no está: los sprites de los huevos en VRAM
(`EGG_ICONS`), la animación de la caja y la carga del overlay; el documento enumera los huevos.

## Apertura de la partida nueva

Una partida nueva empieza como el original (`main.c: main`, estado 0): las preguntas de Jijimon, la película y el
primer mapa. `NewGameInterview` porta `newGameScene` (`main.c`), `loadNewGameScene` y `tickNewGameJijimon`
(`new_game.c`) y `newGameStateMachine` (`shop/script_menu.c`), con la sección `new_game_scene` del ruleset: Jijimon
entra andando sobre negro y, al girarse, pregunta si el jugador tiene Digivice, después día o noche (si dice que sí)
o si tiene amigos (si dice que no); la primera respuesta da Agumon y la segunda Gabumon. Luego pide los dos nombres
en la caja de nombres (`NamingBox`, documento `ui/naming_box`), con su confirmación, y se funde a negro. El
resultado va en `PlayStart::choice` hasta `GameSession::startNewGame`. Las frases son las de la tabla 1238 de
MAPHEAD y se ven en el documento `ui/story_textbox`. La misma caja de nombres sirve a la rutina 32 de los guiones
(renombrar al jugador o al compañero, `StoryNaming`). No corre en un autotest, como las películas.

Con la historia y `story_scenes.new_game.chain: original`, el campo empieza en `twna01` salida 9: la cabecera del
mapa carga el guion 164 y la casilla de esa salida corre la apertura, que lleva a `topn01`, el despertar en casa de
Jijimon, y de ahí a File City (todo dentro de la etapa 1 del candado de historia). `seeded` sigue disponible: el
estado de `data/new_game_story` y el inicio de `field/warps`, sin la apertura.

Diferencias con el original: el texto sale entero (no letra a letra); la caja de nombres es una sola página (las
dos mitades de la americana) y no admite teclear con el teclado; no suenan los cursores ni las confirmaciones de la
escena; no se enciende el disparador 49 mientras dura (el original lo apaga al final); el nombre del compañero sale
en los diálogos, no aún en los menús ni en el HUD.

## Exploración libre

`free_exploration.cpp` lleva la [exploración libre](../../architecture/warps-de-script.md#exploración-libre):
la enciende el ajuste `free_exploration` (menú de pausa, guardado en los ajustes del usuario) o `--explore` (solo en
esa ejecución). Cuando el campo para en una casilla con varios sitios, abre `ui/destination_choice` con los sitios
(`exploration_place_name.N`, `exploration_place_map.N`, `exploration_places`, `exploration_story`) y pasa al campo
lo que se elige (`choose_destination`) o quedarse (`stay_here`).
