# render

El render del juego, encima de `rhi`: mapas, modelos, la UI y la gradación de la escena.

- Carpeta: `packages/engine/render`; objetivos de CMake: `dwe::render` (capa `systems`).
- Depende de: `assets`, `core`, `rhi`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [modulos/engine/render](https://indie-modding-games.github.io/dw-engine/modulos/engine/render).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter render
scripts/dev.sh test --filter render       # sus tests (etiqueta de ctest render)
scripts/dev.sh test --filter ...render    # y los de todo lo que depende de él
```
