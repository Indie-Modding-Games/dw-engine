# story

La historia: su estado y el runtime que ejecuta las secciones de los scripts de los mapas.

- Carpeta: `packages/game/story`; objetivos de CMake: `dwe::story` (capa `game`).
- Depende de: `assets`, `core`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [modulos/game/story](https://indie-modding-games.github.io/dw-engine/modulos/game/story).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter story
scripts/dev.sh test --filter story       # sus tests (etiqueta de ctest story)
scripts/dev.sh test --filter ...story    # y los de todo lo que depende de él
```
