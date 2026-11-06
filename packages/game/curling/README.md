# curling

El juego de las piedras de la pista de hielo.

- Carpeta: `packages/game/curling`; objetivos de CMake: `dwe::curling` (capa `game`).
- Depende de: `assets`, `core`, `ruleset`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [docs/modulos/game/curling.md](../../../docs/modulos/game/curling.md).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter curling
scripts/dev.sh test --filter curling       # sus tests (etiqueta de ctest curling)
scripts/dev.sh test --filter ...curling    # y los de todo lo que depende de él
```
