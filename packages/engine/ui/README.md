# ui

La UI del juego: documentos y estilos leídos por asset id, sus scripts y sus skins.

- Carpeta: `packages/engine/ui`; objetivos de CMake: `dwe::ui` (capa `services`).
- Depende de: `assets`, `core`, `input`, `platform`, `render`, `script`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [modulos/engine/ui](https://indie-modding-games.github.io/dw-engine/modulos/engine/ui).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter ui
scripts/dev.sh test --filter ui       # sus tests (etiqueta de ctest ui)
scripts/dev.sh test --filter ...ui    # y los de todo lo que depende de él
```
