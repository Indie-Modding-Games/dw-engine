# weather

El clima del campo: qué tiempo hace en cada mapa y cuándo cambia.

- Carpeta: `packages/game/weather`; objetivos de CMake: `dwe::weather` (capa `game`).
- Depende de: `assets`, `core`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [modulos/game/weather](https://indie-modding-games.github.io/dw-engine/modulos/game/weather).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter weather
scripts/dev.sh test --filter weather       # sus tests (etiqueta de ctest weather)
scripts/dev.sh test --filter ...weather    # y los de todo lo que depende de él
```
