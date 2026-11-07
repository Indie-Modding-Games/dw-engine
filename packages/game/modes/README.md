# modes

Los modos de juego: el manifiesto `modes/<id>/mode.yaml` de cada modo y el catálogo de los que traen los paquetes.

- Carpeta: `packages/game/modes`; objetivos de CMake: `dwe::modes` (capa `game`).
- Depende de: `assets`, `core`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [modulos/game/modes](https://indie-modding-games.github.io/dw-engine/modulos/game/modes).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter modes
scripts/dev.sh test --filter modes       # sus tests (etiqueta de ctest modes)
scripts/dev.sh test --filter ...modes    # y los de todo lo que depende de él
```
