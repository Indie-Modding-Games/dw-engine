# tournament

Los torneos de la arena: los rivales de cada copa, el cuadro y los premios.

- Carpeta: `packages/game/tournament`; objetivos de CMake: `dwe::tournament` (capa `game`).
- Depende de: `assets`, `core`, `ruleset`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [docs/modulos/game/tournament.md](../../../docs/modulos/game/tournament.md).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter tournament
scripts/dev.sh test --filter tournament       # sus tests (etiqueta de ctest tournament)
scripts/dev.sh test --filter ...tournament    # y los de todo lo que depende de él
```
