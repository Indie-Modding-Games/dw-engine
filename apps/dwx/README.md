# dwx

La herramienta de assets: lee la imagen del disco del usuario, extrae el paquete `base` y lo comprueba, cocina y
empaqueta paquetes y los valida ([tubería de assets](../../docs/architecture/assets-pipeline.md)). Es el único
programa que lee imágenes de disco (regla 2 de [dependencias](../../docs/architecture/dependency-rules.md)). El código
es un módulo (`dwe::dwx`) para que lo enlacen los tests; `src/main.cpp` es la línea de comandos (`dwx` sin argumentos
la explica).

## La caché de desarrollo (`dwx extract --cache`)

Con `--dev-assets` y `disc_image`, el juego y `dwe-sim` ejecutan `dwx extract --cache <carpeta> <imagen>` y montan la
carpeta que `dwx` escribe en su salida estándar, lo único que escribe allí
([assets de desarrollo](../../docs/architecture/assets-de-desarrollo.md#leer-el-disco-en-el-juego)). `disc_cache.hpp`
tiene la clave (`<sha1 del disco>-v<ExtractorVersion>`), la entrada que se escribe en una carpeta temporal y se
renombra a su sitio, el borrado de las entradas de versiones anteriores del mismo disco y el aviso de las carpetas
temporales de más de 24 horas.

Una entrada es una carpeta real con su `dwx-cache.yaml`: la caché nunca sigue un enlace (ni para usar una entrada ni
para borrarla), y lo que tenga el nombre de una entrada sin serlo (un enlace, un fichero, una carpeta sin su registro)
se borra y se extrae de nuevo. Antes del renombrado, `dwx` vuelca al disco (`fsync`) cada fichero y carpeta de la
entrada, y después la carpeta de la caché, para que un corte de luz no deje una entrada a medias con su nombre
definitivo. En Windows no se puede volcar una carpeta, así que ahí se confía en NTFS.

### Cuándo subir `ExtractorVersion`

`ExtractorVersion` (`src/disc_cache.hpp`) es la versión de lo que `dwx extract` escribe. **Súbela en el mismo commit
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
modelos de Digimon, textos del ejecutable...) no cambian la huella: si cambias uno de ellos, sube la versión igual.

### Trabajo futuro

Cada arranque calcula el SHA-1 de la imagen entera, también cuando la entrada ya existe (unos 12 s para 380 MB en la
build `ci`, de depuración con ASan). Si llega a molestar, un camino rápido sería recordar el tamaño y la fecha de
modificación de la imagen junto al SHA-1 y solo volver a calcularlo cuando cambien.
