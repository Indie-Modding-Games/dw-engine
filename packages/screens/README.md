# screens

Los modelos de vista de las pantallas del juego: qué enseñan el combate, el HUD del campo, la bolsa y los menús.

- Carpeta: `packages/screens`; objetivos de CMake: `dwe::screens` (capa `apps`).
- Depende de: `assets`, `battle`, `core`, `inventory`, `localization`, `partner`, `ruleset`, `session`, `ui`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [docs/modulos/apps/screens.md](../../docs/modulos/apps/screens.md).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter screens
scripts/dev.sh test --filter screens       # sus tests (etiqueta de ctest screens)
scripts/dev.sh test --filter ...screens    # y los de todo lo que depende de él
```
