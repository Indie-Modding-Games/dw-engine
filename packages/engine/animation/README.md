# animation

Reproduce las animaciones de los modelos que carga `assets`: muestrea los clips y mezcla las poses.

- Carpeta: `packages/engine/animation`; objetivos de CMake: `dwe::animation` (capa `systems`).
- Depende de: `assets`, `core`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [docs/modulos/engine/animation.md](../../../docs/modulos/engine/animation.md).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter animation
scripts/dev.sh test --filter animation       # sus tests (etiqueta de ctest animation)
scripts/dev.sh test --filter ...animation    # y los de todo lo que depende de él
```
