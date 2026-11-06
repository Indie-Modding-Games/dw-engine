# signs

Los carteles pintados en las imágenes de los mapas, escritos otra vez en el idioma del jugador.

- Carpeta: `packages/engine/signs`; objetivos de CMake: `dwe::signs` (capa `systems`).
- Depende de: `assets`, `core`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [docs/modulos/engine/signs.md](../../../docs/modulos/engine/signs.md).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter signs
scripts/dev.sh test --filter signs       # sus tests (etiqueta de ctest signs)
scripts/dev.sh test --filter ...signs    # y los de todo lo que depende de él
```
