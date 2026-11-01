# Jugar la prueba vertical y pasar sus autotests

La prueba vertical (fase 2, [especificación](../plan/fase-2-prueba-vertical.md)) es el campo jugable: File City y
la casa del anciano (`twna01` y `room08`), con el jugador, el compañero que lo sigue, el menú de pausa y el minimapa.
Todo sale de los paquetes por asset id: el paquete `base` que extrae `dwx extract` del disco del usuario y el
paquete `game` del repositorio (`content/packs/game`: reglas del campo, warps, modelos de los personajes y UI).

## Lo que hace falta

- Un build: `scripts/dev.sh build` (preset `linux-clang-debug` por defecto, con la capa de validación de Vulkan) o
  `PRESET=linux-clang-release scripts/dev.sh build` para medir rendimiento.
- El contenido extraído: una carpeta con el paquete `base` (`dwx extract`, ver
  [extract.md](../formats/extract.md)). El juego monta los paquetes de `--content` y los de su carpeta `packs/`; si
  ninguno es el paquete `game`, añade los del repositorio (`content/packs`), así que basta con la carpeta extraída.

## Jugar

```sh
scripts/dev.sh run --content <carpeta con el paquete base>
```

El juego empieza donde dice `field/warps` del paquete `game` (`start`: `twna01`, salida 1). Opciones:

