# mapper

El editor de mapas de `dwe` (`apps/mapper`, programa `dwe-mapper`). Por ahora es un **visor de solo lectura**: abre la
carpeta de contenido como el juego, lista los mapas y dibuja sobre el fondo del mapa sus colisiones, salidas,
personajes, puertas y baño. Está hecho en Rust con [egui](https://github.com/emilk/egui) (wgpu), aparte de CMake, y
no forma parte de los paquetes que se publican.

## Qué enseña

- **Lista de mapas** (izquierda): los mapas de `data/maps.yaml` que tienen datos, agrupados por el nombre de zona de
  la tabla o por el prefijo del id, con un filtro por id o nombre. El grupo del mapa abierto se despliega solo.
- **Lienzo** (centro): el fondo del mapa (día, atardecer, noche o las paletas que tenga) y las capas encima,
  proyectadas con la misma cámara que el juego ([cómo se dibuja un mapa](../../formats/mapas.md)). Arrastrar
  desplaza, la rueda acerca y aleja hacia el cursor, doble clic encaja el mapa en la ventana.
- **Capas** (barra de arriba), cada una se enciende y apaga:
  - **Paredes:** las celdas bloqueadas (bit `0x80`).
  - **Disparadores:** las celdas de disparo coloreadas por banda: paso (51–79), confirmación (80–109), salida
    (110–119) y baño (120).
  - **Salidas:** cada salida con su índice y su destino (`mapa:salida`) junto a sus celdas, y la flecha de llegada
    (dónde aparece el jugador al entrar por ella y hacia dónde mira). Un destino que no existe o que contradice el
    índice del original se marca.
  - **NPC:** la posición y la orientación de cada hueco de personaje, con su tipo y su script.
  - **Puntos de ruta:** los puntos a los que camina o corre cada personaje, en orden; la ruta vuelve al primero. Las
    esperas no se dibujan.
  - **Puertas** y **Baño:** las puertas de `data/maps.yaml` y los puntos del baño del mapa.
  - **Rejilla:** la rejilla de colisión de 100×100 celdas.
- **Detalles** (derecha): el mapa (índice, nombre, tamaño del fondo), sus salidas con «Ir», sus personajes y lo que hay
  bajo el cursor: la celda, su valor y su banda, y las coordenadas del mundo y del original.
- **Clic en una salida** abre su mapa de destino centrado en la llegada; «Atrás» vuelve al mapa anterior.
- **En vivo:** se conecta al control remoto de un `dwe-game` en marcha ([control remoto](../../guides/control-remoto.md))
  para abrir el mapa que enseña el juego y pedirle que recargue el suyo.

La interfaz está en español y todos sus textos viven en `crates/mapper-core/src/text.rs`.

## Cómo está hecho

| Crate | Carpeta | Qué hace |
|---|---|---|
| `dwe-pack` | `packages/rust/dwe-pack` | Los packs como el motor ([sistema de archivos virtual](../../architecture/vfs.md)): `pack.yaml`, la pila por capa, prioridad y dependencias, los ids de asset, las tablas compuestas (`data/maps`) y el montaje del contenido del juego con los packs del equipo y los mods |
| `dwe-map` | `packages/rust/dwe-map` | El documento de un mapa leído del YAML, la colisión con su leyenda o en hexadecimal, las bandas de disparo, las coordenadas ([espacio del mundo](../../architecture/world-space.md)) y la cámara con su proyección al fondo |
| `mapper-core` | `apps/mapper/crates/mapper-core` | El modelo del editor sin ventana: el contenido abierto, el mapa, la vista, las capas, la lista de mapas y lo que se dibuja (`MapOverlay`). Todo cambio entra por el enum `Action` |
| `mapper-live` | `apps/mapper/crates/mapper-live` | El cliente del control remoto (líneas JSON por socket Unix o TCP local) |
| `dwe-mapper` | `apps/mapper` | La ventana: paneles, lienzo y línea de órdenes |

`dwe-pack` y `dwe-map` copian las reglas del motor en C++ y no dependen de nada gráfico; las podrá usar cualquier
herramienta en Rust. Sus tests comprueban esas reglas con packs sintéticos de ids neutros (`area_a`, `pack_a`):

- **Cámara:** `dwe-map/tests/camera.rs` compara la proyección, el desplazamiento y `pick_ground` con los valores que
  da el `MapCamera` del motor sobre las mismas entradas (`tests/data/camera_vectors.txt` → `.expected`).
  `scripts/mapper.sh camera-vectors` los vuelve a generar compilando `tests/cpp/camera_vectors.cpp` con el motor.
- **Colisión:** leer y volver a escribir la sección da el mismo texto que `dwx` (leyenda y hexadecimal).
- **Contenido real** (opcional): con `DWE_MAPPER_CONTENT=<carpeta de contenido>`, `tests/real_content.rs` lee todos los
  mapas y comprueba la colisión de ida y vuelta. Con `DWE_CAMERA_VECTORS=<ruta sin extensión>` compara además otros
  vectores de cámara (`.txt` y `.expected`) generados en local.

El visor de solo lectura deja fuera lo que no dibuja (luces, objetos, imágenes de casillas): para editar hará falta un
modelo que lea y escriba el documento entero. Las bandas de disparo están fijas en `dwe_map::triggers` como en
`collision_triggers.hpp` hasta que el motor las lea de datos. Los `.pak` no se leen: solo carpetas.

## Usarlo

```sh
scripts/mapper.sh build                    # build/mapper/release/dwe-mapper
scripts/mapper.sh run --content <carpeta>  # compila y lo abre en el equipo
```

Sin `--content` se abre vacío y la carpeta se elige en «Archivo → Abrir contenido». Dentro del repositorio encuentra
solo `content/packs` (los packs del equipo). Opciones (`dwe-mapper --help`):

| Opción | Qué hace |
|---|---|
| `--content <dir>` | La carpeta de contenido: el pack base y los packs de al lado |
| `--team-packs <dir>` | Los packs del equipo, si no se encuentran solos |
| `--mods <dir>` | Una carpeta de mods (se repite) |
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
