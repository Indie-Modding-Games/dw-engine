# launcher

El lanzador de escritorio de `dwe`: lee el disco del jugador, mantiene el juego al día con las versiones publicadas y
lo abre. Es una app de escritorio en Rust (`src-tauri/`) con la interfaz web en `ui/`, aparte de CMake.

- Carpeta: `apps/launcher`; `addons/` son los complementos de ejemplo y `tools/` el servidor de versiones falso de
  sus tests.
- Depende de: ningún paquete de CMake. Sus tests de punta a punta usan `apps/beta-gateway`.
- Documentación: [docs/modulos/apps/launcher.md](../../docs/modulos/apps/launcher.md).

Compilar y probar solo el lanzador (en su propia imagen, `scripts/launcher.sh`):

```sh
scripts/dev.sh build --filter launcher
scripts/dev.sh test --filter launcher
```
