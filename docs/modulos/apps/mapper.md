# mapper

El editor de mapas de `dwe` (`apps/mapper`, programa `dwe-mapper`). Abre la carpeta de contenido como el juego, lista
los mapas y dibuja sobre el fondo del mapa sus colisiones, salidas, personajes, puertas y baño, con los
[parches](../../formats/mapas.md#parches) de los paquetes ya aplicados. **Edita** la colisión, las salidas y los NPC, y
**guarda** lo cambiado como un parche en un paquete de mod ([Editar](#editar), [Guardar](#guardar)). Está hecho en Rust
con [egui](https://github.com/emilk/egui) (wgpu), aparte de CMake, y no forma parte de los paquetes que se publican.

## Qué enseña

- **Lista de mapas** (izquierda): los mapas de `data/maps.yaml` que tienen datos, agrupados por el nombre de zona de
  la tabla o por el prefijo del id, con un filtro por id o nombre. El grupo del mapa abierto se despliega solo.
- **Lienzo** (centro): el fondo del mapa (día, atardecer, noche o las paletas que tenga) y las capas encima,
  proyectadas con la misma cámara que el juego ([cómo se dibuja un mapa](../../formats/mapas.md)). Arrastrar
  desplaza (botón derecho o central), la rueda acerca y aleja hacia el cursor y doble clic con «Seleccionar» encaja el
  mapa en la ventana. El botón izquierdo hace lo de la herramienta elegida ([Editar](#editar)).
- **Capas** (barra de arriba), cada una se enciende y apaga:
  - **Paredes:** las celdas bloqueadas (bit `0x80`).
  - **Disparadores:** las celdas de disparo coloreadas por banda: paso, confirmación, salida y baño. Las bandas son
    las de `field/triggers` del contenido (el fichero que gana en la pila, como en el juego); sin él, las del
    original (51–79, 80–109, 110–119 y 120). Un `field/triggers` que el juego rechazaría (bandas que se solapan, fuera
    de 1–127, más de diez salidas, claves de más o de menos) sale como error en la barra de estado y se usan las del
    original.
  - **Salidas:** cada salida con su índice y su destino (`mapa:salida`) junto a sus celdas, y la flecha de llegada
    (dónde aparece el jugador al entrar por ella y hacia dónde mira). Un destino que no existe o que contradice el
    índice del original se marca.
  - **NPC:** la posición y la orientación de cada hueco de personaje, con su tipo y su script.
  - **Puntos de ruta:** los puntos a los que camina o corre cada personaje, en orden; la ruta vuelve al primero. Las
    esperas no se dibujan.
  - **Puertas** y **Baño:** las puertas de `data/maps.yaml` y los puntos del baño del mapa.
  - **Rejilla:** la rejilla de colisión de 100×100 celdas.
- **Detalles** (derecha): el mapa (índice, nombre, tamaño del fondo y «parcheado por: …» con los paquetes cuyos parches
  se aplicaron, de abajo arriba), sus salidas con «Ir» (al pasar el ratón por una salida parcheada se ve de qué
  paquete es el parche), sus personajes y lo que hay bajo el cursor: la celda, su valor y su banda, y las coordenadas
  del mundo y del original.
- **Ctrl+clic en una salida** abre su mapa de destino centrado en la llegada; «Atrás» vuelve al mapa anterior.
- **Parches:** el mapa se lee como `loadMapDocument`: se aplican los `map/<id>/patch` del paquete cuyo `data` gana y de
  todos los de encima, de abajo arriba, con las mismas reglas y los mismos errores que el motor (`dwe_map::patch`
  copia `map_patch.cpp`). Un parche que no se puede aplicar deja el mapa sin abrir y el error nombra el fichero.
- **En vivo:** se conecta al control remoto de un `dwe-game` en marcha ([control remoto](../../guides/control-remoto.md))
  para abrir el mapa que juega el juego (`status`), pedirle que relea los packs y recargue su mapa (`reload_map`, que
  vuelve a listar los ficheros antes), encender o apagar su
  [capa de depuración del mapa](../../guides/control-remoto.md#capa-de-depuración-del-mapa) (`overlay`; al conectar,
  la casilla toma el estado que da `status`) y leer el estado de la historia (`story_state`): los disparadores activos
  y las variables distintas de 0, con las etiquetas de `story/names` cuando las tienen (`12 door_open`). Con
  «Recargar al guardar» (encendido por defecto) cada guardado pide `reload_map` al juego conectado.

## Editar

Cada cambio es una acción que se puede deshacer (Ctrl+Z) y rehacer (Ctrl+Y o Ctrl+Mayús+Z); un trazo de pincel, un
arrastre o un campo arrastrado en el inspector es **un solo paso**. La barra de estado marca el mapa con cambios sin
guardar (`* twna01 (sin guardar)`) y dice a qué paquete se guarda.

**Herramientas** (barra bajo el menú):

| Herramienta | Tecla | Qué hace |
|---|---|---|
| Seleccionar | V | Elige y arrastra la llegada de una salida, un NPC o un punto de su ruta; con Mayús, arrastrar los orienta hacia el cursor; un clic en las celdas de una salida la elige. Si no hay nada debajo, arrastrar desplaza la vista |
| Pincel | B | Pinta el valor elegido en un cuadrado de 1, 3 o 5 celdas (`[` y `]`); el trazo une las celdas con líneas |
| Rectángulo | R | Pinta el rectángulo arrastrado (se ve antes de soltar) |
| Relleno | F | Pinta las celdas unidas a la del clic que tienen su mismo valor |
| Cuentagotas | I | Toma el valor de la celda (o Alt+clic con cualquier herramienta de pintar) |

El **valor** que se pinta se elige por grupos de las bandas de `field/triggers` (pared, libre, al pisar, al
confirmar, cada salida y baño) o como número crudo; X alterna pared y libre.

**Inspector** (derecha, arriba), según lo elegido:

- **Salida:** el mapa de destino (lista de ids con filtro; elegir uno pone también el índice del original si el mapa lo
  tiene, o -1), la salida de destino (-1 a 9), la llegada (x, y, z) y la orientación (0–4095). «Pintar sus celdas»
  pone el pincel con el disparador de la salida y «Borrar celdas» con el valor libre; «Abrir el destino» va al mapa.
- **NPC:** `type_id`, `follow_mode`, `tracking_range`, `script_id`, la posición y la orientación, y en «Campos» los
  demás campos numéricos del registro. Los de texto no se enseñan y se guardan como estaban.
- **Ruta:** cada parte con su tipo (esperar, andar, correr), su punto (o, en una espera, sus fotogramas) y «Subir»,
  «Bajar» y «Quitar»; «Añadir punto» (o W bajo el cursor) añade un tramo andando. `ai_sections` y `waypoints` se
  mantienen juntos: 8 partes como mucho, el resto con -1.
- **Cámara, luces y objetos:** se enseñan, de solo lectura.

Las puertas y el baño no se editan: viven en `data/maps` (los conjuntos `doors` y `toilets` que cada mapa elige por
índice), que es una tabla compuesta y no un parche de mapa.

### Atajos

| Teclas | Qué hace |
|---|---|
| Ctrl+S | Guardar (parche) |
| Ctrl+Z / Ctrl+Y / Ctrl+Mayús+Z | Deshacer / rehacer |
| V, B, R, F, I | Seleccionar, Pincel, Rectángulo, Relleno, Cuentagotas |
| `[` / `]` | Pincel más pequeño / más grande |
| X | Valor: pared / libre |
| Q / E | Girar la selección 45° a un lado o al otro |
| Supr | Quitar el punto de ruta elegido |
| W | Añadir un punto de ruta al NPC elegido bajo el cursor |
| Esc | Deseleccionar |
| Ctrl+clic | Ir adonde lleva la salida |
| Mayús+arrastrar | Orientar la selección hacia el cursor |

«Editar → Atajos de teclado» enseña la misma lista (`SHORTCUT_LIST` en `text.rs`).

## Guardar

**Destino** («Archivo → Destino»): el paquete donde se escribe. Puede ser un paquete de mod montado (las carpetas de
`--mods`), uno nuevo («Nuevo paquete de mod…»: una carpeta con su `pack.yaml` en la capa `mods`, que se monta al
crearlo) o, solo en un checkout del repositorio, el paquete `game` del equipo (`content/packs/game`). Nunca el paquete
base ni uno extraído o generado. El editor recuerda el último destino en `dwe-mapper/settings.yaml` de la carpeta de
configuración del usuario (`$XDG_CONFIG_HOME`, `%APPDATA%` o `~/.config`).

**Guardar (parche)**, Ctrl+S, escribe `map/<id>/patch.yaml` en el destino con **solo las diferencias** respecto al
suelo que tiene debajo (el `data` que gana más los parches de los paquetes de debajo del destino):

- las salidas por su `exit` y los NPC por su `index`, con solo los campos cambiados (cada campo se reemplaza entero:
  una ruta cambiada lleva `ai_sections` y `waypoints` enteros);
- la cámara, las luces (por `index`) y los objetos si cambiaron;
- la colisión como rectángulos de celdas (`cells`) cuando caben en **64 rectángulos** o menos
  (`dwe_map::diff::MOST_RECTANGLES`), y si no la rejilla entera (`grid`). Los rectángulos se buscan a lo ancho y luego
  a lo alto por filas enteras; 64 rectángulos son unas 380 líneas, más o menos lo que ocupa la rejilla entera.

Si el destino ya tenía un parche, el nuevo se calcula otra vez contra el suelo con lo de antes y lo nuevo juntos, así
que el fichero sigue siendo el mínimo. Un parche que queda vacío se borra. **Guardar mapa completo** escribe en cambio
`map/<id>/data.yaml` entero en el destino (y quita su parche), con el formato de `dwx`.

Antes de escribir se carga el resultado con toda la pila y se comprueba; lo que falla sale en el panel «Problemas al
guardar» y no se escribe nada:

- el destino está debajo del paquete cuyo `data` gana (su parche no se aplicaría);
- una salida tocada lleva a un mapa que no existe, a una salida fuera de 0–9, a nada teniendo celdas (warp muerto) o
  con un id y un índice que nombran mapas distintos;
- la ruta de un NPC tocado no cabe (8 partes);
- lo escrito no carga.

Un parche de un paquete de **encima** del destino que cambia lo mismo es solo un aviso: el juego verá el suyo.

Los ficheros se escriben de forma atómica (un temporal y un cambio de nombre). Después el contenido se vuelve a montar
(lo mismo que «Archivo → Recargar contenido») y el mapa queda limpio. Cambiar de mapa, volver a abrir el contenido o
salir con cambios sin guardar pregunta antes: Guardar, Descartar o Cancelar.

Cómo se guarda un mapa paso a paso: [Editar un mapa con el mapper](../../modding/mapas.md).

La interfaz está en español y todos sus textos viven en `crates/mapper-core/src/text.rs`.

## Cómo está hecho

| Crate | Carpeta | Qué hace |
|---|---|---|
| `dwe-pack` | `packages/rust/dwe-pack` | Los packs como el motor ([sistema de archivos virtual](../../architecture/vfs.md)): `pack.yaml`, la pila por capa, prioridad y dependencias, los ids de asset, las tablas compuestas (`data/maps`) y el montaje del contenido del juego con los packs del equipo y los mods |
| `dwe-map` | `packages/rust/dwe-map` | El documento de un mapa leído del YAML con sus parches y escrito de vuelta (`emit`, `diff`, `save`), la colisión con su leyenda o en hexadecimal, las bandas de disparo de `field/triggers`, los nombres de `story/names`, las coordenadas ([espacio del mundo](../../architecture/world-space.md)) y la cámara con su proyección al fondo |
| `mapper-core` | `apps/mapper/crates/mapper-core` | El modelo del editor sin ventana: el contenido abierto, el mapa, la vista, las capas, la lista de mapas, lo que se dibuja (`MapOverlay`), las herramientas (`tools`), el historial de deshacer (`edit`) y los destinos de guardado (`targets`). Todo cambio entra por el enum `Action` |
| `mapper-live` | `apps/mapper/crates/mapper-live` | El cliente del control remoto (líneas JSON por socket Unix o TCP local) |
| `dwe-mapper` | `apps/mapper` | La ventana: paneles, lienzo y línea de órdenes |

`dwe-pack` y `dwe-map` copian las reglas del motor en C++ y no dependen de nada gráfico; las podrá usar cualquier
herramienta en Rust. Sus tests comprueban esas reglas con packs sintéticos de ids neutros (`area_a`, `pack_a`):

- **Cámara:** `dwe-map/tests/camera.rs` compara la proyección, el desplazamiento y `pick_ground` con los valores que
  da el `MapCamera` del motor sobre las mismas entradas (`tests/data/camera_vectors.txt` → `.expected`).
  `scripts/mapper.sh camera-vectors` los vuelve a generar compilando `tests/cpp/camera_vectors.cpp` con el motor.
- **Colisión:** leer y volver a escribir la sección da el mismo texto que `dwx` (leyenda y hexadecimal).
- **Parches:** `dwe-map/src/patch.rs` repite los casos de `packages/engine/assets/tests/map_patch_tests.cpp` sobre el
  mismo mapa de prueba (una salida campo a campo, el orden de la pila, rectángulos tras una rejilla entera, objetos,
  los errores que nombran el fichero, el suelo de la pila que deja fuera los parches de debajo del `data`).
- **Bandas y nombres:** los mismos casos que `trigger_bands_tests.cpp` y `story_names_tests.cpp` (lo que se rechaza y
  con qué mensaje).
- **Escritura:** un documento sin tocar se escribe igual que lo escribió `dwx` (el emisor imita a libyaml: bloques con
  sangría 2, las listas de una clave sin sangrar, los textos entre comillas dobles); las claves que el modelo no
  conoce se conservan en su sitio. Diferenciar y luego aplicar el parche da el mapa editado, y sin cambios no hay
  parche.
- **Contra el motor:** `dwe-map/tests/written.rs` edita `base.yaml` de `packages/engine/assets/tests/data/written_maps` y guarda para cada caso el
  parche que escribe el editor y el mapa entero que espera (`cells`: rectángulos, salidas y campos de NPC; `grid`: la
  rejilla entera). `packages/engine/assets/tests/map_written_tests.cpp` aplica ese parche a la misma base con
  `patchMapDocument` y compara campo a campo con `expected.yaml` leído por el motor: los dos lados leen, parchean y
  escriben igual. `DWE_MAPPER_BLESS=1 cargo test --test written` los vuelve a generar tras un cambio a propósito.
- **Editor sin ventana:** `mapper-core/tests/session.rs` pinta, rellena, arrastra y gira NPC y salidas, edita rutas,
  deshace y rehace, pregunta por los cambios sin guardar y guarda en un paquete de mod nuevo.
- **Contenido real** (opcional): con `DWE_MAPPER_CONTENT=<carpeta de contenido>`, `tests/real_content.rs` lee todos los
  mapas, comprueba la colisión de ida y vuelta y que cada mapa sin parches se escribe igual byte a byte. Con `DWE_CAMERA_VECTORS=<ruta sin extensión>` compara además otros
  vectores de cámara (`.txt` y `.expected`) generados en local.

El documento guarda entero lo que lee (`MapDocument::source`), también las regiones, las imágenes, el color de ambiente
y los campos de los NPC que no tienen tipo en Rust, y los escribe de vuelta en su orden. Las luces y los objetos se leen con las claves del motor
para que un parche que los rompe se rechace como en el juego. Dos diferencias con el lector del motor, que es más
estricto: los errores de un valor no dicen la línea del parche (el árbol YAML de Rust no la guarda) y en los
personajes solo se comprueban los campos que el editor usa. Los `.pak` no se leen: solo carpetas. El editor lista los
ficheros al abrir el contenido y otra vez tras cada guardado o con «Archivo → Recargar contenido».

## Usarlo

```sh
scripts/mapper.sh build                    # build/mapper/release/dwe-mapper
scripts/mapper.sh run --content <carpeta>  # compila y lo abre en el equipo
```

Sin `--content` se abre vacío y la carpeta se elige en «Archivo → Abrir carpeta de contenido». Dentro del repositorio encuentra
solo `content/packs` (los packs del equipo). Opciones (`dwe-mapper --help`):

| Opción | Qué hace |
|---|---|
| `--content <dir>` | La carpeta de contenido: el pack base y los packs de al lado |
| `--team-packs <dir>` | Los packs del equipo, si no se encuentran solos |
| `--mods <dir>` | Una carpeta de mods o un pack suelto (se repite); sus parches de mapa se aplican |
| `--map <id>` y `--exit <n>` | El mapa que se abre primero, centrado en la llegada por esa salida |
| `--variant <nombre>` | El fondo: `day`, `dusk`, `night`… |
| `--layers <lista>` | Las capas encendidas: `walls,triggers,exits,npcs,waypoints,doors,toilet,grid` |
| `--group-by name\|prefix` y `--filter <texto>` | La lista de mapas |
| `--live unix:<socket>\|tcp:<puerto>` | Conecta con un `dwe-game` en marcha |
| `--size <ancho>x<alto>` | El tamaño de la ventana |
| `--shot <archivo.png>` | Guarda una imagen de la ventana y sale (para la documentación o un X virtual) |

## Compilar y probar

Todo en Docker con la imagen del lanzador (`dwe-launcher-dev`, que ya trae Rust, clippy y rustfmt); no hace falta GPU.

```sh
scripts/dev.sh test --filter mapper   # o scripts/mapper.sh test
```

`scripts/mapper.sh test` pasa `cargo fmt --check`, `cargo test --locked` y `cargo clippy -D warnings` en todo el
workspace (`apps/mapper/Cargo.toml` es su raíz e incluye los crates de `packages/rust`). En CI corre el trabajo
`mapper` cuando cambia `apps/mapper/`, `packages/rust/` o `scripts/mapper.sh`; no es obligatorio y un cambio solo ahí
no compila el C++.
