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

## Dar un objeto en el campo

Usar un objeto de la bolsa en el campo no lo aplica al momento: juega la escena del original (`game_menu.c:
startFeedingItem`, `partner.c: partnerTickFeedItem`, en `feed_scene.cpp`). El menú se cierra y el jugador queda quieto
mientras el tiempo corre (`Field::holdForCare`). El compañero anda hasta la distancia a la que toma objetos: se acerca si
está lejos y se aleja si está cerca (`Field::walkPartnerToRange`, `checkEatDistance`). Esa distancia va por especie en
`field_items.feed.take_distance` (`ITEM_TAKE_DISTANCE`). El jugador le tiende el objeto y suena el sonido. Pasado el
cuadro 7, el compañero lo come y el objeto se aplica al acabar, o lo rechaza y el objeto queda en la bolsa. Mientras
dura, la muerte y la evolución esperan, porque el jugador no está libre. Sin el bloque `feed` del ruleset, la bolsa usa
el objeto al momento, como antes. `use_item` de la consola también lo usa al momento.

Diferencias con el original:

- El compañero decide si lo toma al elegir el objeto, no al tendérselo: es la misma tirada, un momento antes.
- El objeto en la mano del jugador no se dibuja (`renderFeedingItem`).
- Si el compañero se atasca contra una pared, se para donde está; el original sigue andando.

## El baño

Si el jugador pisa la casilla del baño y el compañero tiene ganas, el compañero va al baño del mapa (`FieldToilet`, en
`field_toilet.cpp`). Es la sección 0x4e2 del original: `call_partner_routine(0)` y `partner.c: tickPartnerToilet`. El
jugador queda quieto mientras corre el tiempo. La cámara va al baño en 20 cuadros. El compañero corre a los dos puntos
del baño (`TOILET_DATA`, que el paquete base trae en `maps.yaml`, `toilets`) y hace allí la animación del baño. Al
acabarla, el compañero hace sus necesidades (`handleToilet`), la cámara vuelve y el compañero vuelve andando al primer
punto. Entonces suena el sonido del baño. Sin `field_items.toilet_walk`, o en un mapa sin puntos de baño, el compañero
va donde está, como antes. Si se atasca de camino, se para donde está.

## Las cacas en el suelo

`FieldPoop` (`field_poop.cpp`) coloca en el mapa los montones que el compañero deja (`partner_impl.c: createPoopPile`):
con el campo libre, cada montón pendiente de la sesión (`PoopAdapter`) va a la casilla delante del compañero según hacia
dónde mira, o a una vecina al azar si ya hay uno. Cada fotograma pasa a `FieldScene::setProps` los montones del mapa: el
modelo de `poop_piles.look.model` en el centro de su casilla, a la altura del compañero y a la escala de su tamaño entre
`model_size` (`renderPoop`). Sin la sección `poop_piles` los montones se guardan pero no se ven.

## Muerte y renacer

`DeathScene` juega la vida perdida (`murd.c: MURD_tickScene`), la muerte (`dooa.c: DOOA_tickDissolve`) y el renacer
(`DOOA_tickRebirth`). El mapa donde despierta el tamer se elige al empezar la escena (`map.c: setDeathMap`) y se entra
en él a mitad (`changeToDeathMap`); la carga del mapa en segundo plano no existe aquí. Sin historia, o si la historia no tiene `story_scenes.death`, empieza con los eventos del compañero
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

La caja de nombres es la americana: dos mitades de 9 filas y 5 columnas (mayúsculas; minúsculas y dígitos), con
Atrás y Aceptar a su izquierda, y el cursor se mueve como en `namingSelectionUp/Down/Left/Right` (arriba y abajo dan
la vuelta en cada mitad; a la derecha de la última columna se pasa a la otra mitad o a Atrás; desde un botón se entra
por la fila de la Z). Las direcciones repiten al mantenerlas (a los 6 fotogramas y luego cada 2). Confirmar escribe
la letra (un espacio no puede ser la primera); con el nombre lleno reescribe la última y el cursor va a Aceptar;
cancelar borra la última, y con el nombre lleno borra dos, como la versión americana; menú lleva a Aceptar. A la vez se
puede teclear: las letras de la rejilla y el espacio se escriben (otra tecla suena rechazada) e Intro acepta el
nombre si se ha tecleado algo desde el último movimiento del cursor. El nombre del jugador empieza vacío, como en la
versión americana.

Los sonidos son los del original (`playSound`, banco 0, sección `sounds`): cursor 2, confirmar 3 (una página, una
elección, una letra, borrar), rechazo 0xB (cancelar una elección, porque la escena tiene puesto el disparador 49; un
nombre vacío; un espacio primero). La escena no tiene música. El texto aparece por páginas enteras, como en el
original, que dibuja la página oculta una fila por fotograma y la muestra de golpe: no hay texto letra a letra en
ningún cuadro del juego. Mientras se dibuja, la escena no atiende pulsaciones.

El nombre propio del compañero sale de `PartnerView::nickname` (los nombres de la partida): en los diálogos, en la
página de estado del menú del compañero (con la especie en su propia línea, `game_menu.c: drawDigimonStatsStrings`)
y en las ranuras de partida (`nickname`, junto a la especie, `main_menu.c: drawMainMenuStrings`). El combate, el HUD
y las escenas de evolución y de muerte muestran la especie o ningún nombre, como el original.

Diferencias con el original: en la caja de nombres no hay botón para llevar el cursor a Atrás (○) ni para borrar
todo el nombre (□); las letras son de media anchura y no las de ancho fijo de la fuente del original; el mensaje de
enfermedad y los registros de combate y de la arena, que en el original nombran al compañero, aún no existen.

## Exploración libre

`free_exploration.cpp` lleva la [exploración libre](../../architecture/warps-de-script.md#exploración-libre):
la enciende el ajuste `free_exploration` (menú de pausa, guardado en los ajustes del usuario) o `--explore` (solo en
esa ejecución). Cuando el campo para en una casilla con varios sitios, abre `ui/destination_choice` con los sitios
(`exploration_place_name.N`, `exploration_place_map.N`, `exploration_places`, `exploration_story`) y pasa al campo
lo que se elige (`choose_destination`) o quedarse (`stay_here`).
