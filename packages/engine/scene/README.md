# scene

Las entidades del mundo y sus componentes (ECS).

- Carpeta: `packages/engine/scene`; objetivos de CMake: `dwe::scene` (capa `systems`).
- Depende de: `core`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [modulos/engine/scene](https://indie-modding-games.github.io/dw-engine/modulos/engine/scene).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter scene
scripts/dev.sh test --filter scene       # sus tests (etiqueta de ctest scene)
scripts/dev.sh test --filter ...scene    # y los de todo lo que depende de él
```
