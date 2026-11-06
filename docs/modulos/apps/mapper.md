# mapper

El editor de mapas de `dwe` (`apps/mapper`, programa `dwe-mapper`). Abre la carpeta de contenido como el juego, lista
los mapas y dibuja sobre el fondo del mapa sus colisiones, salidas, personajes, puertas y baño, con los
[parches](../../formats/mapas.md#parches) de los paquetes ya aplicados. **Edita** la colisión, las salidas, los NPC, las puertas, el baño y los
eventos de la historia del mapa, y **guarda** lo cambiado como parches en un paquete de mod ([Editar](#editar),
[Eventos](#eventos), [Guardar](#guardar)). Está hecho en Rust
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
  - **Eventos:** dónde empieza cada evento del mapa: cada celda de disparo con su sección (paso o botón), los NPC
    con la sección que corre al hablarles y, en una esquina, los eventos al entrar y al cargar el mapa. Las etiquetas
    que se tapan se apilan ([Eventos](#eventos)).
- **Detalles** (derecha): el mapa (índice, nombre, tamaño del fondo y «parcheado por: …» con los paquetes cuyos parches
  se aplicaron, de abajo arriba), sus salidas con «Ir» (al pasar el ratón por una salida parcheada se ve de qué
  paquete es el parche), sus personajes y lo que hay bajo el cursor: la celda, su valor y su banda, y las coordenadas
  del mundo y del original.
- **Ctrl+clic en una salida** abre su mapa de destino centrado en la llegada; «Atrás» vuelve al mapa anterior.
- **Parches:** el mapa se lee como `loadMapDocument`: se aplican los `map/<id>/patch` del paquete cuyo `data` gana y de
  todos los de encima, de abajo arriba, con las mismas reglas y los mismos errores que el motor (`dwe_map::patch`
  copia `map_patch.cpp`). Un parche que no se puede aplicar deja el mapa sin abrir y el error nombra el fichero.
- **En vivo:** se conecta al control remoto de un `dwe-game` en marcha ([control remoto](../../guides/control-remoto.md))
  y lo sigue mientras se edita ([En vivo](#en-vivo)).

## En vivo

El panel «En vivo» (derecha) se conecta con `--live` o escribiendo `tcp:<puerto>` o `unix:<socket>` y pulsando
«Conectar». La conexión vive en su propio hilo (`mapper_live::LiveLink`): si el juego se cierra o se reinicia, el
editor lo dice en la barra de estado y vuelve a conectar solo cada segundo. El juego admite varios clientes a la vez
([varios clientes](../../guides/control-remoto.md#varios-clientes)), así que el editor puede convivir con el servidor
MCP u otra herramienta.

- **Estado:** la barra de estado dice el modo del juego (título, campo, batalla…), su mapa, el tick, si está en pausa
  o ocupado con un script de historia, si enseña una vista previa y el tiempo de ida y vuelta de la última pregunta.
  En amarillo mientras no hay conexión.
- **Seguir al juego** (encendido por defecto): cuando el juego entra en otro mapa, el editor lo abre. Si el mapa
  abierto tiene cambios sin guardar, pregunta antes como al abrir un mapa a mano; si se elige quedarse, no vuelve a
  preguntar hasta que el juego cambie otra vez de mapa. «Ir al mapa del juego» lo abre a mano.
- **Capa «Juego»:** sobre el mapa que juega el juego, el jugador, su compañero y los NPC con una flecha hacia donde
  miran, y el rectángulo del fondo que enseña la ventana del juego. El editor pregunta `live` cada 150 ms; el registro
  del juego (`log_tail`) solo se pide cuando `live` dice que tiene algo nuevo.
- **Vista previa en vivo** (apagada por defecto): el juego enseña el cambio sin guardar del mapa abierto. El editor
  manda la diferencia entre el mapa que dan los paquetes y el editado (el mismo parche que guardaría) con
  `preview_map`, como mucho cada 300 ms y nunca a mitad de un trazo. Si solo cambia la colisión, el juego la cambia
  en el sitio; si no, vuelve a entrar en el mapa con el jugador donde estaba. La vista previa vive solo en la memoria
  del juego: no se escribe en disco ni en las partidas. Se quita al apagar la casilla, al deshacer todos los cambios,
  al guardar (entonces «Recargar al guardar» trae lo guardado), al desconectar y al cerrar el editor. El juego la
  rechaza en batalla, con un cambio de mapa pendiente o mientras corre un script de historia; el editor espera a que
  el juego vuelva al campo libre.
- **Llevar al jugador:** con el botón derecho sobre el lienzo, «Llevar al jugador aquí» lo pone en ese punto (`place`).
  Sobre una salida, el mismo menú y el inspector de la salida ofrecen «Probar esta salida» (pone al jugador en una
  celda de la salida, la pulsada o la primera, y deja correr el juego, que la toma como al pisarla) y «Llevar al
  jugador a la llegada» (lo pone donde deja la salida, mirando hacia donde mira).
- **Pausa, Paso y +1 s:** paran el juego, avanzan un tick o un segundo.
- **Recargar al guardar** (encendido por defecto): cada guardado pide `reload_map`. «Recargar el mapa en el juego» lo
  pide a mano y «Capa de depuración del juego» enciende su
  [capa de depuración del mapa](../../guides/control-remoto.md#capa-de-depuración-del-mapa).
- **Historia:** el estado de la historia del juego (`story_state`): la etapa, el script del mapa, las secciones en
  curso, los disparadores activos y las variables distintas de 0, con las etiquetas de `story/names` cuando las tienen
  (`12 door_open`). «Actualizar sola» la vuelve a leer cada segundo y el filtro busca por número o nombre. Para probar
  eventos se puede activar o quitar un disparador y poner una variable (`story_set`); el juego lo rechaza mientras
  corre un script.
- **Registro del juego:** los avisos y errores del juego (los últimos 300). Lo que el juego rechaza (un parche que no
  se puede aplicar, con el fichero y la línea que da el cargador) sale además en «Problemas».

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
- **Puerta:** el modelo (o «sin modelo»), la posición y el giro; también se arrastra y se gira en el lienzo.
  «Añadir puerta» da modelo al primer hueco libre de los seis del conjunto.
- **Baño:** sus dos puntos (el compañero corre al primero y después al segundo); cada punto se arrastra en el lienzo.
  «Añadir baño» y «Quitar baño».
- **Cámara, luces y objetos:** se enseñan, de solo lectura.

**Añadir y quitar NPC.** «Añadir NPC» (o «Añadir un NPC aquí» con el botón derecho en el lienzo) copia el NPC elegido,
o el último del mapa, o el primero que haya en el contenido, en un hueco nuevo al final, y lo hace aparecer: añade
`story.load_species_model(t)` y `story.spawn_npc(t, hueco, 1)` antes del último `return` de la sección de entrada del
mapa (script 0, sección = índice del mapa) en el borrador de la historia ([Eventos](#eventos)). Todo es un solo paso
de deshacer. Un mapa tiene como mucho 8 NPC. «Quitar el último NPC» quita solo el último hueco y sus `spawn_npc`: la
historia nombra los NPC por su número, así que quitar uno de en medio cambiaría a quién habla cada sección. Un parche
de mapa nombra los NPC por posición y no puede añadirlos ni quitarlos, así que con NPC añadidos o quitados el mapa se
guarda entero y el editor lo dice. Las salidas no se añaden ni se quitan: el mapa tiene siempre sus 10 registros y
una salida nueva es uno sin celdas que se edita y se pinta.

Las puertas y el baño no están en el mapa sino en `data/maps` ([puertas y baño](../../formats/mapas.md#parches)); al
guardar se escribe el `data/maps` del destino ([Guardar](#guardar)).

### Atajos

| Teclas | Qué hace |
|---|---|
| Ctrl+S | Guardar (parche) |
| Ctrl+Z / Ctrl+Y / Ctrl+Mayús+Z | Deshacer / rehacer |
| Ctrl+F | Buscar un mapa en la lista |
| RePág / AvPág | Mapa anterior / siguiente de la lista |
| 1 … 9, 0 | Mostrar u ocultar una capa: paredes, disparadores, salidas, NPC, puntos de ruta, puertas, baño, rejilla, juego y eventos |
| Alt+0 … Alt+9 | Elegir la salida de ese número y centrarla |
| C | Centrar la vista en la selección |
| V, B, R, F, I | Seleccionar, Pincel, Rectángulo, Relleno, Cuentagotas |
| `[` / `]` | Pincel más pequeño / más grande |
| X | Valor: pared / libre |
| Q / E | Girar la selección 45° a un lado o al otro |
| Supr | Quitar el punto de ruta elegido |
| W | Añadir un punto de ruta al NPC elegido bajo el cursor |
| Esc | Deseleccionar |
| Ctrl+clic | Ir adonde lleva la salida |
| Mayús+arrastrar | Orientar la selección hacia el cursor |
| Clic en un evento | Con la capa Eventos y Seleccionar: abre su sección |

«Editar → Atajos de teclado» enseña la misma lista (`SHORTCUT_LIST` en `text.rs`). Mientras un campo de texto tiene
el foco, las teclas son del campo: Ctrl+Z deshace lo escrito en él y no en el mapa.

## Eventos

Un evento es una sección de un script de historia que el mapa pone en marcha: al entrar (la sección de entrada del
script 0), al cargar, al pisar o pulsar el botón en una celda de disparo y al hablar con un NPC. El editor lee los
`story/<n>.lua` y les aplica los [parches por sección](../../modding/historia.md#parches-por-sección) como el juego,
así que enseña la sección que de verdad corre y quién la da.

- **Panel «Eventos»** (izquierda, bajo los mapas): cada evento del mapa con su tipo, su sección, «parcheado por …»
  cuando la da un parche y los saltos de mapa que hace (`→ mapa salida`, con la condición si la tiene; un clic abre
  el destino). Se filtra por tipo o por número. Un clic abre la sección y centra la vista en el evento. Una celda que
  apunta a una sección que el script no tiene se marca: no pasa nada al pisarla, y editarla la crea.
- **Del lienzo a la sección:** con la capa Eventos, un clic en una celda de disparo o en un NPC abre su sección. En
  el inspector, la celda bajo el cursor dice qué sección corre (`Pisar → sección 0x51 (script 12)`) con «Abrir
  sección»; al pintar un disparador, «Sección…» dice qué secciones existen para ese valor.
- **Editor de sección** (ventana «Sección»): arriba, el script, la sección y su origen (módulo o parche).
  - **Estructura:** las líneas de la sección como frases en español (diálogos con su texto, cambios de disparadores
    y variables con sus nombres de `story/names`, movimientos, saltos de mapa…), con los `if` y sus ramas. Al elegir
    una línea se editan sus campos (operandos); en un diálogo, la línea de texto: cambiarla cambia la clave en todos
    los sitios que la usan, y «Línea nueva» da a ese comando una clave propia con el texto de ahora. Una condición
    simple (disparador encendido o apagado, variable igual a un valor) se cambia con campos; otra, como texto Lua.
    «Borrar», «Subir» y «Bajar» mueven las líneas y «Abrir rutina» entra en una rutina del script (con «Volver a la
    sección»).
  - **Paleta:** los comandos de la API `story` por grupos, con filtro; uno nuevo va después de la línea elegida (o
    dentro del bloque elegido). «Si…» añade un bloque y los que cambian de mapa o de script acaban la sección.
  - **Lua:** el código de la sección (o de la rutina) como texto; «Aplicar» lo cambia si compila y dice la línea del
    error si no.
  - «Revertir sección» quita la sección del borrador: vuelve a correr como la dan los paquetes.

  Cada cambio es un paso de deshacer, junto con los del mapa. Lo editado vive en un **borrador** por script del
  paquete destino y se ve enseguida en el panel y en el lienzo; sin destino elegido se ve igual, pero para guardarlo
  hay que elegir uno.
- **Probar evento** («Probar» en el panel o en el editor, con un juego conectado en [En vivo](#en-vivo)): el editor
  lee las condiciones que la sección comprueba al empezar (los `if` que solo acaban, siguiendo `return
  routines.x()`) y enseña qué escribirá en la historia del juego para que pase (disparadores y variables) y las que
  no sabe poner (hay que cumplirlas a mano). Al probar manda `story_set` con esos valores, `reload_map` (que también
  vuelve a leer los parches guardados), pone al jugador en la celda del disparador o delante del NPC, y en un evento
  de botón o de hablar pulsa confirmar. Al acabar compara el estado de la historia de antes y de después y lo enseña
  («Lo que cambió la prueba»). Lo que corre es lo **guardado**: el borrador se guarda antes.

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

Los **eventos editados** se guardan en el mismo Ctrl+S como un parche de historia del destino,
`story/patches/<paquete>_<script>.lua` (con `<script>` en tres cifras), con solo las secciones cambiadas y las rutinas
que necesitan copiadas como rutinas privadas ([parches por sección](../../modding/historia.md#parches-por-sección)).
Si el paquete ya tenía un parche de ese script, se edita ese (y se mezcla con él si cambió en disco desde que se
abrió el borrador); un parche que se queda sin secciones se borra. Las líneas de texto nuevas o cambiadas van al
`text/<idioma>` del destino. Un parche de historia de un paquete de encima que da la misma sección es un aviso: el
juego verá el suyo. Si lo guardado cambia saltos de mapa (`story.warp_to`), hay que volver a sacar
`field/script_warps` ([warps de script](../../architecture/warps-de-script.md)): con el programa `dwx` puesto en
«Archivo → Ajustes…», el editor ejecuta `dwx script-warps <contenido> --with <mods> -o <destino>/field/script_warps.yaml`
él mismo; sin él, dice el comando en la barra de estado.

Las **puertas y el baño** cambiados se guardan en el `data/maps.yaml` del destino: las secciones `doors` o `toilets`
enteras (como se componen) y, si el conjunto del mapa lo comparten otros mapas o no tenía, uno nuevo al final con la
entrada del mapa apuntando a él. Los demás mapas no cambian.

Si el destino ya tenía un parche de mapa, el nuevo se calcula otra vez contra el suelo con lo de antes y lo nuevo juntos, así
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
(lo mismo que «Archivo → Recargar contenido»), el mapa queda limpio y el historial de deshacer empieza de nuevo.
«Archivo → Revertir» descarta los cambios sin guardar del mapa y de sus eventos. Cambiar de mapa, volver a abrir el contenido o
salir con cambios sin guardar pregunta antes: Guardar, Descartar o Cancelar.

Cómo se guarda un mapa paso a paso: [Editar un mapa con el mapper](../../modding/mapas.md).

La interfaz está en español y todos sus textos viven en `crates/mapper-core/src/text.rs`.

## Cómo está hecho

| Crate | Carpeta | Qué hace |
|---|---|---|
| `dwe-pack` | `packages/rust/dwe-pack` | Los packs como el motor ([sistema de archivos virtual](../../architecture/vfs.md)): `pack.yaml`, la pila por capa, prioridad y dependencias, los ids de asset, las tablas compuestas (`data/maps`) y el montaje del contenido del juego con los packs del equipo y los mods |
| `dwe-map` | `packages/rust/dwe-map` | El documento de un mapa leído del YAML con sus parches y escrito de vuelta (`emit`, `diff`, `save`), la colisión con su leyenda o en hexadecimal, las bandas de disparo de `field/triggers`, los nombres de `story/names`, las coordenadas ([espacio del mundo](../../architecture/world-space.md)), la cámara con su proyección al fondo y las puertas y el baño de `data/maps` (`places`) |
| `dwe-story` | `packages/rust/dwe-story` | Los scripts de historia en Lua: un lector y escritor del subconjunto que usan los módulos y los parches (`parse`, `write`), los scripts con sus parches aplicados como el juego (`script`), los eventos de un mapa (`events`), las frases en español (`describe`), la edición por líneas y condiciones (`edit`), las condiciones de una sección para probarla (`needs`) y el guardado como parche (`save`) |
| `mapper-core` | `apps/mapper/crates/mapper-core` | El modelo del editor sin ventana: el contenido abierto, el mapa, la vista, las capas, la lista de mapas, lo que se dibuja (`MapOverlay`), las herramientas (`tools`), el historial de deshacer (`edit`) y los destinos de guardado (`targets`). Todo cambio entra por el enum `Action` |
| `mapper-live` | `apps/mapper/crates/mapper-live` | El cliente del control remoto (líneas JSON por socket Unix o TCP local) y `LiveLink`, el hilo que pregunta al juego, vuelve a conectar y le manda las órdenes del editor sin parar la ventana |
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
  deshace y rehace, pregunta por los cambios sin guardar y guarda en un paquete de mod nuevo; también el parche de la
  vista previa y dónde ponen al jugador las pruebas de salida. `mapper-core/src/live.rs` prueba el seguimiento del
  mapa y cuándo se manda o se quita la vista previa.
- **Historia:** `dwe-story` lee y vuelve a escribir cada forma del Lua generado, aplica parches como `StoryLuaHost`,
  y guarda: un parche nuevo, uno que cambió en disco, uno de encima que gana, uno que se queda vacío. El parche que
  escribe se comprueba contra el motor: `packages/session/tests/data/written_story` lleva un módulo y el parche que
  saca el editor (con una rutina privada) y `packages/session/tests/story_patch_tests.cpp` lo carga con el lector del
  juego y corre la sección. `mapper-core/tests/story.rs` edita una línea y una condición, deshace, guarda el parche y
  el texto, añade y quita NPC, guarda puertas y baño y prepara una prueba de evento. Con `DWE_MAPPER_CONTENT`,
  `dwe-story/tests/real_content.rs` lee y vuelve a escribir todos los módulos, saca cada sección a un parche y lista
  los eventos de todos los mapas.
- **En vivo:** `mapper-live` habla con un juego falso (un servidor en un hilo que responde como
  `remote_control.cpp`): las respuestas, los rechazos, el quinto cliente rechazado, que el enlace vuelve a conectar
  cuando el juego vuelve, que solo pide el registro cuando cambia y que al cerrarse quita la vista previa.
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
| `--section <script>:<sección>` | Con `--map`: abre esa sección en el editor (decimal o `0x…`) |
| `--variant <nombre>` | El fondo: `day`, `dusk`, `night`… |
| `--layers <lista>` | Las capas encendidas: `walls,triggers,exits,npcs,waypoints,doors,toilet,grid,live,events` |
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
