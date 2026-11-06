# platform

La ventana, los eventos y los mandos: traduce los eventos del sistema a tipos propios, así que el resto del motor no ve
la biblioteca de plataforma.

- Carpeta: `packages/engine/platform`; objetivos de CMake: `dwe::platform` (capa `base`).
- Depende de: `core`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [modulos/engine/platform](https://indie-modding-games.github.io/dw-engine/modulos/engine/platform).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter platform
scripts/dev.sh test --filter platform       # sus tests (etiqueta de ctest platform)
scripts/dev.sh test --filter ...platform    # y los de todo lo que depende de él
```
