# fishing

La pesca: la caña, el corcho, el sedal y los peces del agua de un mapa.

- Carpeta: `packages/game/fishing`; objetivos de CMake: `dwe::fishing` (capa `game`).
- Depende de: `assets`, `core`, `ruleset`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [docs/modulos/game/fishing.md](../../../docs/modulos/game/fishing.md).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter fishing
scripts/dev.sh test --filter fishing       # sus tests (etiqueta de ctest fishing)
scripts/dev.sh test --filter ...fishing    # y los de todo lo que depende de él
```
