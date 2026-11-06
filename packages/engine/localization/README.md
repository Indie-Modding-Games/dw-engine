# localization

Los textos que ve el jugador en su idioma: el catálogo de cada paquete y sus plantillas.

- Carpeta: `packages/engine/localization`; objetivos de CMake: `dwe::localization` (capa `systems`).
- Depende de: `assets`, `core`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [modulos/engine/localization](https://indie-modding-games.github.io/dw-engine/modulos/engine/localization).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter localization
scripts/dev.sh test --filter localization       # sus tests (etiqueta de ctest localization)
scripts/dev.sh test --filter ...localization    # y los de todo lo que depende de él
```
