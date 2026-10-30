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

Las vistas de desarrollo siguen disponibles: `--test-scene`, `--map <id>`, `--model-viewer`, `--ui-demo` y
`--check-content <carpeta>`.

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
  completa, y hacen capturas.
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
  título y su menú, el reloj y las barras del campo, el menú de pausa y sus opciones, la ficha y la bolsa del
  compañero (con las opciones de un objeto y los órdenes), un combate y sus cuadros finales, y la burbuja del hambre
  sobre el compañero a 1920x1080. Se corre en los dos idiomas.
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
- `route-etapa2.yaml` y `route-etapa2-jungla.yaml`: la Etapa 2 de la historia, con el candado abierto solo para la
  prueba (`--story-stage stage_2`, sin tocar `active_stage`) y una partida sembrada que empieza en el mapa de cada
  mitad. La del Drill Tunnel (`--start tunn06`) evoluciona al compañero a Champion, habla con el minero, ve rodar la
  roca (disparador 120), sigue a tunn07 y tunn08, donde tiembla la pantalla y empieza el combate con el monstruo de
  fuego, lo gana con `battle_win` y acaba con el disparador 209. La de la jungla (`--start trop04`) choca con el
  Betamon, que vuelve a la ciudad (204), pasa por trop01 y trop05 (en trop05 coloca al jugador junto a la salida, para
  no pelear con los salvajes del camino) y entra en el bosque de Amida (alerta, 137) y sube por su camino central:

```sh
scripts/dev.sh run --content <carpeta> --story-stage stage_2 --start tunn06 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa2.yaml --shots <capturas>/etapa2
scripts/dev.sh run --content <carpeta> --story-stage stage_2 --start trop04 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa2-jungla.yaml --shots <capturas>/etapa2
```

- Los reclutamientos de la Etapa 2, una ruta cada uno, con el mismo candado y la partida sembrada; cada una comprueba
  el disparador del reclutamiento con `story.trigger.<n>`. `route-etapa2-coelamon.yaml` (`--start trop00`) cruza las
  casillas del puente roto de la jungla (disparador 185), vuelve por mayo08a, mayo02 y mayo04b a mayo05 y pisa la
  orilla, donde Coelamon sube del río y se une (249).
  `route-etapa2-drimogemon.yaml` (`--start tunn09`) habla con Drimogemon en la mina (disparador 353), deja pasar un
  día cuidando al compañero con el reloj rápido (la variable 30 cuenta los días) y vuelve a hablarle: se une (238).

```sh
scripts/dev.sh run --content <carpeta> --story-stage stage_2 --start trop00 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa2-coelamon.yaml --shots <capturas>/etapa2
scripts/dev.sh run --content <carpeta> --story-stage stage_2 --start tunn09 --lang en --seed 1 \
    --autotest tests/autotest/route-etapa2-drimogemon.yaml --shots <capturas>/etapa2
```

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
funcionamiento, no el rendimiento. Sin gestor de ventanas no hay pantalla completa, así que ese paso se salta:

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
new_game: seeded          # original: la partida nueva corre la apertura (por defecto seeded, el campo libre)
load_save: save.yaml      # empieza cargando esa partida (relativa a la ruta), sin main_menu ni new_game
steps:
  - wait: {map: twna01, seconds: 1}
  - walk_to: {trigger: 112}
  - screenshot: room08
