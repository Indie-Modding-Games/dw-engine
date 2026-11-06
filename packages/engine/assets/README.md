# assets

El contenido por paquetes: los ids estables de los assets, el `pack.yaml` de cada paquete y el sistema de ficheros
virtual que los monta por capas.

- Carpeta: `packages/engine/assets`; objetivos de CMake: `dwe::assets` (capa `systems`).
- Depende de: `core`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [modulos/engine/assets](https://indie-modding-games.github.io/dw-engine/modulos/engine/assets).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter assets
scripts/dev.sh test --filter assets       # sus tests (etiqueta de ctest assets)
scripts/dev.sh test --filter ...assets    # y los de todo lo que depende de él
```
