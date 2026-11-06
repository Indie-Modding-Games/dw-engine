# game

`dwe-game`, el juego: abre la ventana, monta los paquetes, empieza la partida y dibuja el campo, el combate y los menús.

- Carpeta: `apps/game`; objetivos de CMake: el programa `game` (capa `apps`).
- Depende de: `animation`, `assets`, `audio`, `battle`, `content`, `core`, `curling`, `debug`, `evolution`, `field`,
  `fishing`, `gym`, `input`, `inventory`, `localization`, `partner`, `platform`, `render`, `rhi`, `ruleset`, `save`,
  `scene`, `screens`, `script`, `session`, `signs`, `story`, `tournament`, `ui`, `video`, `weather`.
- Estructura: `CMakeLists.txt` (el programa y sus dependencias), `src/` y `tests/`.
- Documentación: [modulos/apps/game](https://indie-modding-games.github.io/dw-engine/modulos/apps/game).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter game
scripts/dev.sh test --filter game       # sus tests (etiqueta de ctest game)
```
