# rhi

El único módulo que habla con la API gráfica: dispositivos, buffers, texturas, pipelines y la tabla bindless. Sus
`shaders/` guardan los módulos de shader que importan todos los demás (`import dwe.bindless;`).

- Carpeta: `packages/engine/rhi`; objetivos de CMake: `dwe::rhi` (capa `base`).
- Depende de: `core`, `platform`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [modulos/engine/rhi](https://indie-modding-games.github.io/dw-engine/modulos/engine/rhi).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter rhi
scripts/dev.sh test --filter rhi       # sus tests (etiqueta de ctest rhi)
scripts/dev.sh test --filter ...rhi    # y los de todo lo que depende de él
```
