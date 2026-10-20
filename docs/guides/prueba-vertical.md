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
| `--size <ancho>x<alto>` | Tamaño de la ventana (por defecto 1280x720) |
| `--fullscreen` | Empieza a pantalla completa |
| `--no-vsync` | Presenta cada frame en cuanto está listo (para medir fps) |
| `--start <mapa>[,<salida>]` | Empieza en otro mapa y salida |
| `--time day\|dusk\|night` | Pone el reloj del juego a las 8:00, 16:00 o 20:00 (por defecto, la hora de inicio de `field/rules`) |
| `--autotest <ruta.yaml>` | Recorre una ruta y sale con 0 si pasa (ver abajo) |
| `--shots <carpeta>` | Dónde guarda las capturas de un autotest |
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
20:00, y al cruzar una puerta la pantalla se funde a negro un segundo mientras el jugador sigue andando, y vuelve en
otro segundo ([ficha del campo](../reference/campo.md)).

## Autotests

Un autotest recorre una ruta escrita en YAML con los mismos eventos que un jugador: las teclas y el ratón entran
como eventos de la plataforma y el mando es un mando virtual de SDL, así que pasan por el mapa de acciones, la UI y
el campo de verdad. Las tres rutas de la prueba están en `tests/autotest/`:

- `route-keyboard.yaml`, `route-gamepad.yaml` y `route-mouse.yaml`: File City → casa del anciano → File City con
  cada dispositivo; abren el menú con ese dispositivo, ocultan y vuelven a mostrar el minimapa, se paran detrás de
  una capa de profundidad de File City, cambian la ventana a 800x600, 1280x720, 1920x1080, 2560x1080 y pantalla
  completa, y hacen capturas.

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
steps:
  - wait: {map: twna01, seconds: 1}
  - walk_to: {trigger: 112}
  - screenshot: room08
```

| Paso | Qué hace |
|---|---|
| `wait: {seconds, map, partner_within, player_near: [x, z], radius, menu_open, timeout}` | Espera los segundos y hasta que se cumplen las condiciones (todas las dadas) |
| `walk_to: {trigger \| point: [x, z], radius, walk, device, timeout}` | Camina con el dispositivo hasta las celdas con ese trigger (y el cambio de mapa que dispara) o hasta el punto; planifica con la colisión del mapa y evita los demás warps. `walk: true` anda en vez de correr |
| `menu: open \| close` | Abre o cierra el menú con el dispositivo (ratón: el botón «Menú» del HUD y clic derecho) |
| `menu_select: <id>` | Enfoca y pulsa una entrada del menú (`open-minimap`, `minimap-visible`...) |
| `expect_setting: {<nombre>: <valor>}` | Comprueba un ajuste publicado a la UI (`minimap_visible: false`) |
| `screenshot: <nombre>` | Guarda `<ruta>-<NN>-<nombre>-<ancho>x<alto>.png` en `--shots` |
| `resize: [ancho, alto]` | Cambia la ventana y espera a que el render tenga ese tamaño |
| `fullscreen: true \| false` | Pantalla completa |
| `press`, `release`, `tap: {key \| button}`, `axis: {axis, value}`, `click: [x, z]` | Controles sueltos: teclas, botones y ejes del mando virtual, clic en un punto del suelo |

Las coordenadas son del mundo del motor (x, z en el suelo; 100 unidades por celda de colisión).
