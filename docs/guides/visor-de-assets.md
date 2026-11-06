# Visor de assets

El overlay de depuración tiene un visor para ver lo que hay en los paquetes sin escribir código: qué IDs existen, qué
paquete da cada uno, qué sobrescribe y, si es una textura PNG o KTX2, cómo se ve. El código está en
`engine/debug/src/asset_viewer.cpp`.

## Abrirlo

1. Crea el paquete base desde tu disco: `dwx extract <imagen>` escribe `content/base/` (ver
   [formatos y `dwx`](../architecture/formats.md)). También vale cualquier carpeta con paquetes propios.
2. Arranca el juego. Por defecto el visor monta `content/base` respecto a la carpeta desde la que lanzas el juego;
   para otra carpeta:

   ```bash
   scripts/dev.sh run --content content          # todos los paquetes de content/
   scripts/dev.sh run --content ~/mis-paquetes/hd # un paquete suelto
   ```

3. Abre el overlay (`º`, `` ` `` o `F1`) y marca **asset viewer** en la ventana **Frame**. Sale la ventana **Assets**.

La carpeta puede ser un paquete (tiene `pack.yaml` en la raíz) o una carpeta con paquetes: subcarpetas con
`pack.yaml` y archivos `.pak` (los que escribe `dwx pack`), mezclados. Si hay una carpeta y un `.pak` con el mismo
nombre (`base/` y `base.pak`), se monta la carpeta y la ventana dice que el archivo quedó fuera. Se monta con el
sistema de ficheros virtual, con sus capas, prioridades y dependencias ([paquetes](../architecture/vfs.md), que
explica estas reglas). Al arrancar, el log dice qué se ha montado: una línea
`asset viewer: mounted '<carpeta>': <n> assets; packs ...` con cada paquete, su versión y su capa.

En la propia ventana puedes escribir otra carpeta y pulsar **Mount** (o Intro) para montarla sin reiniciar; sirve
también para recargar después de volver a extraer.

## Qué enseña

- Arriba, la carpeta, el número de assets y la pila de paquetes de abajo arriba.
- A la izquierda, los IDs agrupados por su primer segmento (`map`, `ui`...), con un filtro de texto. Los IDs sin
  carpeta van en el grupo `(top level)`.
- A la derecha, el ID elegido, el paquete y el fichero que lo dan y, si lo tienen otros paquetes más abajo en la pila,
  cuáles sobrescribe.
- Si el fichero que gana es un PNG o un KTX2 cocinado, la textura con su tamaño en píxeles y en bytes, cómo la guarda
  el fichero y un zoom de 1x a 16x con filtrado *nearest*, para ver cada píxel tal cual. Un KTX2 se transcodifica a
  BC7 si la GPU lo muestrea, y si no a RGBA8 (`assets::decodeKtx2`, ver [KTX2](../formats/ktx2.md)). Un fichero roto
  muestra el error en rojo.

Si la carpeta no existe o no tiene paquetes, la ventana lo dice y explica cómo crear el paquete base. Un paquete con
errores (un `pack.yaml` mal escrito, una dependencia que falta) sale con el mensaje del sistema de ficheros virtual.

## Límites

- **Solo texturas PNG y KTX2.** Los modelos (`.glb`) y los mapas necesitan el camino de render 3D, que este visor no
  usa: los lista y dice qué paquete los da, sin vista previa. Para verlos están `--model-viewer` y `--map <id>`
  ([prueba vertical](prueba-vertical.md#jugar)).
- El motor decodifica los PNG con su propio código (`assets::decodePng`), no con `formats/`, que solo usa `dwx`
  ([reglas de dependencias](../architecture/dependency-rules.md)). No acepta imágenes de más de 16384 píxeles de lado.
- El visor lee, decodifica y sube la textura al elegirla, en el mismo frame. Con texturas del tamaño de las del
  original no se nota.
- Es una herramienta de desarrollo: no forma parte de la UI del juego.
