# sim

`dwe-sim`: juega escenarios de los sistemas del juego muchas veces, sin gráficos y con semillas, y escribe un informe.

- Carpeta: `apps/sim`; objetivos de CMake: `dwe::sim`, el programa `sim_cli` (capa `apps`).
- Depende de: `${game_modules}`, `assets`, `content`, `core`, `ruleset`, `session`.
- Estructura: `CMakeLists.txt` (el programa y sus dependencias), `src/` y `tests/`.
- Documentación: [modulos/apps/sim](https://indie-modding-games.github.io/dw-engine/modulos/apps/sim).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter sim
scripts/dev.sh test --filter sim       # sus tests (etiqueta de ctest sim)
```
