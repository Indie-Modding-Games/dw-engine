# inventory

El inventario: la bolsa del jugador, su dinero, el uso de objetos y el comercio.

- Carpeta: `packages/game/inventory`; objetivos de CMake: `dwe::inventory` (capa `game`).
- Depende de: `assets`, `core`, `ruleset`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [docs/modulos/game/inventory.md](../../../docs/modulos/game/inventory.md).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter inventory
scripts/dev.sh test --filter inventory       # sus tests (etiqueta de ctest inventory)
scripts/dev.sh test --filter ...inventory    # y los de todo lo que depende de él
```
