# partner

El cuidado del partner: hambre, sueño, baño, enfermedad y heridas, felicidad y disciplina, y su vida.

- Carpeta: `packages/game/partner`; objetivos de CMake: `dwe::partner` (capa `game`).
- Depende de: `assets`, `core`, `ruleset`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [modulos/game/partner](https://indie-modding-games.github.io/dw-engine/modulos/game/partner).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter partner
scripts/dev.sh test --filter partner       # sus tests (etiqueta de ctest partner)
scripts/dev.sh test --filter ...partner    # y los de todo lo que depende de él
```
