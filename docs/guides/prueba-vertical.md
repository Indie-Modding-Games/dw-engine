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
| `--mods <carpeta>` | Más paquetes, como mods; se puede repetir. La VFS los apila por capa y prioridad |
| `--ruleset <id>` | El ruleset con el que se juega, `rules/<id>/` de los paquetes (por defecto `original`, ver [rulesets](../architecture/rulesets.md)) |
| `--size <ancho>x<alto>` | Tamaño de la ventana (por defecto 1280x720) |
| `--fullscreen` | Empieza a pantalla completa |
| `--no-vsync` | Presenta cada frame en cuanto está listo (para medir fps) |
| `--start <mapa>[,<salida>]` | Empieza en otro mapa y salida |
| `--time day\|dusk\|night` | Pone el reloj del juego a las 8:00, 16:00 o 20:00 (por defecto, la hora de la partida: las 8:00 de la sección `clock` en una nueva) |
| `--new-game` | Empieza una partida nueva (es lo que hace sin `--load`): el partner, las estadísticas y el nivel de domador de la sección `new_game` del ruleset |
| `--load <hueco>` | Carga una partida guardada, `slot_01` o `autosave_01` de la carpeta de partidas, y empieza donde se guardó (salvo con `--start`) |
| `--clock-speed <x>` | Velocidad del reloj del juego: 1 la del original (un minuto de juego por segundo real), 60 una hora de juego por segundo real |
| `--seed <n>` | La semilla de los números aleatorios de una partida nueva (el partner y los sistemas: combates, objetos, evolución), para que una ruta juegue igual cada vez. Sin ella, el juego toma una del reloj y la escribe en el log (`game: new game, seed <n>`), así una partida se puede repetir. Con `--load` no cambia nada: la partida cargada sigue con los números que guardó |
| `--saves <carpeta>` | Dónde lee y guarda las partidas (por defecto la carpeta de datos del usuario). Un autotest solo guarda con ella |
| `--text-font modern\|original` | La fuente del texto con aspecto original (el nombre del mapa), en vez de la elegida en el menú principal o la de los paquetes; para autotests y capturas, no se guarda (ver [UI](../architecture/ui.md#aspecto-original)) |
| `--autotest <ruta.yaml>` | Recorre una ruta y sale con 0 si pasa (ver abajo) |
| `--shots <carpeta>` | Dónde guarda las capturas de un autotest |
| `--lang en\|es` | El idioma de esta ejecución, para autotests; no se guarda. Sin él, el juego usa el que se eligió en el menú principal o, la primera vez, el del sistema ([localización](../architecture/localizacion.md)) |
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
| Menú | Tab o el botón «Menú» del HUD | Start |
| Aceptar / volver en el menú | Enter, Espacio o clic / Esc, Retroceso o clic derecho | Sur / Este |
| Consola de depuración | F1 o `º` | |

Correr es lo normal, como en el original (allí R1 hacía andar). Los avisos de botones del HUD y del menú cambian al
último dispositivo usado. El menú para el campo mientras está abierto.

La lógica va a 60 ticks por segundo fijos; el dibujo interpola entre ticks y las animaciones avanzan por frame. Como
en el original, el reloj del juego avanza un minuto por segundo y el fondo cambia de paleta a las 6:00, 16:00 y
20:00, y al cruzar una puerta la pantalla se oscurece restando un gris durante un segundo mientras el jugador sigue
andando, con el nombre del mapa nuevo en el centro en la segunda mitad, y vuelve en otro segundo
([ficha del campo](../reference/campo.md)).

## Autotests

Un autotest recorre una ruta escrita en YAML con los mismos eventos que un jugador: las teclas y el ratón entran
como eventos de la plataforma y el mando es un mando virtual de SDL, así que pasan por el mapa de acciones, la UI y
el campo de verdad. Mientras corre, el juego descarta el ratón y el teclado reales del escritorio (el puntero de quien
use el ordenador, o el que descansa sobre la ventana, movería el puntero bajo un clic o una pulsación sostenida), así
que se puede seguir usando el escritorio; un mando real sí llega. Las rutas de la prueba están en `tests/autotest/`:

- `route-keyboard.yaml`, `route-gamepad.yaml` y `route-mouse.yaml`: File City → casa del anciano → File City con
  cada dispositivo; abren el menú con ese dispositivo, ocultan y vuelven a mostrar el minimapa, se paran detrás de
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

- `route-loop-keyboard.yaml`: el bucle de la fase 3 con el teclado, desde el menú principal: cría al compañero
  (lo regaña, espera a que tenga hambre y le da su comida favorita desde la bolsa, lo lleva al baño y lo acuesta),
  lo entrena, combate con el disparador de depuración y, con el reloj acelerado, lo cuida a tramos hasta que
  evoluciona. Cada paso comprueba su efecto en el estado del compañero; hace capturas de cada etapa. Tarda unos dos
  minutos con lavapipe. Se corre en los dos idiomas, como `route-language.yaml`.

Un autotest empieza en el campo, sin el menú principal, salvo que la ruta diga `main_menu: true`. No lee ni guarda
los ajustes del usuario.

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

### Formato de una ruta

```yaml
name: keyboard            # prefijo de las capturas
device: keyboard          # keyboard | gamepad | mouse: el de los pasos que no digan otro
gamepad: xbox             # familia del mando virtual (xbox, playstation, nintendo...)
timeout: 600              # segundos para toda la ruta
main_menu: false          # true: empieza en el menú principal en vez de en el campo
steps:
  - wait: {map: twna01, seconds: 1}
  - walk_to: {trigger: 112}
  - screenshot: room08
```

| Paso | Qué hace |
|---|---|
| `wait: {seconds, map, partner_within, player_near: [x, z], radius, menu_open, map_name, shown, hidden, partner_has, setting, partner_model, timeout}` | Espera los segundos y hasta que se cumplen las condiciones (todas las dadas). `map_name: <id>` espera a que un cambio de mapa enseñe el nombre de ese mapa, y entonces no mira las demás. `shown: <documento>` y `hidden: <documento>` esperan a que un documento de la UI (`battle`, `partner_menu`) se vea o deje de verse. `partner_has: <clave>` espera a que el compañero tenga ese estado, por la clave de la página de estado (`partner_menu.condition.sleepy`, `.hungry`, `.needs_toilet`, `.sick`...). `setting: {<nombre>: <valor>}` espera a que un ajuste tenga ese valor (`battle.phase: end`). `partner_model: <id>` espera a que el compañero se dibuje con ese modelo (`digimon/grey/model`) |
| `walk_to: {trigger \| point: [x, z], radius, walk, stop_on_warp, device, timeout}` | Camina con el dispositivo hasta las celdas con ese trigger (y el cambio de mapa que dispara) o hasta el punto; planifica con la colisión del mapa y evita los demás warps. `walk: true` anda en vez de correr. `stop_on_warp: true` acaba al empezar el cambio de mapa, sin esperar al mapa nuevo |
| `menu: open \| close` | Abre o cierra el menú con el dispositivo (ratón: el botón «Menú» del HUD y clic derecho) |
| `menu_select: <id>` | Enfoca y pulsa una entrada del menú abierto: el principal si se ve (`play`, `language`), si no el de pausa (`open-minimap`, `minimap-visible`...), si no la pantalla de combate (`command-0`..., `open-bag` y, con la bolsa abierta, `bag-slot-0`...) o el menú del compañero. Con teclado o mando mueve el foco abajo, arriba, a la derecha y a la izquierda hasta llegar; con el ratón hace clic en el centro de la entrada |
| `script: <lua>` | Ejecuta una línea de los scripts del juego, como la consola; los comandos de depuración están más abajo |
| `script_if: {script, <condiciones de wait>}` | Ejecuta el script solo si se cumplen ya todas las condiciones (las de `wait` salvo `map_name` y `seconds`); si no, sigue sin esperar y lo dice en el registro |
| `expect_setting: {<nombre>: <valor>, timeout}` | Comprueba un ajuste publicado a la UI (`minimap_visible: false`) o del estado del compañero |
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
| `battle("")` | Empieza un combate contra el primer NPC del mapa que puede luchar; `battle("2,3")`, contra esos NPC del mapa |
| `give_item("<id> <cantidad>")` | Mete objetos en la bolsa (`give_item("meat 3")`) |
| `evolve("<especie>")` | Hace evolucionar al compañero a esa forma como un guion de mapa (`evolve("grey")`); el campo lo dibuja con su modelo |
| `use_item("<id>")` | Usa uno de la bolsa con el compañero, como «Usar» en la bolsa; el compañero puede rechazarlo |
| `toilet("")` | Lleva al compañero al baño |
| `sleep("")` | Lo acuesta, si tiene sueño |
| `scold("")` | Lo regaña, como «Regañar» en la pestaña de cuidados |
| `train("<lugar> <curso>")` | Una sesión de entrenamiento (`train("trn brain")`) |
| `clock_speed("<multiplicador>")` | Acelera el reloj: 1 es un minuto de juego por segundo real, 600 son diez horas |
