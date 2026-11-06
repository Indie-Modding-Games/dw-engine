# ruleset

El formato de ruleset: carga `rules/<id>/` de los paquetes, sigue `extends` y valida las tablas de reglas que leen los
sistemas del juego.

- Carpeta: `packages/engine/ruleset`; objetivos de CMake: `dwe::ruleset` (capa `services`).
- Depende de: `assets`, `core`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [modulos/engine/ruleset](https://indie-modding-games.github.io/dw-engine/modulos/engine/ruleset).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter ruleset
scripts/dev.sh test --filter ruleset       # sus tests (etiqueta de ctest ruleset)
scripts/dev.sh test --filter ...ruleset    # y los de todo lo que depende de él
```
