# audio

El sonido: la salida al dispositivo, el mezclador, las voces, la música y los volúmenes.

- Carpeta: `packages/engine/audio`; objetivos de CMake: `dwe::audio` (capa `systems`).
- Depende de: `assets`, `core`, `platform`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [docs/modulos/engine/audio.md](../../../docs/modulos/engine/audio.md).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter audio
scripts/dev.sh test --filter audio       # sus tests (etiqueta de ctest audio)
scripts/dev.sh test --filter ...audio    # y los de todo lo que depende de él
```