```

| Paso | Qué hace |
|---|---|
| `wait: {seconds, map, partner_within, player_near: [x, z], radius, menu_open, map_name, shown, hidden, partner_has, setting, partner_model, timeout}` | Espera los segundos y hasta que se cumplen las condiciones (todas las dadas). `map_name: <id>` espera a que un cambio de mapa enseñe el nombre de ese mapa, y entonces no mira las demás. `shown: <documento>` y `hidden: <documento>` esperan a que un documento de la UI (`battle`, `partner_menu`) se vea o deje de verse. `partner_has: <clave>` espera a que el compañero tenga ese estado, por la clave de la página de estado (`partner_menu.condition.sleepy`, `.hungry`, `.needs_toilet`, `.sick`...). `setting: {<nombre>: <valor>}` espera a que un ajuste tenga ese valor (`battle.phase: end`). `partner_model: <id>` espera a que el compañero se dibuje con ese modelo (`digimon/species_b/model`). `ticks: <n>` espera esos ticks de lógica (1/60 s cada uno) en vez de segundos de reloj |
| `hold: {keys: [<tecla>...], ticks}` | Mantiene esas teclas pulsadas exactamente esos ticks de lógica (`keys: []`, ninguna) y las suelta después; los `hold` seguidos se encadenan sin huecos, tick a tick, aunque los frames vayan lentos. Para repetir una entrada al tick, como en la [traza de estado](traza-de-estado.md) |
| `walk_to: {trigger \| point: [x, z], radius, walk, stop_on_warp, stop_on_scene, encounters, device, timeout}` | Camina con el dispositivo hasta las celdas con ese trigger (y el cambio de mapa que dispara) o hasta el punto; planifica con la colisión del mapa y evita los demás warps. `walk: true` anda en vez de correr. `stop_on_warp: true` acaba al empezar el cambio de mapa, sin esperar al mapa nuevo. `stop_on_scene: true` acaba en cuanto un script de la historia sujeta al jugador por el camino (la escena de una casilla de sección) y deja la escena en marcha para los pasos siguientes. Si pisa un objeto del suelo, confirma su aviso como lo haría el jugador y sigue. `encounters: true` aguanta un encuentro por el camino, que llega cuando quiere el tiempo (un monstruo salvaje se acerca, habla y empieza un combate): mientras un script sujeta al jugador o se ve el combate, deja de andar, pasa las páginas del cuadro de texto con confirmar (Intro, o South con el mando) cada segundo y deja que el combate acabe solo (el de un script no deja huir, como en el original; si se pierde, el jugador acaba en casa de Jijimon y la ruta lo verá); luego planifica de nuevo desde donde quedó el jugador. Ese tiempo no cuenta para el `timeout` del paso, sí para el de la ruta. Un elegidor de destinos de otra celda por el camino (el camino vuelve a pisar la celda en la que el jugador se quedó) se rechaza con cancelar y la caminata sigue |
| `menu: open \| close` | Abre o cierra el menú con el dispositivo (ratón: el botón «Menú» del HUD y clic derecho) |
| `menu_select: <id>` | Enfoca y pulsa una entrada del menú abierto: el principal si se ve (`play`, `language`), si no el de pausa (`open-minimap`, `minimap-mode`...), si no el menú del compañero (también la bolsa que abre el combate: `slot-0`..., `opt-use`), la pantalla de combate (`command-0`..., `open-bag`), el selector de la exploración libre o, en la portada, el diálogo (`yes`, `no`, `ok`) y la lista de ranuras (`slot-1`..., `no-save`, `back`). Con teclado o mando mueve el foco abajo, arriba, a la derecha y a la izquierda hasta llegar; con el ratón hace clic en el centro de la entrada |
| `script: <lua>` | Ejecuta una línea de los scripts del juego, como la consola; los comandos de depuración están más abajo |
| `script_if: {script, <condiciones de wait>}` | Ejecuta el script solo si se cumplen ya todas las condiciones (las de `wait` salvo `map_name` y `seconds`); si no, sigue sin esperar y lo dice en el registro |
| `expect_setting: {<nombre>: <valor>, timeout}` | Comprueba un ajuste publicado a la UI (`minimap: "off"`) o del estado del compañero; `field.overlaps` es cuántas veces dos personajes (o uno y una pared) se pisaron más de un segundo con el jugador libre (`expect_setting: {field.overlaps: 0}`); `story.trigger.<n>` (`"true"` o `"false"`) y `story.var.<n>` leen los disparadores y las variables de la historia, como el original los guarda en su estado de scripts (`wait: {setting: {story.trigger.249: "true"}}` espera a que Coelamon se una) |
| `remember: <nombre>` | Guarda el valor de un ajuste para un `expect_change` posterior |
| `expect_change: {<nombre>: up \| down \| changed, timeout}` | Comprueba que el ajuste ha subido, bajado o cambiado desde el valor guardado (2 s por defecto) |
| `repeat: {times, steps}` | Repite esos pasos en su sitio tantas veces |
| `screenshot: <nombre>` | Guarda `<ruta>-<NN>-<nombre>-<ancho>x<alto>.png` en `--shots` |
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
| `give_item("<id> <cantidad>")` | Mete objetos en la bolsa (`give_item("meat 3")`) |
| `evolve("<especie>")` | Hace evolucionar al compañero a esa forma como un guion de mapa (`evolve("species_b")`); el campo lo dibuja con su modelo |
| `use_item("<id>")` | Usa uno de la bolsa con el compañero, como «Usar» en la bolsa; el compañero puede rechazarlo |
| `drop_item("<id>")` | Deja ese objeto en el suelo a los pies del jugador, como la caída de un enemigo vencido (`drop_item("meat")`) |
| `toilet("")` | Lleva al compañero al baño |
| `sleep("")` | Lo acuesta, si tiene sueño |
| `scold("")` | Lo regaña, como «Regañar» en la pestaña de cuidados |
| `train("<lugar> <curso>")` | Una sesión de entrenamiento (`train("trn brain")`) |
| `clock_speed("<multiplicador>")` | Acelera el reloj: 1 es un minuto de juego por segundo real, 600 son diez horas |
| `save("slot_NN")` | Guarda la partida en ese hueco manual (`save("slot_01")`), hasta que exista la pantalla de guardar; los huecos de autoguardado no se escriben a mano. Un autotest solo guarda con `--saves` |
