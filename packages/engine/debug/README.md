# debug

Las herramientas de desarrollo: la consola con el log y una línea de comandos de Lua, las estadísticas del frame y el
visor de assets.

- Carpeta: `packages/engine/debug`; objetivos de CMake: `dwe::debug` (capa `services`).
- Depende de: `assets`, `core`, `input`, `platform`, `rhi`, `ruleset`, `script`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [docs/modulos/engine/debug.md](../../../docs/modulos/engine/debug.md).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter debug
scripts/dev.sh test --filter debug       # sus tests (etiqueta de ctest debug)
scripts/dev.sh test --filter ...debug    # y los de todo lo que depende de él
```
