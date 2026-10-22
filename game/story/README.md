# story

El estado de la historia (`dwe::game::story`, capa `game`): los disparadores y las variables de los scripts de eventos,
como los guarda el original (`script.h: ScriptState`). Hoy lo usan los warps de script del campo, que leen los
disparadores y aplican sus `sets_flags` ([warps de script](../../docs/architecture/warps-de-script.md)); el
intérprete de historia ([diseño](../../docs/architecture/interprete-historia.md)) lo heredará sin cambiar su forma.

- `StoryState`: 800 disparadores (0–799) y 256 variables de un byte. Un número fuera de rango es un error y no cambia
  nada.
- `toYaml`/`fromYaml`: la sección `story` de la partida guardada, versión `StoryStateVersion` (1). `triggers` son los
  100 bytes de bits en 200 cifras hexadecimales (el byte k lleva los disparadores 8k a 8k + 7; el n, en el bit n % 8,
  como el original), y `variables`, las 35 primeras (0–34, las que guarda `main_menu.c: writeSavegame`); las demás
  solo viven mientras corre el juego.

No conoce a los demás sistemas: la sesión (`apps/session`, `StoryAdapter`) crea el estado de partida nueva, lo
registra en la partida guardada y se lo da al campo.
