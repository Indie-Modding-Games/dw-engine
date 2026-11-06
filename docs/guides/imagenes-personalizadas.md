# Imágenes personalizadas

El jugador puede exportar las imágenes que el juego leyó de su propio disco, mejorarlas y cargarlas en el lanzador.
El juego las dibuja en el mismo sitio y al mismo tamaño en pantalla, con más detalle. Las imágenes salen siempre del
disco del jugador: ni el lanzador ni el repositorio traen ninguna.

Hoy se pueden cambiar:

- **Los fondos de los mapas:** `map/<mapa>/background_<variante>.png`, con las variantes `day`, `dusk`, `night` y, en
  pocos mapas, `paletteN`.
- **La pantalla de título, los menús, los iconos y las cifras de la fecha:** cada imagen de `texture/`, una vez por
  paleta: `texture/.../<imagen>_palette<N>.png`. El juego dibuja muchas de esas imágenes con varias paletas de color
  y cada archivo es la imagen con una de ellas. Por ejemplo, `texture/etcna/title2_palette0.png` es la pantalla de
  título y `texture/etcdat/fi_info_palette12.png` tiene los iconos del menú del campo y las cifras del año y el día.

## En el lanzador: la sección Imágenes

La sección **Imágenes** de la barra lateral prepara las imágenes la primera vez (unos segundos, con
`dwx export-images`) y ofrece:

- **Exportar originales:** copia todas las imágenes en una carpeta nueva, `dwe-imagenes`, dentro de la que elijas, con
  `LEEME.txt` (qué es cada carpeta y cómo mejorarlas) y `mapas.tsv` (el nombre de cada mapa).
- **Cargar carpeta / Cargar .zip** (o soltarlos sobre la sección): busca los archivos con los nombres de la
  exportación y, antes de tocar nada, enseña un resumen: cuántas imágenes se pueden usar, cuántas son iguales a las
  originales (se omiten), qué nombres no son de ninguna imagen y cuáles tienen un tamaño que no sirve. **Usar N
  imágenes** copia solo las buenas. Da igual que la carpeta de la exportación vaya dentro de otra: el nombre cuenta
  desde la primera carpeta `map/` o `texture/`, sin distinguir mayúsculas.
- **La lista:** los mapas agrupados por las letras de su id (`mayo`, `twna`...), con el nombre que lleva la mayoría
  de ellos en el idioma del lanzador, y después las imágenes de título, menús e iconos por carpeta. Cada fila enseña
  la original y la tuya, si la hay, con su escala. El buscador filtra por nombre o id.
- **Asignar / Cambiar / Restablecer** en cada imagen: elige un PNG para esa imagen (con las mismas comprobaciones
  que la carga en bloque) o vuelve a la original.
- **Usar mis imágenes en el juego** las enciende y apaga sin borrarlas; **Quitar todas** las borra.

El juego no tiene zonas que el lanzador pueda leer, así que los grupos de mapas son las letras del id; cada grupo se
llama como la mayoría de sus mapas.

## Qué imagen sirve

Una imagen sirve si:

- es un PNG (de cualquier tipo: indexado, RGB o RGBA);
- tiene la misma proporción que la original y es 1, 2, 3... hasta 8 veces más grande, el mismo número de veces en
  ancho y en alto (de 768x640 a 1536x1280 sí; a 1600x1280 no);
- en un mapa, todas las variantes que cambies tienen la misma escala;
- no es idéntica a la original.

Si una imagen no sirve al jugar (por ejemplo, se cambió a mano en la carpeta), el juego dibuja la original y lo anota
en su registro.

## Cómo lo usa el juego

Todo va a un único paquete de la capa `user` en `<datos>/custom-images/pack` (`pack.yaml` con `id: custom_images`).
Mientras está encendido, el lanzador arranca el juego con `--mods <datos>/custom-images/pack`. Las originales
exportadas viven en `<datos>/custom-images/originals` y se vuelven a preparar cuando cambia el juego o el disco leído.

- **Fondos de mapa** (`packages/engine/assets`, `map_images.cpp`): el juego lee siempre los datos del mapa (índices y paletas)
  del paquete del juego más alto que no sea de la capa `user`, así que una imagen del jugador nunca rompe un mapa. La
  imagen del jugador se dibuja por el camino del fondo en alta resolución (`MapRenderer`, el mismo que usa la mejora
  de escenario HD) con su escala: cada píxel original corresponde a un cuadrado de k×k píxeles de la imagen. Qué
  partes tapan a los personajes sigue saliendo de la original. Una variante sin imagen propia usa la original
  agrandada. Las páginas de textura de los mapas (`texture8/`, `texture4/`) no se pueden cambiar todavía.
- **Título, menús e iconos** (`packages/engine/ui`, `image_reference.cpp`): cuando la interfaz pide `texture/... ?palette=N`,
  busca `<imagen>_palette<N>` (sin paleta, la 0). Le dice a la interfaz el tamaño de la original, así que las hojas de
  sprites, los rectángulos de `<img rect>` y los decoradores siguen en píxeles de la original, y la textura conserva
  toda su resolución.

## Para desarrolladores

```sh
dwx export-images <contenido> [--with <carpeta de paquetes>]... [--language es] -o <carpeta>
```

Escribe la exportación que lee el lanzador (sin `LEEME.txt`, que añade el lanzador): las imágenes de la base a su
tamaño y `maps.tsv` (`<id>\t<nombre>`). Solo sustituye una carpeta vacía o una exportación anterior.

Para probar en el juego sin lanzador: crea una carpeta con el `pack.yaml` de
`apps/launcher/src-tauri/src/custom_images/pack.yaml` y las imágenes con los nombres de arriba, y arranca el juego con
`--mods <carpeta>`. El registro dice `map <id>: drawing the player's background images, 2x, ...`.

Los tests: `packages/engine/assets/tests/image_replacement_tests.cpp` (escala, fallos y vuelta a la original),
`packages/engine/render/tests/scenery_upscale_tests.cpp` (dónde cae cada píxel), `packages/engine/ui/tests/image_replacement_tests.cpp`
(paletas y tamaño), `apps/dwx/tests/export_images_tests.cpp` y, en el lanzador, `custom_images/tests.rs` (nombres,
comprobaciones, carga en bloque y por imagen, exportación) y `launch.rs` (`--mods`).
