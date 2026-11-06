# gym

El gimnasio del pueblo: la coreografía de cada curso de entrenamiento y lo que da.

- Carpeta: `packages/game/gym`; objetivos de CMake: `dwe::gym` (capa `game`).
- Depende de: `assets`, `core`, `ruleset`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [docs/modulos/game/gym.md](../../../docs/modulos/game/gym.md).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter gym
scripts/dev.sh test --filter gym       # sus tests (etiqueta de ctest gym)
scripts/dev.sh test --filter ...gym    # y los de todo lo que depende de él
```
