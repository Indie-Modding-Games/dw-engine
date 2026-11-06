# field

La lógica del campo: el jugador se mueve por un mapa, choca, cambia de pantalla y habla con los NPC.

- Carpeta: `packages/game/field`; objetivos de CMake: `dwe::field` (capa `game`).
- Depende de: `assets`, `core`, `scene`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [modulos/game/field](https://indie-modding-games.github.io/dw-engine/modulos/game/field).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter field
scripts/dev.sh test --filter field       # sus tests (etiqueta de ctest field)
scripts/dev.sh test --filter ...field    # y los de todo lo que depende de él
```
