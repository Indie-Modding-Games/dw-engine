# input

La entrada por acciones: el juego pregunta por acciones (`confirm`, `move_left`...), nunca por teclas o botones.

- Carpeta: `packages/engine/input`; objetivos de CMake: `dwe::input` (capa `systems`).
- Depende de: `core`, `platform`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [docs/modulos/engine/input.md](../../../docs/modulos/engine/input.md).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter input
scripts/dev.sh test --filter input       # sus tests (etiqueta de ctest input)
scripts/dev.sh test --filter ...input    # y los de todo lo que depende de él
```
