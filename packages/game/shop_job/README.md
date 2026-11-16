# shop_job

El trabajo en la tienda del cañón, versión mejorada (opción `shop_job.mode`).

- Carpeta: `packages/game/shop_job`; objetivos de CMake: `dwe::shop_job` (capa `game`).
- Depende de: `assets`, `core`, `ruleset`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [modulos/game/shop_job](https://indie-modding-games.github.io/dw-engine/modulos/game/shop_job).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter shop_job
scripts/dev.sh test --filter shop_job       # sus tests (etiqueta de ctest shop_job)
scripts/dev.sh test --filter ...shop_job    # y los de todo lo que depende de él
```
