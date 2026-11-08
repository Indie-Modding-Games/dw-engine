# versus

El protocolo del versus en línea: identidad, serie, alineaciones, resultados y abandonos entre dos máquinas.

- Carpeta: `packages/game/versus`; objetivos de CMake: `dwe::versus` (capa `game`).
- Depende de: `core`, `net`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [modulos/game/versus](https://indie-modding-games.github.io/dw-engine/modulos/game/versus).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter versus
scripts/dev.sh test --filter versus       # sus tests (etiqueta de ctest versus)
scripts/dev.sh test --filter ...versus    # y los de todo lo que depende de él
```
