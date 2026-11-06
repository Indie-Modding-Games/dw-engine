# video

Las películas del juego: lee frame a frame el vídeo que deja `dwx extract` y da la imagen que toca.

- Carpeta: `packages/engine/video`; objetivos de CMake: `dwe::video` (capa `systems`).
- Depende de: `assets`, `core`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [modulos/engine/video](https://indie-modding-games.github.io/dw-engine/modulos/engine/video).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter video
scripts/dev.sh test --filter video       # sus tests (etiqueta de ctest video)
scripts/dev.sh test --filter ...video    # y los de todo lo que depende de él
```
