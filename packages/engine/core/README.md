# core

Lo que usan todos los módulos: `Result` y errores sin excepciones, el log, las plantillas de mensajes, la matemática y
los tipos básicos.

- Carpeta: `packages/engine/core`; objetivos de CMake: `dwe::core` (capa `core`).
- Depende de: ningún otro paquete.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [docs/modulos/engine/core.md](../../../docs/modulos/engine/core.md).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter core
scripts/dev.sh test --filter core       # sus tests (etiqueta de ctest core)
scripts/dev.sh test --filter ...core    # y los de todo lo que depende de él
```
