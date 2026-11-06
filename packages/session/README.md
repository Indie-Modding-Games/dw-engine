# session

La partida: la librería que junta los sistemas de `packages/game`, que no se usan entre sí, en una partida que se juega
y se guarda.

- Carpeta: `packages/session`; objetivos de CMake: `dwe::session` (capa `apps`).
- Depende de: `assets`, `audio`, `battle`, `core`, `curling`, `evolution`, `field`, `fishing`, `gym`, `inventory`,
  `partner`, `ruleset`, `save`, `scene`, `script`, `story`, `tournament`, `weather`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [docs/modulos/apps/session.md](../../docs/modulos/apps/session.md).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter session
scripts/dev.sh test --filter session       # sus tests (etiqueta de ctest session)
scripts/dev.sh test --filter ...session    # y los de todo lo que depende de él
```
