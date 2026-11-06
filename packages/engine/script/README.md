# script

La máquina virtual de Lua y la única puerta de los mods al motor.

- Carpeta: `packages/engine/script`; objetivos de CMake: `dwe::script` (capa `services`).
- Depende de: `assets`, `core`, `input`, `localization`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [docs/modulos/engine/script.md](../../../docs/modulos/engine/script.md).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter script
scripts/dev.sh test --filter script       # sus tests (etiqueta de ctest script)
scripts/dev.sh test --filter ...script    # y los de todo lo que depende de él
```
