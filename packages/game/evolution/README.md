# evolution

La evolución del partner: cuándo cambia de forma, a cuál y qué se lleva a la nueva.

- Carpeta: `packages/game/evolution`; objetivos de CMake: `dwe::evolution` (capa `game`).
- Depende de: `assets`, `core`, `ruleset`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [docs/modulos/game/evolution.md](../../../docs/modulos/game/evolution.md).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter evolution
scripts/dev.sh test --filter evolution       # sus tests (etiqueta de ctest evolution)
scripts/dev.sh test --filter ...evolution    # y los de todo lo que depende de él
```
