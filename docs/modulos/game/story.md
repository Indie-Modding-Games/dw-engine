# story

La historia (`dwe::game::story`, capa `game`): su estado y el runtime que ejecuta las secciones de los scripts de
eventos con la semántica del original. El diseño está en el [intérprete de historia](../../architecture/interprete-historia.md)
y el comportamiento del original en la [ficha de scripts](../../reference/scripts.md).

Depende de `core` y `assets`. No conoce Lua ni ningún otro `game/*`: lo que necesita del mundo lo pide por puertos que
implementa `apps/session`, y los tests, con dobles.

## Estado

`StoryState` tiene los disparadores y las variables de los scripts de eventos,
como los guarda el original (`script.h: ScriptState`). Hoy lo usan los warps de script del campo, que leen los
disparadores y aplican sus `sets_flags` ([warps de script](../../architecture/warps-de-script.md)); el
intérprete de historia ([diseño](../../architecture/interprete-historia.md)) lo heredará sin cambiar su forma.

- `StoryState`: 800 disparadores (0–799) y 256 variables de un byte. Un número fuera de rango es un error y no cambia
  nada.
- `toYaml`/`fromYaml`: la sección `story` de la partida guardada, versión `StoryStateVersion` (1). `triggers` son los
  100 bytes de bits en 200 cifras hexadecimales (el byte k lleva los disparadores 8k a 8k + 7; el n, en el bit n % 8,
  como el original), y `variables`, las 35 primeras (0–34, las que guarda `main_menu.c: writeSavegame`); las demás
  solo viven mientras corre el juego.

No conoce a los demás sistemas: la sesión (`apps/session`, `StoryAdapter`) crea el estado de partida nueva, lo
registra en la partida guardada y se lo da al campo.

## Runtime

- **`StoryRunner`**: el único intérprete. Corre un script a la vez (una petición con otro en marcha se descarta y se
  anota en el registro de depuración), da un fotograma del original cada `ticksPerFrame` ticks del motor (3 a 60 Hz,
  del ruleset) y reanuda la sección en curso una vez por fotograma salvo que espere. Una sección empieza en el
  fotograma siguiente a la petición (`StartTiming::NextFrame`) o en el acto (`StartTiming::Now`).
- **`SectionHost`**: cómo el runner arranca, reanuda y cierra una sección sin saber de Lua. La sesión lo implementa
  con la VM de scripts (una corrutina por sección). Una sección cede con una espera (`FrameWait`: n fotogramas, al
  menos uno, y el valor que recibe al volver; `FileJump`: salto a otro script) o acaba (`SectionEnded`,
  `SectionReturned`, `SectionFailed`).
- **Saltos a otro script** (`jump_to_file`): la sección de destino corre en el mismo fotograma. El original solo mueve
  el puntero del script: fuera de toda subrutina (`linkDepth` 0) la sección que salta termina y el destino ocupa su
  sitio, así que dos scripts pueden saltar uno al otro sin fin, como en el original. Dentro de `linkDepth` llamadas
  `jump_and_link`, sus marcos de vuelta siguen en la pila: la sección que saltó espera debajo del destino; si este
  acaba el script, se cierran las dos, y si vuelve sin nada a lo que volver (un `jump_return`), la que saltó recibe
  `nil` y su subrutina vuelve a quien la llamó. El host cuenta `linkDepth`, las llamadas desde las que se salta.
- **`StoryOrders`**: las órdenes que solo cambian el estado: `set_trigger`, `unset_trigger`, `set_pstat`,
  `add_to_pstat`, `reduce_pstat`, `copy_pstat`, `store_random` (`(límite × r) >> 15`, como el original) y `delay` (al
  menos un fotograma). Los operandos son enteros sin más: los anchos de byte o de 16 bits del original eran su
  codificación, no reglas del juego.
- **`StoryRules`**: las reglas del estado que sí ve el jugador, del ruleset. El valor de una variable va de 0 a
  `variableMax` (255 en el original): `set_pstat`, `add_to_pstat`, `reduce_pstat` y `store_random` se quedan en ese
  rango, como el tope de 255 y el suelo de 0 del original.
- **`ScriptMotion`** (movimientos con guion, `script_interp.c: tickScriptedMovement`): cada personaje tiene un hueco;
  una orden nueva sobre el mismo personaje ocupa su hueco y la anterior deja de contar. **Suposición** (leída del
  decomp; la medida en el original está en cola): al acabar un giro o un paseo, el personaje vuelve a su animación de
  reposo (la animación 0 del jugador, del compañero o del NPC) y se queda quieto. En el original es la animación la
  que lo lleva, así que un paseo al que un `entity_look_at_entity` quitó el hueco sigue hasta que el giro acaba y
  entonces para. Antes el motor guardaba la velocidad del paseo y el jugador seguía corriendo hasta la pared (trop03,
  Vegiemon: de (-708, -2323) hasta (-4647, 3941) en ejes del original). Diferencia que queda: durante el giro el motor
  mantiene la dirección del paseo en vez de torcerla con él (unos pocos fotogramas).
- **Puertos** (`story_ports.hpp`): `StoryStatePort` (disparadores y variables; dice cuántos tiene: 800 y 256 son el
  conjunto base del original y un mod puede ampliarlo) y `StoryRandomPort` (el generador de la sesión, de 0 a 0x7FFF).

## Guardas del motor

No son reglas del original sino redes contra scripts desbocados, muy por encima de lo que pide el disco:

- **Profundidad de llamadas en un salto:** 256 (`StoryRunner::MaxLinkDepth`), sumando las secciones que esperan bajo
  un salto. La pila del original tiene 8 marcos (`pushScriptStack`), pero es un límite de su memoria, no del juego.
- **Reanudaciones por fotograma:** 1024 (`StoryRunner::MaxResumesPerFrame`). El original no tiene límite; esto solo
  impide que una sección que nunca espera cuelgue el juego.

Pasar una guarda acaba el script y lo anota en el registro de errores con el script y la sección.

## Errores

Un disparador o una variable que el estado no tiene, pasar una guarda, un salto a una sección que no existe y un
`jump_return` sin nada a lo que volver son errores: la orden falla (la sección lo ve como un error de Lua) o el
runner acaba el script y lo anota en el registro de errores. Es la lista de la Q11 del diseño; nada más es un error.

## Pruebas

Los tests del módulo usan un `SectionHost` con pasos fijos y puertos falsos, sin Lua. La paridad de efectos sobre el
disco (incremento S2, tercera parte) debe correr varias vueltas de la sección `0x52` del script 49, que salta a la
`0x33` de los scripts 199–202 y vuelve sin fin en el original: es la prueba de que un `jump_to_file` no apila nada.