| Opción | Qué hace |
|---|---|
| `--content <carpeta>` | Paquetes del juego (por defecto `content`) |
| `--mods <carpeta>` | Más paquetes, como mods; se puede repetir. La VFS los apila por capa y prioridad. Un paquete de terceros va solo en una carpeta de mods o en `--mods`, nunca en la carpeta de contenido ni en su `packs/`: todo lo que hay ahí se trata como contenido del propio motor |
| `--dev-assets` | Usa también los assets locales del fichero `dev-assets.yaml`, con un aviso en pantalla mientras está activo; solo para desarrollo ([assets de desarrollo](../architecture/assets-de-desarrollo.md)) |
| `--ruleset <id>` | El ruleset con el que se juega, `rules/<id>/` de los paquetes (por defecto `original`, ver [rulesets](../architecture/rulesets.md)) |
| `--story-stage <id>\|all` | La etapa del [candado de historia](../modulos/apps/session.md) que se juega, en vez del `active_stage` del ruleset; `all` lo abre todo (desarrollo y pruebas) |
| `--size <ancho>x<alto>` | Tamaño de la ventana (por defecto 1280x720) |
| `--fullscreen` | Empieza a pantalla completa |
| `--no-vsync` | Presenta cada frame en cuanto está listo (para medir fps) |
| `--start <mapa>[,<salida>]` | Empieza en otro mapa y salida |
| `--new-game-chain original\|seeded` | Cómo empieza una partida nueva, en vez de la cadena `story_scenes.new_game.chain` del ruleset: `original` corre la apertura desde `twna01` salida 9, `seeded` empieza en el campo libre con el estado de `data/new_game_story` ([game](../modulos/apps/game.md)). Un autotest usa `seeded` salvo que su ruta diga `new_game` |
| `--time day\|dusk\|night` | Pone el reloj del juego a las 8:00, 16:00 o 20:00 (por defecto, la hora de la partida: las 8:00 de la sección `clock` en una nueva) |
| `--new-game` | Empieza una partida nueva (es lo que hace sin `--load`): el partner, las estadísticas y el nivel de domador de la sección `new_game` del ruleset |
| `--load <hueco>` | Carga una partida guardada, `slot_01` o `autosave_01` de la carpeta de partidas, y empieza donde se guardó (salvo con `--start`) |
| `--clock-speed <x>` | Velocidad del reloj del juego: 1 la del original (un minuto de juego por segundo real), 60 una hora de juego por segundo real |
| `--seed <n>` | La semilla de los números aleatorios de una partida nueva (el partner y los sistemas: combates, objetos, evolución), para que una ruta juegue igual cada vez. Sin ella, el juego toma una del reloj y la escribe en el log (`game: new game, seed <n>`), así una partida se puede repetir. Con `--load` no cambia nada: la partida cargada sigue con los números que guardó |
| `--original-rolls <estado>` | Solo para el oráculo: los combates tiran con el generador del original (`rand()`) desde ese estado de 32 bits (decimal o `0x…`), el que tenía el original al empezar su pelea, así las primeras decisiones de los luchadores son las mismas en los dos lados. Sin ella, el motor tira con su propio generador |
| `--trace <fichero>` | Escribe una línea JSON por tick de lógica con lo que simula el juego (jugador, compañero, NPC, combatientes), para compararla con una traza del original ([traza de estado](traza-de-estado.md)). Sin ella no se escribe nada |
| `--control <socket>` | Abre el canal de depuración en ese socket Unix: una herramienta lee el estado y mueve el juego en marcha ([control remoto](control-remoto.md)). Con `--control-paused`, la lógica espera parada antes de su primer tick |
| `--saves <carpeta>` | Dónde lee y guarda las partidas (por defecto la carpeta de datos del usuario). Un autotest solo guarda con ella |
| `--reports <carpeta>` | Dónde guarda los [reportes de bug](#reportar-un-bug-f12) (por defecto `reports` en la carpeta de datos del usuario) |
| `--text-font modern\|original` | La fuente del texto con aspecto original (el nombre del mapa), en vez de la elegida en el menú principal o la de los paquetes; para autotests y capturas, no se guarda (ver [UI](../architecture/ui.md#aspecto-original)) |
| `--ui-skin <id>` | El aspecto de la interfaz (`original`, el de por defecto, `modern`, o uno de un mod), en vez del elegido en los menús o el de los paquetes; para autotests y capturas, no se guarda (ver [UI](../architecture/ui.md#aspectos)) |
| `--autotest <ruta.yaml>` | Recorre una ruta y sale con 0 si pasa (ver abajo) |
| `--shots <carpeta>` | Dónde guarda las capturas de un autotest |
| `--lang en\|es` | El idioma de esta ejecución, para autotests; no se guarda. Sin él, el juego usa el que se eligió en el menú principal o, la primera vez, el del sistema ([localización](../architecture/localizacion.md)) |
| `--explore` | Empieza con la [exploración libre](../architecture/warps-de-script.md#exploración-libre) encendida en esta ejecución, sin guardarlo; sin él, el juego usa el ajuste del menú de pausa (apagado por defecto) |
| `--skip-steps <tipo>[,<tipo>...]` | Salta los pasos de esos tipos (`fullscreen` en un X sin gestor de ventanas); el informe lo dice, y saltar `walk_to` o `wait` deja la ejecución en parcial (sale con 2) |

Las vistas de desarrollo siguen disponibles: `--test-scene`, `--map <id>`, `--model-viewer`, `--ui-demo`,
`--check-content <carpeta>` y `--dump-signs <mapa>[,<mapa>...] --shots <carpeta>` (las imágenes de un mapa con rejilla
para escribir los [carteles](carteles.md)).

### Controles

| | Teclado y ratón | Mando |
|---|---|---|
| Moverse (relativo a la cámara) | WASD o flechas | Stick izquierdo o cruceta |
| Andar en vez de correr | Mayúsculas (mantener) | Botón oeste (X en Xbox, cuadrado) o RB/R1 (mantener) |
| Ir a un punto | Clic izquierdo en el suelo; mantener y mover lo arrastra | |
| Olvidar el punto | Esc o clic derecho | Este (B, círculo) |
| Menú | Tab o el botón «Menú» del HUD | Norte (Y, triángulo) |
| Pausa (caja «Pause», todo se congela) | P o Pausa | Start |
| Aceptar / volver en el menú | Enter, Espacio o clic / Esc, Retroceso o clic derecho | Sur / Este |
| Consola de depuración | F1 o `º` | |
| Guardar un reporte de bug | F12 (acción `report_bug`) | |

Correr es lo normal, como en el original (allí R1 hacía andar). Los avisos de botones del HUD y del menú cambian al
último dispositivo usado. El menú para el campo mientras está abierto.

La lógica va a 60 ticks por segundo fijos; el dibujo interpola entre ticks y las animaciones avanzan por frame. Como
en el original, el reloj del juego avanza un minuto por segundo y el fondo cambia de paleta a las 6:00, 16:00 y
20:00, y al cruzar una puerta la pantalla se oscurece restando un gris durante un segundo mientras el jugador sigue
andando, con el nombre del mapa nuevo en el centro en la segunda mitad, y vuelve en otro segundo
([ficha del campo](../reference/campo.md)).

## Reportar un bug (F12)

Durante una partida, **F12** (la acción `report_bug`, que se puede reasignar como las demás) guarda un reporte en
`<datos del usuario>/reports/<AAAAMMDD-HHMMSS>-<mapa>/` (en Linux `~/.local/share/indie-modding-games/dwe/reports/`,
o la carpeta de `--reports`), escribe la ruta de la carpeta en el log y enseña «Reporte guardado» unos segundos. El
HUD normal no enseña nada de esto. En la carpeta:

| Fichero | Qué es |
|---|---|
| `save.yaml` | La partida en ese momento, con el sistema de guardado de siempre. En mitad de un combate, un script, un cambio de mapa o una escena (cuidados, evolución, sueño, gimnasio, tienda...) no se puede guardar: entonces es la última foto del campo libre, que el juego toma cada 5 s mientras se puede, y `state.yaml` lo dice (`save.kind: field_snapshot`, `seconds_ago` y `reason`) |
| `screenshot.png` | La captura del frame siguiente, sin el aviso |
| `state.yaml` | El commit del que se compiló (`git describe --dirty`), el mapa, la hora y el día del juego, qué hay en pantalla (campo, combate y su fase, script en marcha y sus secciones, cuadro de diálogo, menú, cambio de mapa), la posición y el giro del jugador, del compañero y de cada NPC (id de script, especie, modelo, si un script lo sujeta o lo mueve), los luchadores del combate con HP, MP y posición, la especie y el estado del compañero, y el contador `field.overlaps` |
| `log.txt` | Las últimas 2000 líneas del log |
| `route.yaml` | Una ruta de autotest que carga `save.yaml` (`load_save`) y espera en ese mapa |

Para reproducirlo, desde la raíz del repositorio:

```bash
build/linux-clang-debug/apps/game/dwe-game --content build/studio-content \
  --autotest ~/.local/share/indie-modding-games/dwe/reports/<carpeta>/route.yaml --shots /tmp/repro
```

La ruta acaba con una captura; añade detrás de la espera los pasos que enseñan el bug y queda como prueba.

## Autotests

Un autotest recorre una ruta escrita en YAML con los mismos eventos que un jugador: las teclas y el ratón entran
como eventos de la plataforma y el mando es un mando virtual de SDL, así que pasan por el mapa de acciones, la UI y
el campo de verdad. Mientras corre, el juego descarta el ratón y el teclado reales del escritorio (el puntero de quien
use el ordenador, o el que descansa sobre la ventana, movería el puntero bajo un clic o una pulsación sostenida), así
que se puede seguir usando el escritorio; un mando real sí llega. Las rutas de la prueba están en `tests/autotest/`:

- `route-keyboard.yaml`, `route-gamepad.yaml` y `route-mouse.yaml`: File City → casa del anciano → File City con
  cada dispositivo; abren el menú con ese dispositivo, ponen el minimapa en «Siempre», «Nunca» y otra vez «Según el aspecto», se paran detrás de
  una capa de profundidad de File City, cambian la ventana a 800x600, 1280x720, 1920x1080, 2560x1080 y pantalla
  completa, y hacen capturas. Con el aspecto original, el minimapa y las opciones están entre las herramientas de la
  página del jugador (Tab en ella; Norte en el mando), como el menú del original, que solo tiene sus seis entradas;
  las rutas que abren el minimapa o las opciones (`route-keyboard`, `route-gamepad`, `route-mouse`, `route-language`,
  `route-field-hud` y las tres de la exploración libre) llegan a ellas por ahí con ese aspecto (pasos `when` con
  `ui_skin`) y por la primera página del menú con el moderno. El ratón aún no tiene forma de abrir esas
  herramientas, así que las rutas del ratón pulsan Tab para eso y hacen clic en lo demás. `route-mouse` además hace
  clic en las pestañas de la página del jugador (aspecto original) y de la del compañero (Estado y Técnicas) y
  comprueba que cada clic enseña su página.
- `route-fade.yaml`: entra en la casa del anciano con el ratón y hace una captura en mitad del fundido, con el nombre
  del mapa a la vista en el idioma del juego (`--lang`), y otra al acabar.
- `route-language.yaml`: empieza en el menú principal (`main_menu: true`), cambia el idioma desde él y lo devuelve,
  entra al campo y abre el menú de pausa, con capturas de cada pantalla. Se corre una vez por idioma, cada una con
  su carpeta de capturas:

```sh
for lang in en es; do
    scripts/dev.sh run --content <carpeta> --lang $lang --autotest tests/autotest/route-language.yaml \
        --shots <capturas>/$lang
done
```

- `route-field-hud.yaml`: el estado del campo del HUD ([contrato](../architecture/ui.md#estado-del-campo)): la hora y
  el día de una partida nueva, el reloj que avanza, los rangos de felicidad y disciplina del ruleset, que al compañero
  no le parece nada especial el mapa de inicio (`hud.partner.area` a 0), el hambre que aparece con el reloj
  acelerado, el panel que se va con el menú de pausa y vuelve al cerrarlo, y capturas a 800x600, 1280x720 y 2560x1080
  y con la escala de la interfaz más grande. Se corre en los dos idiomas, como
  `route-language.yaml`. Con ella, `route-skin-original.yaml`, `route-scripted-loop.yaml` y las tres de la
  exploración libre, el paso de rutas es de 25 rutas (con la pareja de `route-save-load`): 50 ejecuciones sobre el
  contenido extraído, más las 4 del `.pak` cocinado, 54 en total.
- `route-loop-keyboard.yaml`: el bucle de la fase 3 con el teclado, desde el menú principal: cría al compañero
  (lo regaña, espera a que tenga hambre y le da su comida favorita desde la bolsa, lo lleva al baño y lo acuesta),
  lo entrena, combate con el disparador de depuración y, con el reloj acelerado, lo cuida a tramos hasta que
  evoluciona. Cada paso comprueba su efecto en el estado del compañero; hace capturas de cada etapa. Tarda unos dos
  minutos con lavapipe. Se corre en los dos idiomas, como `route-language.yaml`.
- `route-save-load.yaml` y `route-save-load-check.yaml`: guardar y cargar una partida, en dos ejecuciones. La
  primera empieza una partida nueva desde el menú principal, pone tres objetos en la bolsa, entra en la casa del
  anciano y cruza la sala, adelanta el reloj hasta la una de la tarde y lo para, y guarda en `slot_01` con el comando
  de depuración `save("slot_01")` (solo guarda si `--saves` nombra una carpeta). La segunda arranca con
  `--load slot_01` desde esa misma carpeta y comprueba que la partida está como se guardó: el mapa y el sitio del
  jugador, el nombre y la edad del compañero, el día y la hora, el dinero y los objetos de la bolsa. Todavía no
  comprueba la historia, que no se guarda. Es una ruta de humo: el gate corto la corre en `en`, y el diario en los
  dos idiomas. La segunda nunca corre sola; a mano:

```sh
saves=<capturas>/saves
scripts/dev.sh run --content <carpeta> --lang en --saves $saves --autotest tests/autotest/route-save-load.yaml \
    --shots <capturas>/save-load
scripts/dev.sh run --content <carpeta> --lang en --saves $saves --load slot_01 \
    --autotest tests/autotest/route-save-load-check.yaml --shots <capturas>/save-load-check
```

  Cuando exista la pantalla de guardar del juego, la ruta la usará en vez del comando y cargará desde el título.
- `route-sleep-save.yaml`: dormir y guardar ([guardar-y-cargar.md](../architecture/guardar-y-cargar.md#36-dormir)).
  Empieza una partida nueva en `slot_01` desde el menú principal, acelera el reloj hasta que el compañero tiene sueño
  (`pause_menu.sleep_enabled`), elige «Dormir» en la pausa y «Guardar y dormir» en el menú de dormir, y espera
  `save.last_result` = `saved` sin ninguna pregunta, porque la ranura es de esta misma partida; luego comprueba que el
  compañero cumplió un día. Necesita `--saves` con una carpeta vacía; el gate le da una suya (`saves_routes`). Se
  corre en los dos idiomas.
- `route-dev-assets.yaml`: comprueba que el aviso de los [assets de desarrollo](../architecture/assets-de-desarrollo.md)
  se ve en el menú principal y en el campo. Es la única ruta que necesita `--dev-assets` y un `dev-assets.yaml` en la
  carpeta actual; se corre en los dos idiomas.
- `route-skins.yaml`: empieza con el aspecto original (el de por defecto), desde el menú principal cambia en vivo al
  de prueba (el paquete sintético `tests/mods/ui_skin/test_contrast`), recorre con él el campo, el menú de pausa, el
  del compañero y un combate, y pasa al moderno desde las opciones del menú de pausa, con capturas de cada pantalla.
  Necesita `--mods tests/mods/ui_skin` y se corre en los dos idiomas, como `route-language.yaml`.
- `route-skin-original.yaml`: el aspecto original en cada pantalla que dibuja, con una captura de cada una: el
  título y su menú, la lista de ranuras y la pregunta de jugar sin guardar (necesita `--saves` con una carpeta vacía
  propia, que el gate le da), el reloj y las barras del campo, el menú de pausa, la página del jugador y las opciones de sus
  herramientas, la ficha del compañero (su entrada del menú) y la bolsa (la de los objetos, con las opciones de un objeto y
  los órdenes), un combate y sus cuadros finales, y la burbuja del hambre sobre el compañero a 1920x1080. La carpeta de
  partidas sigue vacía tras el combate y los días nuevos (`saves.files: "0"`): jugar sin guardar no escribe nada. Se
  corre en los dos idiomas.
- `route-text-fit.yaml`: que cada texto cabe en su caja (`expect_text_fits` tras cada captura) en las pantallas del
  aspecto original desde el campo: el menú de pausa y sus páginas (jugador, árbol, medallas, cartas, herramientas,
  opciones, minimapa), la ficha, las técnicas y la bolsa del compañero con las opciones de un objeto, la máquina de
  guardar y un combate con sus cuadros finales, a 1280x720 y otra vez las páginas con más texto a 1920x1080. Se corre
  en los dos idiomas. Para mirar los textos de otra ruta sin tocarla, añádele `text_fit: true`.
- `route-save-indicator.yaml`: el [aviso de guardado](../modulos/apps/game.md#aviso-de-guardado) desde una partida
  nueva con el teclado: el guardado de la consola, el autoguardado al entrar en la casa del anciano, la máquina de
  guardado y un guardado con el menú de pausa abierto a 1280x720, y otro a 1920x1080 y a 1024x768; cada captura
  comprueba que el aviso no pisa ninguna ventana (`expect_clear`) y que los textos caben (`text_fit`). Al final apaga
  la opción en el menú de pausa y comprueba que un guardado ya no lo enseña. Necesita `--saves` con una carpeta vacía.
  Se corre en los dos idiomas.
- `route-minimap-clear.yaml`: el [minimapa bajo las ventanas](../modulos/apps/game.md#minimapa-bajo-las-ventanas)
  desde una partida nueva con el teclado: el minimapa siempre visible y grande, y el menú de pausa y la página del
  compañero a 1280x720, 1920x1080 y 1024x768 sin pisar nada (`expect_clear`), con el minimapa de vuelta al cerrarlos.
  Hecha para el aspecto moderno (`--ui-skin modern`); también pasa con el original. Se corre en los dos idiomas.
- `route-digital-time.yaml`: la [hora en dígitos](../modulos/apps/game.md#hora-en-dígitos) bajo el reloj del HUD
  desde una partida nueva con el teclado: las 08:00 a 1280x720, 1920x1080 y 1024x768, la noche (las 20:00), el menú
  de pausa abierto (el reloj se va con su caja) y la opción apagada desde el menú. Cada captura con la caja comprueba
  que no pisa nada del HUD ni de otro documento (`expect_clear: hud#field-time`) y que los textos caben (`text_fit`).
  Se corre en los dos idiomas y con los dos aspectos (`--ui-skin original` y `modern`).
- `route-hidden-content.yaml`: el [contenido oculto](../modulos/apps/game.md#contenido-oculto) desde el menú principal
  con el teclado: la pantalla de reglas de la partida nueva (con las reglas del original y con el contenido oculto
  encendido), el árbol con 65 casillas y las cuatro formas nuevas por la revisión natural: Panjyamon desde Penguinmon,
  Gigadramon desde Seadramon, MetalEtemon desde Sukamon y Myotismon desde Devimon (`story_set` para las puertas,
  `evolve` para la forma de origen, `partner_set` para los valores y `clock_speed` hasta la revisión; para Myotismon
  el reloj corre antes hasta las 19:00, `hud.clock.hour`, y la revisión cae de noche). Captura cada compañero nuevo en
  el campo y en su ficha, y el árbol con las casillas nuevas en el aspecto original (Panjyamon y Gigadramon en la
  página 1, MetalEtemon y Myotismon, que no caben en la caja de Ultra, en la página 2, con el detalle de MetalEtemon),
  con `text_fit`. Tras cada evolución espera a que su escena acabe (el ajuste `evolution.active`, `"true"` mientras
  corre) y confirma el aviso del nivel de domador si sale, así que vale también en una máquina lenta. Se corre en los
  dos idiomas, con `--saves` en una carpeta vacía.
- `route-hidden-content-off.yaml`: lo mismo con las reglas del original: el árbol sigue con 61 casillas y el mismo
  Penguinmon evoluciona a la forma que elige el original. Se corre en los dos idiomas, con `--saves` en una carpeta
  vacía.
- `route-mega-step.yaml`: el paso Ultra → Mega ([evolución](../modulos/game/evolution.md#paso-ultra--mega)): una
  partida nueva con el contenido oculto y el paso Mega encendidos, el árbol con 66 casillas, MetalEtemon desde Etemon
  y Machinedramon desde Megadramon por la revisión de un ultra (120 h). Captura la pantalla de reglas, los dos Megas
  en el campo (Machinedramon a 0,6) y la ficha de Machinedramon; hace evolucionar a Myotismon (`evolve`, para que su
  casilla se abra) y captura el árbol en el aspecto original: los dos Megas en la caja Mega, a la derecha de la de
  Ultra en la misma página, y Myotismon, que no cabe en la caja de Ultra, en la página 2 (Abajo desde la última
  casilla de Bebé I, su detalle y Arriba de vuelta); luego enciende las etapas canónicas y captura el árbol de una
  página con los cuatro Megas y el detalle de Machinedramon, con `text_fit`. Se corre en los dos idiomas y los dos
  aspectos, con `--saves` en una carpeta vacía.
- `route-canonical-stages.yaml`: las [etapas canónicas](../modulos/apps/game.md#etapas-canónicas) en una partida nueva
  con el contenido oculto: enciende la opción (la lista de opciones se desplaza hasta la fila nueva), captura el árbol
  de una página con las seis cajas (la caja Mega a la derecha de la de Ultra), abre el detalle de Phoenixmon y el de
  MetalGreymon (marcados como criados con `story_set`), la apaga y captura el árbol del original con flechas y número
  de página y el detalle de MetalGreymon, baja con Abajo desde Etemon a la página 2 (MetalEtemon y Myotismon, que no
  caben en la caja de Ultra, en la misma columna) y vuelve con Arriba a la página 1. El árbol solo en la piel
  original. Se corre en los dos idiomas y las dos pieles, con `--saves` en una carpeta vacía.
- `route-city-tab.yaml`: la [pestaña Ciudad](../modulos/apps/screens.md#pestaña-ciudad-mejora-de-dwe) desde una
  partida nueva con siete especies unidas a la ciudad (`story_set`): abre la página del jugador con el teclado, va a la
  pestaña, pone el cursor en una que vive en la ciudad, en una que falta y en un campeón unido, a 1280x720 y otra vez a
  1920x1080 y a 1024x768, con `text_fit`. Se corre en los dos idiomas y, además, con `--ui-skin modern` (la entrada
  «Ciudad» de la página del jugador).
- `route-evolution-requirements.yaml`: los [requisitos de evolución](../modulos/apps/game.md#requisitos-de-evolución)
  en el gráfico del compañero (Agumon, como en una partida nueva) con el teclado: el cursor por sus evoluciones sin
  descubrir («???») y descubiertas, la vista original, el peso movido con bayas grandes y el gráfico a 1280x720, 1920x1080 y 1024x768 (`text_fit`,
  `expect_clear`); al final apaga y enciende la opción. Necesita `--saves` con una carpeta vacía (o una copia de una
  partida con Agumon y `--load`). Se corre en los dos idiomas.
- `route-status-extras.yaml`: los [extras de la ficha](../modulos/apps/screens.md#extras-de-la-ficha) del
  compañero desde una partida nueva con el teclado: dos entrenamientos (cansancio) y el reloj rápido hasta un error de
  cuidado, la ficha con la opción encendida a 1280x720 y 1920x1080, la opción apagada desde el menú de pausa y la ficha
  otra vez como el original, con `text_fit`. Se corre en los dos idiomas y con los dos aspectos (`--ui-skin original`
  y `modern`).
- `route-scripted-loop.yaml`: una vuelta por cinco zonas desde una partida nueva con el teclado: File City (`twna01`)
  → la casa del anciano, `room08` (tabla de salidas del mapa) → File City (script) → `trai00` (tabla) → `mayo00`
  (tabla), donde juega el primer combate del bosque y el reclutamiento de Agumon → `twnb01` (warp del script del mapa,
  al pisar la celda) → File City por otra entrada (script), que tras el reclutamiento ya es `twna02`. Comprueba los dos caminos de cambio de mapa, la tabla y los
  [warps de script](../architecture/warps-de-script.md), y que el compañero sigue al jugador en cada llegada; hace
  una captura en cada zona. Se corre en los dos idiomas, como `route-language.yaml`.
- `route-free-exploration.yaml`: la [exploración libre](../architecture/warps-de-script.md#exploración-libre) con el
  teclado, desde una partida nueva: la enciende en las opciones del menú de pausa y pasa las puertas que la historia
  aún tiene cerradas hacia dos zonas a las que una partida nueva no llega (`mayo02_2` y `mayo08b`), con el
  selector de destinos y con camino de vuelta. Comprueba que el selector empieza en el lugar de la historia, que pasa
  de página y que Escape deja al jugador en el mapa. En `mayo00` juega antes el primer combate del bosque, como
  una partida nueva, y sus caminatas aguantan con `encounters` el Betamon salvaje que llega cuando quiere el tiempo; antes cura al
  compañero con objetos (y lo regaña si los rechaza), porque ese combate no deja huir y uno perdido lo manda a casa.
  Acaba al llegar a `mayo08b`: su encuentro salvaje no deja huir y el compañero de una partida nueva lo pierde, lo que
  lo manda a casa de Jijimon, como en el original. Se corre en los dos idiomas.
- `route-free-exploration-mouse.yaml` y `route-free-exploration-gamepad.yaml`: el interruptor y el selector con el
  ratón (un clic en la segunda página) y con el mando (Este se queda, Sur toma el lugar de la historia). Se corren en
  los dos idiomas.

Todas las rutas corren con el aspecto original, el de por defecto, salvo que pidan otro. Las dos de aspectos (y el
aspecto original en todas) necesitan contenido extraído con `dwx` de la versión 2 del extractor o posterior, que
trae las imágenes del aspecto original; con uno anterior el juego empieza con el moderno y esas dos rutas fallan en
su primer paso.

- `route-mod-map.yaml`: con el mod de ejemplo montado ([`tests/mods/example_mod`](../modulos/tests/mods.md)),
  empieza en el mapa que añade el mod por su id (`example_mod:meadow`), sin código propio, y sale por su salida a
  File City. Se corre en los dos idiomas:

```sh
for lang in en es; do
    scripts/dev.sh run --content <carpeta> --mods tests/mods/example_mod --lang $lang \
        --autotest tests/autotest/route-mod-map.yaml --shots <capturas>/$lang
done
```
- `route-etapa2.yaml` y `route-etapa2-jungla.yaml`: la Etapa 2 de la historia, con el candado tal como sale (la
  etapa activa del ruleset es la 2: la jungla y el túnel abiertos, la etapa 3 y siguientes cerradas) y una partida
  sembrada que empieza en el mapa de cada mitad. La del Drill Tunnel (`--start tunn06`) evoluciona al compañero a Champion, habla con el minero, ve rodar la
  roca (disparador 120), sigue a tunn07 y tunn08, donde tiembla la pantalla y empieza el combate con el monstruo de
  fuego, lo gana con `battle_win` y acaba con el disparador 209. La de la jungla (`--start trop04`) choca con el
  Betamon, que vuelve a la ciudad (204), pasa por trop01 y trop05 (en trop05 coloca al jugador junto a la salida, para
  no pelear con los salvajes del camino) y entra en el bosque de Amida (alerta, 137) y sube por su camino central:

```sh
scripts/dev.sh run --content <carpeta> --start tunn06 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa2.yaml --shots <capturas>/etapa2
scripts/dev.sh run --content <carpeta> --start trop04 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa2-jungla.yaml --shots <capturas>/etapa2
```

- Los reclutamientos de la Etapa 2, una ruta cada uno, con el mismo candado y la partida sembrada; cada una comprueba
  el disparador del reclutamiento con `story.trigger.<n>`. `route-etapa2-coelamon.yaml` (`--start trop00`) cruza las
  casillas del puente roto de la jungla (disparador 185), vuelve por mayo08a, mayo02 y mayo04b a mayo05 y pisa la
  orilla, donde Coelamon sube del río y se une (249).
  `route-etapa2-drimogemon.yaml` (`--start tunn09`) habla con Drimogemon en la mina (disparador 353), deja pasar un
  día cuidando al compañero con el reloj rápido (la variable 30 cuenta los días) y vuelve a hablarle: se une (238).
  `route-etapa2-piximon.yaml` (`--start trop01 --seed 4`) se cruza con Piximon, que solo aparece en la jungla con una
  tirada de 3 entre 100 al entrar (la semilla 4 la da; otra tirada de azar antes de entrar puede cambiarla), acepta
  su reto, gana la pelea y Piximon se une (255).
  `route-etapa2-dirt.yaml` (`--start tunn04`) hace el trabajo de la tierra del túnel: vence al excavador de tunn04
  (disparador 140), acepta el trabajo del minero de tunn05, carga la vagoneta en el montón (una casilla de 80 a 109:
  corre al confirmar), la vacía en tunn01 y cobra (variable 18 a 1). Cinco días después los mineros acaban solos
  (variable 18 a 10) y el paso a tunn06 queda abierto.
  `route-etapa2-centarumon.yaml` (`--start trop06`) cruza el bosque de la lotería de escaleras: la casilla de entrada
  echa a suertes cuál de los tres finales es el bueno (variable 116), la ruta sale por el carril que lleva a él (un
  bloque `when` por final), toma cada cruce que encuentra y Centarumon se une al final (236). Su viaje a koda00 queda
  cerrado por el candado de la etapa 2 y el jugador sigue en trop06. Los tres finales reclutan igual (secciones 0x44,
  0x45 y 0x46, la misma escena con otro carril); cuál sale depende de la semilla y de los fotogramas que el jugador
  pasa en la casilla, así que puede cambiar con el motor: hoy la semilla 1 lleva a la sección 0x44, la 5 a la
  0x45 y la 2 a la 0x46; las tres reclutan.
  `route-etapa2-amida-trap.yaml` (`--start trop06`) sube por el carril oeste pasando su primer cruce (disparador 161,
  sección 0x37) y vuelve al inicio del carril (sección 0x34), que encuentra el 161 puesto y salta la trampa: la marca
  de diana parpadea sobre el nodo 10 del jugador (`spawn_sprite_at_entity`, sprites 44 y 45), vuelan las flechas y
  el final de la trampa borra los disparadores del carril (158 a 173).
  `route-etapa2-vegiemon-rumor.yaml` (`--start twna03`) habla con Tanemon en el campo de Palmon, que cuenta lo de la
  planta rara de la jungla y pone el disparador 86; lo hace con Palmon en la ciudad (246), sin Vegiemon (225), de 7 a
  18 h y con 10 puntos de prosperidad (variable 1), que la ruta escribe con `story_set` porque la partida sembrada no
  los tiene. `route-etapa2-vegiemon.yaml` (`--start trop03`) parte del 86 (escrito) y de la Rain Plant en la bolsa
  (`give_item`; en la historia es el fruto que da el campo el día 15), pisa el brote, dice que sí a usar objetos y
  elige la planta en el selector de la rutina 0 (variable 254 = 121): llueve sobre el brote y Vegiemon se une (225).
  `route-etapa2-lock.yaml` (`--start trop03`) pisa la frontera del candado en la jungla: la salida de trop03 a
  gcan01 (etapa 4) y la de trop02 a dgha01 (etapa 7) no ocurren, sale el mensaje del candado y el jugador sigue en
  el mapa de la jungla.

```sh
scripts/dev.sh run --content <carpeta> --start trop00 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa2-coelamon.yaml --shots <capturas>/etapa2
scripts/dev.sh run --content <carpeta> --start tunn09 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa2-drimogemon.yaml --shots <capturas>/etapa2
scripts/dev.sh run --content <carpeta> --start trop01 --lang en --seed 4 \
    --autotest tests/autotest/route-etapa2-piximon.yaml --shots <capturas>/etapa2
scripts/dev.sh run --content <carpeta> --start tunn04 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa2-dirt.yaml --shots <capturas>/etapa2
scripts/dev.sh run --content <carpeta> --start trop06 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa2-centarumon.yaml --shots <capturas>/etapa2
scripts/dev.sh run --content <carpeta> --start trop06 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa2-amida-trap.yaml --shots <capturas>/etapa2
scripts/dev.sh run --content <carpeta> --start twna03 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa2-vegiemon-rumor.yaml --shots <capturas>/etapa2
scripts/dev.sh run --content <carpeta> --start trop03 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa2-vegiemon.yaml --shots <capturas>/etapa2
scripts/dev.sh run --content <carpeta> --start trop03 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa2-lock.yaml --shots <capturas>/etapa2

- Las rutas de la Etapa 4 (el Gran Cañón y la fortaleza), con `--story-stage stage_4` (la etapa activa del ruleset
  todavía es otra) y la partida sembrada; cada trozo empieza en su mapa y escribe con `story_set` los disparadores de
  los trozos anteriores. `route-etapa4-yuramon.yaml` (`--start twna03`) habla con Yuramon en la ciudad: con 6 puntos de
  prosperidad (variable 1, escrita) pide ayuda (disparador 124); con el puente del cañón hecho (103, escrito) cuenta
  lo de los bandidos (197). `route-etapa4-canon.yaml` (`--start trop03`) entra en gcan01, cruza el puente (103), pasa
  por gcan09 a gcan08_1, donde los bandidos tienden la emboscada (con el 197): gana el combate con `battle_win` (198 y
  155), sigue a gcan03 y pisa su salida a Freezeland (etapa 5), que el candado deja cerrada.
  `route-etapa4-birdramon.yaml` (`--start gcan03`) sube con el ascensor (Press UP) a gcan10, gana a Birdramon (221),
  que lleva al jugador a la ciudad, y en la casa del transporte (room12) abre la lista de destinos con más pájaros
  escritos y 1800 bits: los destinos que el candado cierra salen en rojo, no se pueden elegir y no cobran; vuela a
  gcan03. `route-etapa4-fortaleza.yaml` (`--start gcan03`) baja con el ascensor (Press DOWN) a gcan06 y gcan05, cuyo
  guardia se aparta tras la emboscada (200), cruza ogre00 a ogre03 (los guardias que lo alcanzan pelean y se ganan con
  `battle_win`), gana a Ogremon en el trono (175 y 176), baja por el montacargas a gcan02 y sigue al puente (gcan04),
  donde una voz pide ayuda (182), y pisa su salida cerrada a Freezeland. `route-etapa4-shellmon.yaml` (`--start
  gcan02`) escribe lo que deja bajar con el ascensor de arriba del cañón a la fortaleza y volver andando (180 fuera,
  181; el montacargas del trono volvería a poner el 180), sigue al otro puente (gcan04_2), donde Shellmon se une
  (235), y su salida a Freezeland también queda cerrada.
  `route-etapa4-lock.yaml` (`--start gcan01`) pisa la salida de gcan01 a dgha01 (etapa 7): sale el aviso del candado
  y el jugador sigue en gcan01; luego vuelve a la jungla por trop03.

```sh
scripts/dev.sh run --content <carpeta> --start twna03 --story-stage stage_4 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa4-yuramon.yaml --shots <capturas>/etapa4
scripts/dev.sh run --content <carpeta> --start trop03 --story-stage stage_4 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa4-canon.yaml --shots <capturas>/etapa4
scripts/dev.sh run --content <carpeta> --start gcan03 --story-stage stage_4 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa4-birdramon.yaml --shots <capturas>/etapa4
scripts/dev.sh run --content <carpeta> --start gcan03 --story-stage stage_4 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa4-fortaleza.yaml --shots <capturas>/etapa4
scripts/dev.sh run --content <carpeta> --start gcan02 --story-stage stage_4 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa4-shellmon.yaml --shots <capturas>/etapa4
scripts/dev.sh run --content <carpeta> --start gcan01 --story-stage stage_4 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa4-lock.yaml --shots <capturas>/etapa4
```
```

- Las rutas de la Etapa 3 (Mt. Panorama, Gear Savanna, la cueva de Leomon, Trash Mountain y el vestíbulo de la
  Arena) se juegan con `--story-stage stage_3` mientras la etapa activa del ruleset sea anterior, con la partida
  sembrada y una ruta por tramo; los combates del camino acaban ganados al empezar (`encounters: win`).
  `route-etapa3-panorama.yaml` (`--start mayo02_2`) sube por miha00, cruza miha05, miha01 y miha02 (su bajada es
  la casilla 51 del script 21, que lleva a miha04a) y baja a gias00. `route-etapa3-sabana.yaml` (`--start gias00`)
  cruza gias01 hasta Elecmon (gias02): la ruta lo pone delante del jugador (`place`) y lo toca tres veces (variable
  33; a la tercera se une, 218); sigue por gias03 hasta gias08, cuya salida a stic01 (etapa 7), a la que la ruta llega
  desde el camino del norte (`place`, para no rozar la salida a Trash Mountain), queda cerrada con el aviso del candado, y entra en Trash Mountain (gomi01, gomi02). `route-etapa3-leomon-tablet.yaml` (`--start
  leom02`) baja a la cueva (leom01), pasa por la casilla 51 (el jugador mira la cueva, 134) y coge la tablilla de la
  casilla 52 (Leomonstone, 135). `route-etapa3-leomon.yaml` (`--start gias07`) ve el rescate de Patamon en gias07
  (196), pone la Leomonstone en la bolsa y entra en gias09, donde Leomon lee la tablilla y se une (248).
  `route-etapa3-arena.yaml` (`--start room13`) confirma en el mostrador del vestíbulo de la Arena (casillas 81): la
  bienvenida (91) y, a la segunda, la inscripción, que abre el calendario (rutina 18); el jugador vuelve a quedar
  libre y sale a la ciudad por la puerta (casillas 52). `route-etapa3-tournaments.yaml` (`--start room13`, en y es)
  juega dos copas: se inscribe en la Grade C de hoy, a las 10:00 pisa la puerta del estadio (casilla 54, rutina 6),
  ve la tarjeta del grado y el cuadro, y pierde el primer combate (`battle_lose`): premio de participación, sin copa
  ganada (el compañero queda con un quinto de PV y la ruta lo cura con un objeto). Luego se inscribe en la Grade D de
  mañana, pasa la noche, gana los tres combates (`battle_win`) con el cuadro antes de cada uno y la subida del
  campeón al final, y comprueba el disparador de la copa ganada (15), las 3 victorias (variable 255) y el dinero del
  premio en la bolsa.

```sh
scripts/dev.sh run --content <carpeta> --story-stage stage_3 --start mayo02_2 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa3-panorama.yaml --shots <capturas>/etapa3
scripts/dev.sh run --content <carpeta> --story-stage stage_3 --start gias00 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa3-sabana.yaml --shots <capturas>/etapa3
scripts/dev.sh run --content <carpeta> --story-stage stage_3 --start leom02 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa3-leomon-tablet.yaml --shots <capturas>/etapa3
scripts/dev.sh run --content <carpeta> --story-stage stage_3 --start gias07 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa3-leomon.yaml --shots <capturas>/etapa3
scripts/dev.sh run --content <carpeta> --story-stage stage_3 --start room13 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa3-arena.yaml --shots <capturas>/etapa3
scripts/dev.sh run --content <carpeta> --story-stage stage_3 --start room13 --lang es --seed 1 \
    --autotest tests/autotest/route-etapa3-tournaments.yaml --shots <capturas>/etapa3
```

- `route-etapa5*.yaml`: la Etapa 5 de la historia (Freezeland y Whamon) con `--story-stage stage_5` y la partida
  sembrada. Los salvajes de Freezeland vencen al compañero sembrado, así que las caminatas acaban sus combates al
  momento (`encounters: win`), y cada ruta escribe con `story_set` los disparadores de etapas anteriores que necesita.
  El camino que fuerza el original se juega en dos tramos: `route-etapa5.yaml` (`--start gcan04`) sale del Great
  Canyon a frzl01, cruza frzl02, frzl03, frzl06, frzl12 y frzl13 hasta la playa de frzl16, habla con Whamon (casilla
  0x51 del script 130, con el 175 de la Ogre Fortress), dice que sí al viaje (352) y entra en la cueva de ogre10 con
  su escena (sección 0x3c, «una voz detrás»). `route-etapa5-whamon.yaml` (`--start ogre10,0`, la salida interior de
  la cueva) entra en ogre11, donde Ogremon, sus dos Gabumon y WaruSeadramon pelean (script 137); la victoria pone el
  150 y vuelve a ogre10 con la sección 6 del script 136: Ogremon vuela las rocas y huye, Whamon se une (224) y lleva
  al jugador a frzl16. Con el 150, `route-etapa5-ogremon.yaml` (`--start tunn02`) vuelve al Drill Tunnel de la etapa
  2: la emboscada de la sección 0x33 del script 26 sale, se gana y Ogremon se une (234).
  `route-etapa5-angemon.yaml` (`--start icsa01`) pisa delante de la estatua (324), el compañero mueve el pedestal
  (rutina 50, 323), baja por el laberinto (icsa02, icsa07, icsa04, icsa05) a icsa06, donde la luz (rutinas 45 y 46)
  pone el 322, y vuelve a icsa01 (su entrada repite las rutinas 49 y 50); en icsa05 pone al jugador junto a las
  salidas (`place`), porque BlueMeramon tumba al compañero sembrado. El despertar de Angemon lo juega
  `route-angemon.yaml` (`--start icsa02`, con el 324 y el 323 escritos): la subida de icsa02 a icsa01 (casillas de la
  sección 0x33 del script 94) lleva a la salida 1 de icsa01 y, con el 322 de la luz, corre al llegar la sección 5 del
  script 93 (`warp_to` 97, 1, 5): «Oh? About that statue...», la estatua despierta como Angemon, se va hacia el sur y
  se une a la ciudad (220). Sin el 322 la misma subida no corre nada y la estatua sigue quieta. Para el jugador: tras
  la luz de icsa06, volver a icsa01 por la escalera de icsa02 (no por la salida de icsa05). La sección 0x34 de icsa01
  también salta a la sección 5, pero solo con el 220 ya puesto, así que en el original no hace nada.
  `route-etapa5-curling.yaml` (`--start frzl15,2`) habla con Penguinmon (357), juega la partida de curling de frzl17
  confirmando cada tiro y vuelve a frzl15; si gana, Penguinmon se une (257). Frontera del candado:
  `route-etapa5-lock.yaml` (`--start frzl08,1`) pisa la salida de frzl08 a mist07 (etapa 7) y
  `route-etapa5-dock-lock.yaml` (`--start twna02`, con el 224 escrito) elige Factorial Town (fact01, etapa 6) en el
  muelle de File City; las dos quedan cerradas con el aviso y el jugador sigue en el mapa.

```sh
for r in "route-etapa5 gcan04" "route-etapa5-whamon ogre10,0" "route-etapa5-ogremon tunn02" \
         "route-etapa5-angemon icsa01" "route-angemon icsa02" "route-etapa5-curling frzl15,2" "route-etapa5-lock frzl08,1" \
         "route-etapa5-dock-lock twna02"; do
    set -- $r
    scripts/dev.sh run --content <carpeta> --start $2 --story-stage stage_5 --lang en --seed 1 \
        --autotest tests/autotest/$1.yaml --shots <capturas>/etapa5
done
```

- `route-etapa6-*.yaml`: la Etapa 6 (Factorial Town) con `--story-stage stage_6`, en eslabones que se reanudan con
  `--start`; cada uno escribe con `story_set` los disparadores de los anteriores. Las caminatas ganan los combates del
  camino con `encounters: win` (los guardias de día y PlatinumSukamon de noche). `route-etapa6-llegada.yaml` (`--start
  twna02`) sube a Whamon en la orilla (casilla 55, disparador 224), elige Factorial Town, baja a la cloaca (340) y
  habla con Numemon (342). `route-etapa6-guardia.yaml` (`--start fact03`) habla con el guardia de fact04 (347), con
  Andromon en fact06 (343) y otra vez con el guardia por el turno (331); espera en fact03 con el reloj rápido a las
  11:56, cuando fact04 no tiene guardias, pasa a fact09 (332) y fact07, baja con el ascensor (Intro en la casilla 81)
  a la sala de máquinas (fact08b) y vence a Giromon: vuelve la luz (344). `route-etapa6-numemon.yaml` (`--start
  fact06`) oye a Andromon (341), baja a la cloaca, vence a Numemon, que se une (211), y vuelve con Andromon (328).
  `route-etapa6-andromon-ciudad.yaml` (`--start mayo02`) cruza el puente de mayo08b a twnb13 (329).
  `route-etapa6-andromon.yaml` (`--start fact06`) habla con Andromon, que se une (330, 240), y vence a Giromon en
  fact08a (241). `route-etapa6-metalmamemon.yaml` (`--start fact02 --seed 4`) se cruza con MetalMamemon (sale con un
  3 % al entrar en fact02; la semilla 4 lo saca), dice que sí al reto y lo vence (227). `route-etapa6-lock.yaml`
  (`--start twna02`) pisa la frontera: la casilla 56 de twna02 a mgen01 (etapa 8) muestra el aviso y el jugador sigue
  en twna02, y con el disparador 220 la casilla 52 lleva a room10.

```sh
scripts/dev.sh run --content <carpeta> --start twna02 --lang en --seed 1 --story-stage stage_6 \
    --autotest tests/autotest/route-etapa6-llegada.yaml --shots <capturas>/etapa6
scripts/dev.sh run --content <carpeta> --start fact03 --lang en --seed 1 --story-stage stage_6 \
    --autotest tests/autotest/route-etapa6-guardia.yaml --shots <capturas>/etapa6
scripts/dev.sh run --content <carpeta> --start fact06 --lang en --seed 1 --story-stage stage_6 \
    --autotest tests/autotest/route-etapa6-numemon.yaml --shots <capturas>/etapa6
scripts/dev.sh run --content <carpeta> --start mayo02 --lang en --seed 1 --story-stage stage_6 \
    --autotest tests/autotest/route-etapa6-andromon-ciudad.yaml --shots <capturas>/etapa6
scripts/dev.sh run --content <carpeta> --start fact06 --lang en --seed 1 --story-stage stage_6 \
    --autotest tests/autotest/route-etapa6-andromon.yaml --shots <capturas>/etapa6
scripts/dev.sh run --content <carpeta> --start fact02 --lang en --seed 4 --story-stage stage_6 \
    --autotest tests/autotest/route-etapa6-metalmamemon.yaml --shots <capturas>/etapa6
scripts/dev.sh run --content <carpeta> --start twna02 --lang en --seed 1 --story-stage stage_6 \
    --autotest tests/autotest/route-etapa6-lock.yaml --shots <capturas>/etapa6
```

- `route-etapa7-*.yaml`: la Etapa 7 (la ruta opcional, en cualquier orden) con `--story-stage stage_7`. No tiene un
  suceso obligado: sus reclutas suben la prosperidad y, con 50 o más, Greymon en la ciudad (disparador 205) y un
  compañero que ya no es de las formas pequeñas, la charla de Jijimon en su casa crecida abre Mt. Infinity (354).
  `route-etapa7-montana.yaml` (`--start twna02`) escribe eso, evoluciona al compañero, habla con Jijimon (354) y
  prueba el candado: la habitación de atrás (room11) y la casilla 56 a mgen01 son de la Etapa 8 y muestran el aviso.
  `route-etapa7-lock.yaml` (`--start trop02`) entra por la jungla a la mansión (dgha01, cerrada en la Etapa 2) y pisa
  en el cementerio (dgha02) las casillas 51 hacia la Back Dimension (mgen11, etapa 9, con la variable 16 a 2): aviso y
  el jugador sigue en dgha02; sin ella, la escena de la «fuerza negra» lo hace volver.
  `route-etapa7-mist.yaml` (`--start mist02`) cruza el bosque de la niebla: sus casillas mandan al azar al claro
  siguiente, atrás o en círculo, y la décima vez llevan siempre al claro de Jureimon (mist05), cuyo encuentro (147) y
  su relato quitan la niebla (148, rutina 37; la niebla aún no se dibuja, así que no tapa nada); sigue a Toy Town.
  `route-etapa7-toy-cards.yaml` (`--start omoc01`) da con `story_set` las tres cartas que pide el coleccionista de Toy
  Town, que se las queda (`take_card`) y da un objeto. `route-etapa7-fishing.yaml` (`--start mayo06`) saca la caña en
  el lago (disparador 45 y carne de cebo), lanza y la guarda con Esc: la pesca no entra en la beta, pero no rompe
  nada. `route-etapa7-stic.yaml` (`--start stic01`): hasta el disparador 319 el centro del pantano (stic02) está
  cerrado y no se llega a Volume Villa; el Otamamon (sale una de cada tres veces, la ruta entra y sale hasta verlo con
  `field.npc.9`) pelea, los Gekomon llevan al jugador a gkyo02, su jefe abre el pantano (319) y el camino recto de la
  niebla (114), y la ruta vuelve por gkyo01 y el pantano abierto hasta mist01. `route-etapa7-betl`, `-saib` y `-koda`
  recorren las salidas de Beetle Land, Overdell y el bosque de koda, ida y vuelta, ganando los combates del camino.

```sh
scripts/dev.sh run --content <carpeta> --start twna02 --lang en --seed 1 --story-stage stage_7 \
    --autotest tests/autotest/route-etapa7-montana.yaml --shots <capturas>/etapa7
scripts/dev.sh run --content <carpeta> --start trop02 --lang en --seed 1 --story-stage stage_7 \
    --autotest tests/autotest/route-etapa7-lock.yaml --shots <capturas>/etapa7
scripts/dev.sh run --content <carpeta> --start mist02 --lang en --seed 1 --story-stage stage_7 \
    --autotest tests/autotest/route-etapa7-mist.yaml --shots <capturas>/etapa7
scripts/dev.sh run --content <carpeta> --start omoc01 --lang en --seed 1 --story-stage stage_7 \
    --autotest tests/autotest/route-etapa7-toy-cards.yaml --shots <capturas>/etapa7
scripts/dev.sh run --content <carpeta> --start mayo06 --lang en --seed 1 --story-stage stage_7 \
    --autotest tests/autotest/route-etapa7-fishing.yaml --shots <capturas>/etapa7
for r in betl:betl01 stic:stic01 saib:saib01 koda:koda00; do
    scripts/dev.sh run --content <carpeta> --start ${r#*:} --lang en --seed 1 --story-stage stage_7 \
        --autotest tests/autotest/route-etapa7-${r%%:*}.yaml --shots <capturas>/etapa7
done
```

- Las rutas de la Etapa 8 (Mt. Infinity y el final) y de la Etapa 9 (el post-juego) van en trozos encadenados por una
  carpeta de partidas (`--saves`): cada trozo guarda en `slot_01` y el siguiente la carga (`--load slot_01`).
  `route-etapa8-jijimon.yaml` (`--start room08`) escribe los reclutas de las etapas 1 a 7 (77 puntos de prosperidad;
  guardar los vuelve a contar, como el original), da al compañero los chips de la tienda de méritos y la forma
  Ultimate, habla con Jijimon (sección 0x33 del script 210, disparador 354), gana a Airdramon al salir de la casa
  (`battle_win` si tarda), ve la escena de room11 y guarda en la ciudad. `route-etapa8-subida.yaml` cruza la puerta
  (casilla 58), sube de mgen01 a mgen10, mgen98 y mgen99 andando (corriendo a pocos fps se pasa a la salida de bajada
  junto a algunas llegadas) con los salvajes ganados al empezar; gana a los jefes de mgen06 (321) y mgen98 (326),
  contesta a Megadramon con la primera respuesta (darse prisa: sin combate, el camino crítico) y guarda en mgen99.
  `route-etapa8-final.yaml` (con `movies: true`) gana al jefe final (disparador 50), llega a tend02 (con 50 puntos o
  más), dice que sí al guardado de fin de juego, ve los créditos y vuelve a la portada. `route-etapa9-huevo.yaml` y
  `route-etapa9-back.yaml` cargan esa partida terminada: el huevo y Digitamamon en la cima (256), y la Back Dimension
  de frzl05 por mgen11, mgen12 y mgen13 hasta icsa08. `route-etapa8-lock.yaml` (`--start frzl05`) pisa la entrada a
  la Back Dimension con la etapa 8: el cambio de mapa del script no ocurre, sale el aviso y el jugador sigue en frzl05
  (la casilla no vuelve a lanzar su sección hasta que el jugador sale de ella).

```sh
scripts/dev.sh run --content <carpeta> --story-stage stage_8 --start room08 --saves <partidas> --lang en --seed 1 \
    --autotest tests/autotest/route-etapa8-jijimon.yaml --shots <capturas>/etapa8
scripts/dev.sh run --content <carpeta> --story-stage stage_8 --saves <partidas> --load slot_01 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa8-subida.yaml --shots <capturas>/etapa8
scripts/dev.sh run --content <carpeta> --story-stage stage_8 --saves <partidas> --load slot_01 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa8-final.yaml --shots <capturas>/etapa8
scripts/dev.sh run --content <carpeta> --story-stage stage_8 --start frzl05 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa8-lock.yaml --shots <capturas>/etapa8
scripts/dev.sh run --content <carpeta> --story-stage stage_9 --saves <copia de partidas> --load slot_01 \
    --start mgen99 --lang en --seed 1 --autotest tests/autotest/route-etapa9-huevo.yaml --shots <capturas>/etapa9
scripts/dev.sh run --content <carpeta> --story-stage stage_9 --saves <copia de partidas> --load slot_01 \
    --start frzl05 --lang en --seed 1 --autotest tests/autotest/route-etapa9-back.yaml --shots <capturas>/etapa9
```

#### La cadena completa

Las rutas `tests/autotest/route-full-NN-*.yaml` juegan la historia entera en orden, de la portada a los créditos, con
el candado tal como está (`active_stage`). Cada tramo empieza en la partida que el anterior guardó en `slot_01` (el
primero, en la portada con una partida nueva) y dura menos de 20 minutos:

| Tramo | Qué juega |
|---|---|
| 01 | Portada, partida nueva, la apertura, Agumon (203) y la pelea del puente roto (mayo08b) |
| 02 | Vuelta al gimnasio (trai00): 32 sesiones con los cuidados después de cada una |
| 03 | El paseo de Coelamon por la tarde, Betamon (204), el puente de Coelamon (249) y Centarumon (236) |
| 04 | El túnel: el excavador (140), el trabajo de la mina (variables 17 y 18) y cinco días de cuidados |
| 05 | La roca de tunn06 (120), Meramon (209) y Drimogemon (238); guarda en tunn09 |
| 06 | La muerte de vejez del compañero, el renacer (se queda el nombre), el gimnasio y un campeón (Numemon) |
| 07 | **Atajo**: el anciano (87), Greymon (205), el resto de reclutas escrito, Airdramon (354) y twna08 |
| 08 | La subida a Mt. Infinity hasta mgen99 |
| 09 | El jefe final, el guardado de fin de juego, los créditos y la portada |

El compañero de la cadena muere de viejo hacia el día 12 (los cinco días del túnel y los tres de Drimogemon), así
que el tramo 06 juega la muerte y el renacer de verdad: el anciano rechaza a un compañero de nivel novato o menor.

`scripts/story-chain.sh` las corre en orden, se para en el primer tramo que falla y escribe una línea por tramo
(`full-NN PASS 4m49s ...` o `full-NN FAIL ...: <motivo>`). Un tramo que falla se repite una vez antes de darlo por
fallado (`CHAIN_RETRIES`), porque con la máquina cargada las rutas a veces llegan tarde. `--from NN` empieza en el
tramo NN con la partida que dejó el anterior en `<salida>/saves/<NN-1>/`:

```sh
DISPLAY=:23 VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
    scripts/story-chain.sh --content <carpeta> --out <salida> [--from 03] [--to 05]
```

Atajos que la cadena declara (lo que una partida real ganaría jugando más): los salvajes del camino se ganan al
empezar (`encounters: win`); los jefes que el compañero no gana a tiempo terminan ganados tras un rato
(`win_after`); la comida, las medicinas y los chips están en la bolsa en vez de comprarse; el tiempo pasa deprisa
(`clock_speed`) para esperar la tarde o los días del túnel; y el tramo 07 escribe en dos partes los reclutas de las
etapas 4 a 7 (primero Leomon, Elecmon y Shellmon para los 15 puntos de la primera escena del anciano; luego el resto
hasta 77 de prosperidad), que aún no tienen tramos encadenados: la cueva de Leomon pide 45 de prosperidad.

Un autotest empieza en el campo, sin el menú principal, salvo que la ruta diga `main_menu: true`. No lee ni guarda
los ajustes del usuario. Como no tiene las preguntas de la apertura ni las películas, su partida nueva empieza en la
cadena sembrada (el campo libre de File City) salvo que la ruta diga `new_game: original`, como `route-etapa1`.

```sh
scripts/dev.sh run --content <carpeta> --autotest tests/autotest/route-keyboard.yaml --shots <carpeta de capturas>
```

El juego se cierra del todo (escena, UI, renderer y dispositivo) antes de juzgar la ejecución, para que cuente lo
que se informa al destruir. Sale con 0 si todos los pasos se cumplen, el registro no tiene ninguna línea `[error]`
(errores de scripts de UI, recursos que no cargan...) y, con la capa de validación activa (build de depuración), ésta
no ha dicho nada; si no, con 1 y el motivo (`autotest: FAIL: ...`). Sale con 2 (`PASS (partial: skipped ...)`) si
todo se cumple pero `--skip-steps` ha saltado pasos `walk_to` o `wait`, que son los que mueven al jugador y comprueban
el campo. El registro dice siempre el estado de la capa: `on` (y cuántos mensajes), `unavailable` (pedida pero no
instalada) u `off` (release); sin capa no se cuentan mensajes. También dice cuántas líneas `[error]` hubo, el tiempo
de cada paso, cada cambio de mapa (lectura y total, en ms) y los tiempos de frame (`all` con todos los frames,
`steady` sin los que son lentos a propósito: carga de mapa, cambio de tamaño, captura): frames, fps medios, mínimos y
percentil 99. Las rutas comprueban que el partner sigue al jugador dentro de cada mapa (`partner_within` tras un
`walk_to`), no solo al entrar, donde aparece siempre cerca.

Una ventana no puede ser mayor que la parte usable del escritorio: en una pantalla de 1920x1080 con paneles, los
pasos `resize` a 1920x1080 y 2560x1080 se saltan con un aviso (`resize to ... skipped: the desktop's usable area
is ...`) y el informe los lista. Para cubrirlos, un Xvfb con Vulkan por software (lavapipe) sirve para comprobar el
funcionamiento, no el rendimiento. Sin gestor de ventanas no hay pantalla completa: si a mitad de su `timeout` la
ventana no ha cambiado ni de modo ni de tamaño, el paso `fullscreen` se salta solo con un aviso (`fullscreen on
skipped: the window manager did not apply it`), y `--skip-steps fullscreen` lo salta sin esperar:

```sh
Xvfb :23 -screen 0 2560x1440x24 -nolisten tcp &
DISPLAY=:23 VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json scripts/dev.sh run ... --autotest ... \
    --skip-steps fullscreen
```

### Rutas sobre el paquete cocinado

Las rutas de arriba corren sobre el contenido extraído (PNG, WAV, glTF), pero un jugador carga el paquete cocinado
(KTX2, Opus) y empaquetado en un `.pak`. Si `dwx cook` convierte algo que el juego necesita leer de otra forma, el
contenido extraído no lo nota: así se perdieron los fondos de los mapas y la fuente, que se cocinaron a KTX2 y ya no
cargaban. Por eso el gate cocina la extracción y corre algunas rutas sobre el resultado.

Cocina, empaqueta y comprueba dentro del contenedor. Necesita un build del preset; con un preset que no compila para
Linux (`windows-cross`) usa el `dwx` del build `ci`:

```sh
DWE_CONTENT=<carpeta extraída> DWE_COOKED_DIR=<salida> PRESET=ci scripts/dev.sh cook-content
```

Lee `DWE_CONTENT/base` y escribe en `DWE_COOKED_DIR` (por defecto `build/cooked-content`; fuera del repositorio se
monta en el contenedor en la misma ruta). Antes de escribir nada rechaza una salida que esté dentro de, o que
contenga, `DWE_CONTENT`, la carpeta del disco, `DWE_DECOMP`, `~/code/assets` o las carpetas protegidas del equipo
(`_work/content*`, `_work/p2/content`, `_work/p2/scratch-routes`, `_work/p3-inventory`, `_work/saves-play`,
`_work/mouse-route`, `_work/validate-runtime`, `_work/viz` y `build/verify-p1` del checkout principal), también a
través de enlaces simbólicos, y la propia carpeta del repositorio. Si git no le dice dónde está el checkout principal,
no cocina: sin él no sabe qué carpetas proteger. Escribe:

- `folder/base`: el paquete cocinado (`dwx cook`). **A mano, el cocinado es incremental:** la segunda vez solo cocina
  lo que ha cambiado en la extracción y conserva lo demás. Si cambias `dwx` (un cocinero, `PngOnlyImages`), borra
  `DWE_COOKED_DIR` antes, o el resultado puede ser de un cocinado anterior.
- `archive/base.pak`: ese paquete en un archivo (`dwx pack`). Se borra antes de cocinar, así que un cocinado que falla
  no deja un `.pak` viejo.

Después pasa `dwx validate --engine` por el paquete cocinado y por el paquete `game` del repositorio montado sobre el
`.pak` (así comprueba también el `.pak`); las dos tienen que acabar en `0 problems`. Tarda unos 8 minutos con el
preset `ci` la primera vez y unos 5 después.

`check-full` hace este paso solo (`cook-content.log`) cuando le das `DWE_CONTENT` y el preset compila para Linux; con
`windows-cross` lo salta y lo dice. No usa `DWE_COOKED_DIR`: cocina desde cero en `build/gate-cooked`, que vacía
antes, para que nada de un cocinado anterior tape un fallo. Después corre estas rutas sobre el `.pak`, en los dos
idiomas, en el mismo Xvfb que las demás (`<cocinado>` es `build/gate-cooked` tras `check-full`, o tu
`DWE_COOKED_DIR`):

```sh
for lang in en es; do
    scripts/dev.sh run --content <cocinado>/archive --mods tests/mods/example_mod --lang $lang \
        --autotest tests/autotest/route-mod-map.yaml --skip-steps fullscreen
    scripts/dev.sh run --content <cocinado>/archive --text-font original --lang $lang \
        --autotest tests/autotest/route-fade.yaml --skip-steps fullscreen
done
```

Entre las dos cubren lo que el juego lee de su PNG (`assets::PngOnlyImages`) y lo demás del paquete cocinado:

- `route-mod-map`: el mapa de un mod (en PNG, sin cocinar) sobre la base cocinada, y su salida a File City: los fondos y
  las páginas de textura de `twna01` desde el `.pak`, y los modelos del jugador y del compañero.
- `route-fade` con `--text-font original`: los fondos y las páginas de textura de `twna01` y `room08`, con la paleta
  de la hora, y el nombre del mapa durante el fundido dibujado con la fuente de mapa de bits (en `es`, con los
  caracteres que le faltan desde la moderna).

Además de `autotest: PASS`, 0 mensajes de la capa de validación y 0 líneas `[error]`, el registro de cada una tiene
que decir `ui: original bitmap font font/main: ...`: si el atlas de la fuente no carga, el juego solo avisa
(`[warning]`) y sigue con la moderna, así que la ruta pasaría igual.

En Windows no hay contenedor: los mismos pasos con `dwx` del build de Windows
(`build\windows-clang-debug\apps\dwx\dwx.exe cook <carpeta extraída>\base -o <salida>\folder\base`, luego `pack` y
`validate`), y el juego con `--content <salida>\archive`.

### Las ejecuciones del gate

Un gate corre, sobre el tip exacto, en Xvfb con lavapipe y con `--skip-steps fullscreen`:

- [ ] Todas las rutas de `tests/autotest/route-*.yaml` (21) sobre el contenido extraído, en `en` y en `es`: 42
  ejecuciones. `route-dev-assets` con `--dev-assets`, `route-mod-map` con `--mods tests/mods/example_mod`,
  `route-skins` con `--mods tests/mods/ui_skin` y la pareja `route-save-load` → `route-save-load-check` con una
  carpeta de partidas propia.
- [ ] `check-full` con `DWE_CONTENT` (paso `cook-content`, desde cero en `build/gate-cooked`) con `0 problems` en
  las dos comprobaciones.
- [ ] `route-mod-map` y `route-fade` sobre el paquete cocinado, en `en` y en `es`: 4 ejecuciones.

En total, **46 ejecuciones**. Cada una acaba en `autotest: PASS`, con la capa de validación en 0 mensajes y
`autotest: 0 [error] lines in the log`; las del paquete cocinado, además, con la fuente de mapa de bits cargada. Si
añades una ruta, súmala aquí.

`scripts/gate.sh --tier daily` corre todo esto por ti: el check, el build de Windows y las 46 ejecuciones, una detrás
de otra o en varias pantallas de Xvfb a la vez. El gate de subida de cada paquete es el corto
(`scripts/gate.sh`): las rutas de humo y las del paquete. Los dos están en [pasar el gate](gate.md). Si añades una
ruta que necesita opciones propias (`--mods`, `--dev-assets`), añádelas también en `route_job` de `scripts/gate.sh`.

### Formato de una ruta

```yaml
name: keyboard            # prefijo de las capturas
device: keyboard          # keyboard | gamepad | mouse: el de los pasos que no digan otro
gamepad: xbox             # familia del mando virtual (xbox, playstation, nintendo...)
timeout: 600              # segundos para toda la ruta
main_menu: false          # true: empieza en el menú principal en vez de en el campo
boot_screen: false        # true (con main_menu): la portada empieza en «Press Start»
language_first: false     # true (con boot_screen): antes, la caja de idiomas, como al arrancar el juego
new_game_rules: false     # true (con main_menu): la partida nueva pasa por la pantalla de reglas (si no, empieza con las del original)
new_game: seeded          # original: la partida nueva corre la apertura (por defecto seeded, el campo libre)
load_save: save.yaml      # empieza cargando esa partida (relativa a la ruta), sin main_menu ni new_game
movies: false             # true: las películas del juego salen como sin ruta (los créditos del final)
text_fit: false           # true: cada captura comprueba además que los textos caben (como expect_text_fits: all)
steps:
  - wait: {map: twna01, seconds: 1}
  - walk_to: {trigger: 112}
  - screenshot: room08
```

| Paso | Qué hace |
|---|---|
| `wait: {seconds, map, partner_within, player_near: [x, z], radius, menu_open, map_name, shown, hidden, partner_has, setting, partner_model, timeout}` | Espera los segundos y hasta que se cumplen las condiciones (todas las dadas). `map_name: <id>` espera a que un cambio de mapa enseñe el nombre de ese mapa, y entonces no mira las demás. `shown: <documento>` y `hidden: <documento>` esperan a que un documento de la UI (`battle`, `partner_menu`) se vea o deje de verse. `partner_has: <clave>` espera a que el compañero tenga ese estado, por la clave de la página de estado (`partner_menu.condition.sleepy`, `.hungry`, `.needs_toilet`, `.sick`...). `setting: {<nombre>: <valor>}` espera a que un ajuste tenga ese valor (`battle.phase: end`). `partner_model: <id>` espera a que el compañero se dibuje con ese modelo (`digimon/species_b/model`). `ticks: <n>` espera esos ticks de lógica (1/60 s cada uno) en vez de segundos de reloj |
| `wait: {confirm, win_after, ...}` | Además, `confirm: true` pulsa confirmar mientras espera, cada segundo, en el cuadro de texto de la historia (no en un elegidor, cuyo cursor se ve: ése lo contesta la ruta) y en el cuadro final de un combate, que se juega solo. `win_after: <s>` (con `confirm`) acaba con `battle_win` un combate que lleva ese tiempo en pantalla, para los jefes que el compañero de una partida escrita no gana |
| `hold: {keys: [<tecla>...], ticks}` | Mantiene esas teclas pulsadas exactamente esos ticks de lógica (`keys: []`, ninguna) y las suelta después; los `hold` seguidos se encadenan sin huecos, tick a tick, aunque los frames vayan lentos. Para repetir una entrada al tick, como en la [traza de estado](traza-de-estado.md) |
| `walk_to: {trigger \| point: [x, z], radius, walk, stop_on_warp, stop_on_scene, stop_on_closed, encounters, device, timeout}` | Camina con el dispositivo hasta las celdas con ese trigger (y el cambio de mapa que dispara) o hasta el punto; planifica con la colisión del mapa y evita los demás warps. `walk: true` anda en vez de correr. `stop_on_warp: true` acaba al empezar el cambio de mapa, sin esperar al mapa nuevo. `stop_on_scene: true` acaba en cuanto un script de la historia sujeta al jugador por el camino (la escena de una casilla de sección) y deja la escena en marcha para los pasos siguientes. `stop_on_closed: true` acaba cuando el candado de historia deja cerrada la salida y muestra su aviso (el diálogo `dialog`): el jugador sigue en el mapa. Si pisa un objeto del suelo, confirma su aviso como lo haría el jugador y sigue. `encounters: true` aguanta un encuentro por el camino, que llega cuando quiere el tiempo (un monstruo salvaje se acerca, habla y empieza un combate): mientras un script sujeta al jugador o se ve el combate, deja de andar, pasa las páginas del cuadro de texto con confirmar (Intro, o South con el mando) cada segundo y deja que el combate acabe solo, pulsando confirmar también en su cuadro final, que escribe cada línea con una pulsación (el de un script no deja huir, como en el original; si se pierde, el jugador acaba en casa de Jijimon y la ruta lo verá); luego planifica de nuevo desde donde quedó el jugador. Ese tiempo no cuenta para el `timeout` del paso, sí para el de la ruta. Con `encounters: win` el combate del encuentro acaba ganado en cuanto empieza (como `battle_win`), para que el compañero de la ruta no pierda por el camino. Un elegidor de destinos de otra celda por el camino (el camino vuelve a pisar la celda en la que el jugador se quedó) se rechaza con cancelar y la caminata sigue |
| `menu: open \| close` | Abre o cierra el menú con el dispositivo (ratón: el botón «Menú» del HUD y clic derecho) |
| `menu_select: <id>` | Enfoca y pulsa una entrada del menú abierto: el principal si se ve (`play`, `language`), si no el de pausa (`open-minimap`, `minimap-mode`...), si no el menú del compañero (también la bolsa que abre el combate: `slot-0`..., `opt-use`), la pantalla de combate (`command-0`..., `open-bag`), el selector de la exploración libre o, en la portada, el diálogo (`yes`, `no`, `ok`) y la lista de ranuras (`slot-1`..., `no-save`, `back`). Con teclado o mando mueve el foco abajo, arriba, a la derecha y a la izquierda hasta llegar; con el ratón hace clic en el centro de la entrada |
| `script: <lua>` | Ejecuta una línea de los scripts del juego, como la consola; los comandos de depuración están más abajo |
| `script_if: {script, <condiciones de wait>}` | Ejecuta el script solo si se cumplen ya todas las condiciones (las de `wait` salvo `map_name` y `seconds`); si no, sigue sin esperar y lo dice en el registro |
| `expect_setting: {<nombre>: <valor>, timeout}` | Comprueba un ajuste publicado a la UI (`minimap: "off"`) o del estado del compañero; `field.overlaps` es cuántas veces dos personajes (o uno y una pared) se pisaron más de un segundo con el jugador libre (`expect_setting: {field.overlaps: 0}`); `saves.files` es cuántos ficheros hay en la carpeta de `--saves` (0 sin ella); `has_class.<documento>#<elemento>.<clase>` (`"true"` o `"false"`) dice si ese elemento tiene esa clase, como la pestaña que enseña una página (`has_class.partner_menu#tab-tech.selected`); `story.trigger.<n>` (`"true"` o `"false"`) y `story.var.<n>` leen los disparadores y las variables de la historia, como el original los guarda en su estado de scripts (`wait: {setting: {story.trigger.249: "true"}}` espera a que Coelamon se una); `field.npc.<id>` (`"true"` o `"false"`) dice si el NPC con ese id de script está ahora en el mapa (el Otamamon del pantano sale solo a veces). Si no se cumple, el fallo dice el valor que leyó |
| `remember: <nombre>` | Guarda el valor de un ajuste para un `expect_change` posterior |
| `expect_change: {<nombre>: up \| down \| changed, timeout}` | Comprueba que el ajuste ha subido, bajado o cambiado desde el valor guardado (2 s por defecto) |
| `repeat: {times, steps}` | Repite esos pasos en su sitio tantas veces |
| `when: {<condiciones de wait>, steps}` | Hace esos pasos solo si se cumplen ya todas las condiciones (como `script_if`); si no, se los salta y lo dice en el log. Sirve para lo que el juego echa a suertes al jugarlo (`when: {setting: {story.var.116: "1"}, steps: [...]}`) |
| `screenshot: <nombre>` | Guarda `<ruta>-<NN>-<nombre>-<ancho>x<alto>.png` en `--shots` |
| `expect_text_fits: <documento> \| all` | Comprueba que cada texto que se ve en ese documento de la UI (o en todos) cabe: dentro de la caja dibujada que lo rodea, de lo que lo recorta y de la ventana, sin cruzar el borde de otra caja ni pisar otro texto, con un píxel del original (3dp) de margen. Lo que tapa una caja opaca posterior no cuenta, ni una caja que se abre o se cierra. Espera hasta 1 s a que la caja quede en su sitio; si sigue sin caber, escribe cada texto en el log (`in <documento>, '<texto>' (<elemento>) is out of <caja> by <n> dp`) y la ruta sigue, pero acaba fallando |
| `expect_clear: <documento>` | Comprueba que nada de lo que se ve de ese documento (texto, caja, imagen) pisa lo que dibuja otro documento mostrado (`Ui::drawnRects`, sin los velos que cubren media ventana o más), como el aviso de guardado. Espera hasta 1 s; si sigue pisando, escribe en el log la primera pareja de rectángulos y la ruta acaba fallando, como `expect_text_fits`. Con `<documento>#<elemento>` comprueba solo ese elemento, que tiene que verse, contra lo demás de su documento y de los otros (`Ui::drawnRectsAround`, sin las cajas que lo rodean entero, su panel), como la hora en dígitos del HUD (`hud#field-time`) |
| `resize: [ancho, alto]` | Cambia la ventana y espera a que el render tenga ese tamaño |
| `fullscreen: true \| false` | Pantalla completa |
| `press`, `release`, `tap: {key \| button}`, `axis: {axis, value}`, `click: [x, z]` | Controles sueltos: teclas, botones y ejes del mando virtual, clic en un punto del suelo |

Las coordenadas son del mundo del motor (x, z en el suelo; 100 unidades por celda de colisión).

Los ajustes del compañero se leen en vivo, con el menú del compañero abierto o no: `partner.name` (la clave de su
nombre, igual en cualquier idioma), `partner.age`, `partner.weight`, `partner.lives`, `partner.hp`, `partner.max_hp`,
`partner.mp`, `partner.max_mp`, `partner.stat.<0-3>.value` (ataque, defensa, velocidad e inteligencia, en el orden de
la página de estado), `partner.meter.<0-2>.value` (felicidad, disciplina y virus) y `partner.condition.count` con
`partner.condition.<n>.key`. La bolsa publica lo que pasó con el último objeto en `bag.result`.

Comandos de depuración de los scripts, que también sirven en la consola. Hacen lo mismo que la acción del jugador y
no añaden reglas; sustituyen a los baños, las camas, las pantallas de entrenamiento y las opciones del reloj hasta la
fase 4:

| Comando | Qué hace |
|---|---|
| `battle("")` | Empieza un combate contra el primer NPC del mapa que puede luchar; `battle("2,3")`, contra esos NPC del mapa (su número en la lista de NPC del mapa). Con `story:` delante (`battle("story:2")`) es un combate de la historia: no se puede huir y los enemigos no gastan MP |
| `place("<quién> <x> <z> <rotación>")` | Pone al jugador (`player`), al compañero (`partner`) o al NPC de ese id de script (`npc:6`) en ese punto del suelo y con esa rotación, en los ejes y unidades del original (z hacia dentro de la pantalla, 4096 por vuelta); conserva su altura. Para empezar una comparación con el original en el mismo sitio ([traza de estado](traza-de-estado.md)) |
| `battle_win("")` | Termina el combate en curso con los enemigos vencidos: caen como con un golpe final y siguen la victoria, los objetos y las estadísticas ganadas |
| `battle_lose("")` | Lo contrario: el compañero cae como con un golpe final y el combate se pierde (para probar un torneo perdido) |
| `give_item("<id> <cantidad>")` | Mete objetos en la bolsa (`give_item("meat 3")`) |
| `story_set("trigger <n> on\|off")`, `story_set("var <n> <valor>")`, `story_set("money <bits>")`, `story_set("card <n> <copias>")` | Escribe un disparador o una variable de la historia, como el original los guarda en su estado de scripts, sin correr ningún script (`story_set("var 1 10")`: diez puntos de prosperidad), el dinero que lleva el jugador (de 0 a 999999), o da copias de una carta como `give_card` (`story_set("card 19 1")`). Para empezar una ruta en un punto de la historia al que la partida sembrada no llega |
| `evolve("<especie>")` | Hace evolucionar al compañero a esa forma como un guion de mapa (`evolve("species_b")`); el campo lo dibuja con su modelo |
| `use_item("<id>")` | Usa uno de la bolsa con el compañero, como «Usar» en la bolsa; el compañero puede rechazarlo |
| `drop_item("<id>")` | Deja ese objeto en el suelo a los pies del jugador, como la caída de un enemigo vencido (`drop_item("meat")`) |
| `toilet("")` | Lleva al compañero al baño |
| `sleep("")` | Lo acuesta, si tiene sueño |
| `scold("")` | Lo regaña, como «Regañar» en la pestaña de cuidados |
| `train("<lugar> <curso>")` | Una sesión de entrenamiento (`train("trn brain")`) |
| `partner_set("<valor> <número>")` | Escribe un valor del compañero, de 0 a 99999: `hp`, `mp`, `offense`, `defense`, `speed`, `brains` (los stats base, HP y MP en sus unidades), `weight`, `care` (errores de cuidado), `battles`, `hours` (el contador de evolución) y `discipline` (disciplina, dentro de los límites del ánimo) (`partner_set("hours 71")`). Para dejar al compañero justo antes de una revisión de evolución |
| `learn_move("<técnica> [hueco]")` | El compañero aprende esa técnica (y las que vienen con ella) como en el entrenamiento de inteligencia y, con un hueco de 0 a 3, la equipa ahí (`learn_move("meltdown 0")`), para ver una técnica que aún no sabe |
| `clock_speed("<multiplicador>")` | Acelera el reloj: 1 es un minuto de juego por segundo real, 600 son diez horas |
| `save("slot_NN")` | Guarda la partida en ese hueco manual (`save("slot_01")`), hasta que exista la pantalla de guardar; los huecos de autoguardado no se escriben a mano. Un autotest solo guarda con `--saves` |
