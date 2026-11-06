# save

Las partidas guardadas: el fichero YAML versionado, las secciones de cada sistema y sus migraciones.

- Carpeta: `packages/game/save`; objetivos de CMake: `dwe::save` (capa `game`).
- Depende de: `assets`, `core`, `ruleset`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [docs/modulos/game/save.md](../../../docs/modulos/game/save.md).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter save
scripts/dev.sh test --filter save       # sus tests (etiqueta de ctest save)
scripts/dev.sh test --filter ...save    # y los de todo lo que depende de él
```
