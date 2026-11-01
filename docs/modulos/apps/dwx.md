# dwx

La herramienta de assets: lee la imagen del disco del usuario, extrae el paquete `base` y lo comprueba, cocina y
empaqueta paquetes y los valida ([tubería de assets](../../architecture/assets-pipeline.md)). Es el único
programa que lee imágenes de disco (regla 2 de [dependencias](../../architecture/dependency-rules.md)). El código
es un módulo (`dwe::dwx`) para que lo enlacen los tests; `src/main.cpp` es la línea de comandos (`dwx` sin argumentos
la explica).

## El primer arranque del juego (`dwx identify`, `dwx extract --progress`)

La pantalla de primer arranque del juego ejecuta `dwx identify <imagen>`, que
escribe `supported <versión>` (sale con 0), `other-version <versión>` para una versión conocida de otra región cuyas tablas
no están localizadas (3; hoy `eu`, `jp` y `jp_rev1`, que se reconocen por la huella del ejecutable) o `unknown -` (1), y 2 si la imagen no se puede leer. Después ejecuta `dwx extract --progress`, que además
del informe escribe en la salida de error una línea `progress <porcentaje> <paso>` por cada cambio (`read`,
`extract` del 2 al 80 por archivos leídos, `write`, `check`, `upscale`, `done`). Los dos los prueba
`tests/identify_tests.cpp`.

## La caché de desarrollo (`dwx extract --cache`)

Con `--dev-assets` y `disc_image`, el juego y `dwe-sim` ejecutan `dwx extract --cache <carpeta> <imagen>` y montan la
carpeta que `dwx` escribe en su salida estándar, lo único que escribe allí
([assets de desarrollo](../../architecture/assets-de-desarrollo.md#leer-el-disco-en-el-juego)). `disc_cache.hpp`
tiene la clave (`<sha1 del disco>-v<ExtractorVersion>`), la entrada que se escribe en una carpeta temporal y se
renombra a su sitio, el borrado de las entradas de versiones anteriores del mismo disco y el aviso de las carpetas
temporales de más de 24 horas.

Una entrada es una carpeta real con su `dwx-cache.yaml`: la caché nunca sigue un enlace (ni para usar una entrada ni
para borrarla), y lo que tenga el nombre de una entrada sin serlo (un enlace, un fichero, una carpeta sin su registro)
se borra y se extrae de nuevo. Antes del renombrado, `dwx` vuelca al disco (`fsync`) cada fichero y carpeta de la
entrada, y después la carpeta de la caché, para que un corte de luz no deje una entrada a medias con su nombre
definitivo. En Windows no se puede volcar una carpeta, así que ahí se confía en NTFS.

### Cuándo subir `ExtractorVersion`

`ExtractorVersion` (`engine/assets/include/dwe/assets/extractor_version.hpp`, compartida con el juego) es la
versión de lo que `dwx extract` escribe; el juego vuelve a leer el disco del jugador cuando su contenido instalado
es de otra versión. **Súbela en el mismo commit
que cambie la salida para el mismo disco**: un extractor nuevo o cambiado, otro nombre o formato de un fichero, otro
`pack.yaml` o informe de cobertura. Si no, la caché de los demás sigue sirviendo la salida vieja sin avisar.

El test «the extractor's output for the synthetic disc matches the fingerprint of its version»
(`tests/disc_cache_tests.cpp`) lo vigila en parte: extrae un disco sintético que llega a varios extractores (arranque,
imágenes TIM, colisión de arenas, guiones, efectos, modelos) y compara la huella de lo que escribe. Cuando falla con
«bump ExtractorVersion and update the fingerprint»:

1. sube `ExtractorVersion`;
2. añade al final de la tabla `Fingerprints` una fila con la versión nueva y la huella nueva (la del mensaje del
   test). La tabla solo crece: nunca se edita una fila, y otro test comprueba que las versiones suben y que cada una
   tiene su propia huella, así que una versión nunca nombra dos salidas distintas.

El disco sintético no tiene las tablas de un ejecutable conocido, así que los extractores que las necesitan (mapas,
modelos de Digimon, textos del ejecutable...) no cambian la huella: si cambias uno de ellos, sube la versión igual
y añade la fila con la huella anterior y `.unseenBySyntheticDisc = true` (la versión 5, los warps de conversación y
de transporte, es así). El test de la tabla acepta esa huella repetida solo en una fila marcada.

## `dwx upscale` y las recetas incluidas

```sh
dwx upscale <receta.recipe.yaml> <carpeta de un paquete | carpeta de contenido> -o <carpeta>
```

Agranda imágenes indexadas de los paquetes con el paso integrado `index_scale` y las escribe, con su `_meta.yaml` y
`scale: k`, en un paquete de capa `remaster` con el id de la receta. Las imágenes conservan su asset id, así que el
paquete tapa a la base sin tocar nada más. Lee un subconjunto determinista de las recetas del remaster
([tubería del remaster](../../architecture/remaster-pipeline.md#lo-que-ya-hay-dwx-upscale)): `recipe: 1`, `id`,
`asset_type` (`ui` o `icon`), `select.include` con ids de asset, `scale` y un único paso `builtin: index_scale` con
`params.mode` (`edge` o `nearest`). Sin proveedores, modelos ni red: los mismos bytes en cualquier máquina.

- Lee los paquetes de la carpeta dada, sin contar la de salida, así que repetirlo lee siempre la base y no su propia
  salida.
- Escribe en `<salida>.partial` y lo renombra. Solo sustituye una carpeta que ya sea un paquete `remaster` con el id
  de la receta; cualquier otra se rechaza sin tocarla.
- Si no hay ninguna imagen que agrandar no escribe nada.

**Las recetas incluidas.** `content/enhance/ui_original.recipe.yaml` va dentro de `dwx` al compilarlo
(`src/bundled_recipes.hpp.in`) y `dwx extract` la ejecuta al terminar el paquete base, también para la caché de
desarrollo. Escribe `ui_original` junto al paquete base y se salta las imágenes que el disco no traiga. Cambiar la
receta cambia la salida de `dwx extract`: sube `ExtractorVersion` en el mismo commit (el disco sintético trae una
imagen de la receta, `ETCDAT/SYSTEM_W.TIM` inventada, para que la huella lo vea). La versión 2 es la primera que
escribe `ui_original`.

### Trabajo futuro

Cada arranque calcula el SHA-1 de la imagen entera, también cuando la entrada ya existe (unos 12 s para 380 MB en la
build `ci`, de depuración con ASan). Si llega a molestar, un camino rápido sería recordar el tamaño y la fecha de
modificación de la imagen junto al SHA-1 y solo volver a calcularlo cuando cambien.
