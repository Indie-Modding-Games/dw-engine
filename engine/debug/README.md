# debug

Herramientas de desarrollo con Dear ImGui: consola con el log y una línea de comandos de Lua, estadísticas del frame y estado de los mandos y las acciones. Se abre con la acción `debug_console` (`º` o F1). Desde la ventana Frame se abre el [visor de assets](../../docs/guides/visor-de-assets.md), que monta los paquetes de una carpeta y enseña sus texturas. No es la UI del juego: esa es `ui`, con RmlUi.

## Panel de ajuste (R3.4)

El panel **Tuning** cambia el ruleset con el juego en marcha. Se abre con la casilla `tuning panel` de la ventana Frame,
con la orden `/tuning` en la consola (las líneas que empiezan por `/` son órdenes del overlay, no Lua) o arrancando el
juego con `--tuning`. La app le da los paquetes, el `RulesetStore` y el `GameClock` con `DebugUi::attachTuning`.

- **Ruleset:** elige el ruleset entre los de los paquetes (`RulesetStore::select`) y enseña su revisión. Cada sección es
  un árbol de sus valores: los números y los textos (nombres de estrategia incluidos) se editan en su campo y se
  aplican con Enter, y los `true`/`false` (las `quirks`) con una casilla, todos con `RulesetStore::setValue`. Si las
  comprobaciones lo rechazan, el error sale en rojo y el valor sigue el de antes. *What would reloading change?* lista
  lo que una recarga cambiaría (los valores ajustados que se pierden y lo editado en los ficheros); *reload from files*
  recarga. *Save to file* escribe la sección en el fichero de su paquete (el del ruleset que la define, aunque se
  herede), cambiando solo los valores en su sitio: los comentarios con las citas del decomp se quedan.
- **Clock:** la hora de juego, en marcha o parado, la velocidad (deslizador y botones 0, 1, 10, 60 y 3600) y el salto a
  una hora como al dormir (`GameClock::jumpTo`).
- **Systems:** las secciones activas, de qué ruleset viene cada una y su fichero, con la revisión. Un sistema añade su
  propia pestaña con `DebugUi::addTuningTab({nombre, filas})`: cada fila es un nombre y un valor, o un medidor de 0 a 1.

La lógica sin widgets está en `src/tuning_model.hpp` (`TuningModel`, `buildTuningTree`, `yamlOfEdit`) y se prueba en
`tests/tuning_model_tests.cpp`; los widgets, en `src/tuning_panel.cpp`.

### Prueba manual

1. `dwe-game --tuning` (o `º`/F1 y `/tuning` en la consola). La pestaña Ruleset enseña `original`, revisión 1, y las
   secciones `care` y `clock` abiertas.
2. En `care › hunger › window_minutes` escribir `45` y Enter: la revisión pasa a 2 y sale `care.hunger.window_minutes =
   45` en verde.
3. En `clock › frames_per_minute` escribir `0` y Enter: sale en rojo `... frames_per_minute must be more than zero`, el
   campo vuelve a `20` y la revisión no cambia.
4. Desmarcar `care › quirks › sleeping_hungry_keeps_weight`: revisión 3.
5. *What would reloading change?*: lista `care.hunger.window_minutes: 45 -> 90` y la rareza. *Reload from files*: los
   valores vuelven a los del fichero y la revisión sube.
6. Cambiar `window_minutes` otra vez y *save to file*: `git diff content/packs/game/rules/original/care.yaml` enseña
   solo esa línea cambiada, con los comentarios intactos. Deshacerlo con `git checkout`.
7. Pestaña Clock: pulsar `x3600` y ver correr las horas; desmarcar `running` y ver que se para; saltar a las 20:00.
8. Pestaña Systems: las dos secciones, del ruleset `original`, con la revisión actual.
