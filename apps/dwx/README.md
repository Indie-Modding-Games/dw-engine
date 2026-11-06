# dwx

La herramienta de assets: lee la imagen del disco del jugador, extrae el paquete `base`, lo comprueba, lo cocina y lo
empaqueta.

- Carpeta: `apps/dwx`; objetivos de CMake: `dwe::dwx`, el programa `dwx_cli` (capa `apps`).
- Depende de: `assets`, `core`, `formats`.
- Estructura: `CMakeLists.txt` (el programa y sus dependencias), `src/` y `tests/`.
- Documentación: [docs/modulos/apps/dwx.md](../../docs/modulos/apps/dwx.md).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter dwx
scripts/dev.sh test --filter dwx       # sus tests (etiqueta de ctest dwx)
```
