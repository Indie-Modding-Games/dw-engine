# formats

Los lectores de los formatos del disco original. Solo lo usa `dwx`: el runtime nunca depende de este paquete.

- Carpeta: `packages/formats`; objetivos de CMake: `dwe::formats` (capa `base`).
- Depende de: `core`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [docs/modulos/formats.md](../../docs/modulos/formats.md).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter formats
scripts/dev.sh test --filter formats       # sus tests (etiqueta de ctest formats)
scripts/dev.sh test --filter ...formats    # y los de todo lo que depende de él
```
