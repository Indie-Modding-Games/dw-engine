# Rutas de autotest

`tests/autotest/` guarda las rutas: recorridos del juego escritos en YAML que `dwe-game --autotest <ruta.yaml>` juega
con los mismos eventos que un jugador (teclado, ratón o un mando virtual), así que pasan por el mapa de acciones, la UI
y el campo de verdad. No es un módulo de CMake: el ejecutor vive en `apps/game` (`autotest.hpp`, `autotest.cpp`) y las
rutas necesitan el contenido extraído de un disco, así que no corren en la CI.

- El formato de una ruta (cabecera, pasos y condiciones de `wait`): [formato de una
  ruta](../../guides/prueba-vertical.md#formato-de-una-ruta).
- Qué comprueba cada ruta de la prueba vertical: [autotests](../../guides/prueba-vertical.md#autotests).
- Correr muchas a la vez, en `en` y `es`: [el gate](../../guides/gate.md).

## Qué hay

| Rutas | Qué recorren |
|---|---|
| `route-keyboard`, `route-gamepad`, `route-mouse` y las de `mouse-*`, `walk-*`, `battle-*` | La entrada con cada dispositivo: andar, menús, combate y el tamaño de la ventana |
| `route-etapa1` … `route-etapa9` y sus variantes (`route-etapa2-*`…) | Las etapas de la historia y el candado de historia de cada una ([story](../game/story.md)) |
| `route-full-*`, `route-canonical-stages` | La campaña encadenada y las etapas canónicas |
| `route-save-*`, `route-sleep-save` | Guardar y cargar, también al dormir ([guardar y cargar](../../architecture/guardar-y-cargar.md#6-rutas-y-tests)) |
| `route-bank*`, `route-bulk-*`, `route-feed-*`, `route-partner-*`, `route-status-extras` | Pantallas y sistemas sueltos: tiendas y guardián, cambios en lote, cuidado del compañero, ficha |
| `route-settings-*`, `route-options*`, `route-skin*`, `route-language`, `route-text-*`, `route-signs*`, `route-minimap*` | Opciones, aspectos de la UI, idioma, textos que caben y carteles |
| `route-evolve`, `route-evolution-requirements`, `route-field-hud`, `route-free-exploration*`, `route-fade` | Evolución y sus requisitos, el HUD del campo, la exploración libre y los fundidos |
| `route-menu-stress` | El menú de juego abierto y cerrado cientos de veces: sobre una casilla de paso, con pulsaciones durante los fotogramas de su caja, con el compañero moviéndose, en la página del jugador y en la escala de UI más pequeña y la más grande |
| `route-mod-*`, `route-hidden-*`, `route-dev-assets` | Paquetes de mods, contenido oculto y assets de desarrollo |
| `training/route-training-<mapa>-<curso>.yaml` | Una sesión entera de cada curso de cada gimnasio, con `expect_range` sobre los ajustes `gym.session.*` ([comprobar el entrenamiento](../../guides/comprobar-entrenamiento.md)) |

Una ruta nueva entra sola en el nivel `daily` del gate (todas las `route-*.yaml` de la carpeta, sin las de
`training/`).

## Convenciones

- El nombre es `route-<tema>.yaml` y `name:` es el prefijo de sus capturas.
- Un comentario al principio dice qué comprueba, qué función del original sigue y con qué opciones se arranca
  (`--start <mapa> --new-game --seed 1`...): la ruta no las lleva dentro.
- Una ruta comprueba con pasos `expect_*` (`expect_setting`, `expect_range`, `expect_change`, `expect_clear`,
  `expect_text_fits`), no solo con capturas: una captura no falla sola.
- Lo que el juego echa a suertes va en pasos `when` o `script_if`, para que la ruta pase con cualquier tirada.
- Una ruta que nace de un reporte F12 lleva su partida (`load_save:`, relativa a la ruta).
- El código de salida es 0 si todo pasa, 2 si pasa pero se saltaron pasos `walk_to` o `wait`, y 1 si falla.

## Correr una

```sh
scripts/dev.sh run --content <carpeta extraída> --lang es --autotest tests/autotest/route-keyboard.yaml \
    --shots <carpeta de capturas>
scripts/dev.sh training-check --content <carpeta extraída>   # las de training/, sin ventana, con su resumen
```
