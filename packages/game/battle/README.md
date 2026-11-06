# battle

Las reglas del combate como una simulación determinista y sin pantalla.

- Carpeta: `packages/game/battle`; objetivos de CMake: `dwe::battle` (capa `game`).
- Depende de: `assets`, `core`, `ruleset`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [modulos/game/battle](https://indie-modding-games.github.io/dw-engine/modulos/game/battle).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter battle
scripts/dev.sh test --filter battle       # sus tests (etiqueta de ctest battle)
scripts/dev.sh test --filter ...battle    # y los de todo lo que depende de él
```
