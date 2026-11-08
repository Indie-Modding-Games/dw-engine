# net

La red entre dos máquinas: UDP, un canal fiable y ordenado, lockstep y códigos para unirse.

- Carpeta: `packages/engine/net`; objetivos de CMake: `dwe::net` (capa `base`).
- Depende de: `core`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [modulos/engine/net](https://indie-modding-games.github.io/dw-engine/modulos/engine/net).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter net
scripts/dev.sh test --filter net       # sus tests (etiqueta de ctest net)
scripts/dev.sh test --filter ...net    # y los de todo lo que depende de él
```
