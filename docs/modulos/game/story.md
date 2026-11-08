# story

La historia (`dwe::game::story`, capa `game`): su estado y el runtime que ejecuta las secciones de los scripts de
eventos con la semántica del original. El diseño está en el [intérprete de historia](../../architecture/interprete-historia.md)
y el comportamiento del original en la [ficha de scripts](../../reference/scripts.md).

Depende de `core` y `assets`. No conoce Lua ni ningún otro `packages/game/*`: lo que necesita del mundo lo pide por puertos que
implementa `packages/session`, y los tests, con dobles.

## Estado

`StoryState` tiene los disparadores y las variables de los scripts de eventos,
como los guarda el original (`script.h: ScriptState`). Lo usan el intérprete (`StoryRunner`, por `StoryStatePort`) y
los warps de script del campo, que leen los disparadores y aplican sus `sets_flags`
([warps de script](../../architecture/warps-de-script.md)).

- `StoryState`: los 800 disparadores (0–799) y 256 variables del original (`BaseTriggerCount`,
  `BaseVariableCount`) más lo que añade el ruleset (`StoryCapacity`: `capacity` de la sección `story`, numerado a
  continuación; [capacidad](../../modding/historia.md#capacidad)). Las variables son enteros: el rango de cada una lo
  ponen las reglas (`StoryRules::variableMax` para las del original, `extraVariableMax` para las extra). Un número
  fuera de la capacidad es un error y no cambia nada; `setCapacity` cambia la capacidad conservando lo que cabe.
- `toYaml`/`fromYaml`: la sección `story` de la partida guardada, versión `StoryStateVersion` (2). `triggers` son
  los bytes de bits en cifras hexadecimales (el byte k lleva los disparadores 8k a 8k + 7; el n, en el bit n % 8,
  como el original; 100 bytes del original y luego los extra), `variables`, las 35 primeras (0–34, las que guarda
  `main_menu.c: writeSavegame`; las demás del original solo viven mientras corre el juego) y `extra_variables`,
  todas las extra. `fromYaml` lee en la capacidad del ruleset y descarta, con un aviso, lo que no cabe.
  `storyStateFromVersion1` es la migración de la versión 1 (exactamente 100 bytes y sin variables extra).

- `ScreenHistory`: la pantalla actual y la anterior con sus salidas (`map.c: changeMap`, `PREVIOUS_SCREEN`,
  `PREVIOUS_EXIT`), adonde vuelve una huida (`script_interp.c`: `START_BATTLE`). `arrive` sigue cada cambio de mapa
  que juega el campo (`StoryFieldLink::followScreen`): una salida a pie guarda su número de warp (trigger − 110,
  `checkMapInteraction`), un cambio de guion la salida por la que se entró (`map.c: scriptTickChangeMap`).
  La partida guardada lo lleva en la sección `screens` (`main_menu.c: writeSavegame` guarda las cuatro); una partida
  sin ella, o un arranque en un mapa, empieza la historia en el mapa donde aparece. Antes (hasta 2026-11-03, #179)
  nada lo movía tras la partida nueva y una huida llevaba al mapa de la partida nueva o al 0.

- `StoryNames` (`story_names.hpp`): las etiquetas de `story/names` del paquete `game` para disparadores y variables,
  solo para herramientas (mapper, control remoto, logs); `loadStoryNames` sin el fichero da ninguna. El esquema es
  `story_names` ([API de historia](../../modding/historia.md#nombres-de-disparadores-y-variables)).

No conoce a los demás sistemas: la sesión (`packages/session`, `StoryAdapter`) crea el estado de partida nueva, lo
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
  `species_ae`: de (-708, -2323) hasta (-4647, 3941) en ejes del original). Diferencia que queda: durante el giro el motor
  mantiene la dirección del paseo en vez de torcerla con él (unos pocos fotogramas).
- **Un paseo hacia un personaje mira al destino y choca por los bordes de delante** (`tamer.c: tickEntityWalkTo`,
  `map.c: entityCheckCollision`, `entityCheckEntityCollision`, `entityMoveForward`): cada fotograma el que camina mira
  al destino (`entityLookAtLocation`), y el paseo acaba al pisar la casilla del destino o al chocar con un personaje
  (los 0 a 9). La prueba avanza al que camina por el impulso de su clip, que `startAnimation` deja a cero, así que el
  primer fotograma no prueba paso; el jugador en el campo prueba su cuadrado entero y los demás solo los bordes que
  van delante (los ejes con paso distinto de 0, con bordes que se tocan contando). El primer paso del clip de andar ya
  va hacia el destino. Igual. Medido en el original (gias04, script 70, sección 0x33, la emboscada): el compañero se
  acerca al jugador y para en (−454, 3314) con el jugador en (−697, 3149); tras la animación 22 durante el `delay` de
  20 fotogramas, el jugador sale al oeste a 50 por fotograma y el compañero da su primer paso, de 30, hacia el ave
  ((−474, 3291)) y llega a (−805, 2963). Antes el motor probaba un círculo contra el jugador y el compañero en todos
  los fotogramas, el primero incluido, y el primer paso del clip iba hacia donde el personaje miraba antes (50 hacia el
  compañero en el jugador): el compañero chocaba al momento y se quedaba quieto. Ahora para en (−460, 3315), da su
  primer paso a (−480, 3292), el jugador a (−747, 3151), y llega a (−796, 2972). `script_motion_tests`: "a walk to a
  character takes its first frame whatever stands ahead", "a walk faces its point as it starts, before the clip's
  first step"; `field_tests`: "a scripted walk bumps on its clip's step, into what lies ahead or, the player, anyone",
  "a step under one unit moves no edge into the collision test". Queda: el motor desliza el paseo a la velocidad de
  suelo del clip (45 por fotograma en el compañero) en vez de por el impulso de cada fotograma (~30 medido).
- **Un paseo a un punto en la esquina de su casilla no da vueltas sin fin** (`tamer.c: tickEntityWalkTo`,
  `getModelTile`): el paseo acaba al pisar la casilla del punto, y un punto de coordenadas redondas está en la esquina
  de su casilla. Quien llega por un lado que no es el de la casilla cruza la esquina sin pisarla, se vuelve hacia el
  punto y la cruza otra vez: con pasos exactos, para siempre. En el original los pasos van en unidades enteras (el
  paso del clip girado y desplazado), que se apartan un poco de la línea en cada pasada hasta que una cae en la
  casilla. mgen13 por la mansión (script 63, sección 0x3c): tras el combate fijo el jefe vuelve a (−100, 800) desde
  donde acabó el combate y `wait_for_entity(6)` no acababa nunca, así que el disparador 202 no llegaba. Ahora, si el
  paso de este fotograma pasa el punto sin acabar en su casilla, el que camina se queda en el punto (que siempre
  está en su casilla) y el paseo acaba. Suposición: el original acaba unas pasadas después y un poco más allá. Los
  demás paseos acaban como antes, en el primer paso dentro de la casilla. `script_motion_tests`: "a walk at a point on
  its cell's corner from outside the cell stops on the point", "a walk still ends as it steps into its point's
  cell, short of the point"; `route-etapa9-back-mansion.yaml`.
- **El paso de un deslizamiento es uno para todos** (`tamer.c: tickEntityMoveTo`, `tickEntityMoveToAxis`, con
  `MOVE_TO_DELTA_X`/`MOVE_TO_DELTA_Z` y `PREVIOUS_CAMERA_POS_INITIALIZED`): el primer fotograma de un `entity_move_to`
  o `entity_move_to_axis` toma su paso (la distancia en unidades enteras dividida entre los fotogramas, con división
  entera) y lo guarda; el paso solo se suelta cuando un deslizamiento llega. Una orden nueva mientras otra corre
  sigue con el paso de la anterior y llega en cuanto lo pasa. Igual. Medido en el original (mgen06, el jefe del
  script 194, `w21_mgen06_devimon_leaves`): el primer deslizamiento toma (−107, −12) desde (−2405, 298); la orden
  siguiente, dada antes de que llegue, sigue con ese paso y llega a los ~29 fotogramas; la de después, ya suelto,
  toma (46, 4) desde (−5487, −47) y llega a los ~21. Antes el motor tomaba un paso nuevo por orden y, como el jefe se
  aleja con su raíz, no llegaba nunca: lo cortaba a los 40 fotogramas de más (ya quitado). `script_motion_tests`:
  "a move to a point ordered while another runs goes on with that one's step", "a move to a point ordered after one
  ended takes a new step", "a move to a point closer than its count ends at once where it stands". Queda: en el
  original el paseo con cámara (`entity_walk_to` con cámara) también usa esa marca; el motor no la comparte.
- **Un deslizamiento acaba donde lo deja el paso que pasa el punto, no en el punto** (`tamer.c: tickEntityMoveTo`,
  `tickEntityMoveToAxis`; `anim.c: applyRootMomentum`): cada fotograma el paso mueve la posición y también la
  posición propia de la animación (`setEntityPosition`); el eje que pasa el punto se pone en el punto, pero el
  siguiente tick de la animación vuelve a copiar su posición propia (`anim->locX >> 15`) y lo deja donde lo llevó el
  paso. Un eje que ya pasó sigue dando pasos hasta que pasan los dos, y el deslizamiento acaba con el personaje hasta
  un paso más allá. Igual. Medido en el original (2026-11-08, `twnb03` de noche, el tendero de la caja del mercado,
  entidad 7, script 163 al entrar: `entity_move_to(7, -1232, 2903, 10)` desde (225, −143)): paso (−145, 304), acaba
  en (−1370, 0, 3201) a los once pasos, con la posición de la animación igual (−1370 y 3201 en `anim.locX`/`locZ`
  desplazados 15 bits). Antes el motor lo ponía en el punto, (−1232, 2903), más cerca de la cámara y medio hundido
  en su caja; ahora (−1370, 3201). `entity_move_to_axis` hace lo mismo en su eje (Suposición: mismo código, no
  medido aparte). `script_motion_tests`: "a move to a point ends where the step that takes it past the point leaves
  it", "a move to an axis position ends where the step that takes it past leaves it".
- **Cada sección empieza con las cajas en modo confirmar** (`script_interp.c: callScriptSection`, `TEXTBOX_CLOSE_MODE`
  0, y su `initializeTextbox`, que pone a 0 los fotogramas del modo temporizado): el modo que dejó la sección anterior
  (automático o temporizado) no pasa a la siguiente. Antes el motor lo arrastraba: una sección que acababa en modo
  automático (`textbox_mode(1, 0)`) hacía que la primera página de la siguiente se cerrara sola. Igual
  (`dialogue_mode_tests.cpp`). El cerrojo de confirmar al abrir una caja (`setFreshDialogue`,
  `isXPressedAfterDialogue`) es `Box::freshGuard`. Igual.
- **La pila de subrutinas** (`popScriptStack`, `returnFromTop`): igual, sin el tope de 8 marcos del original, que era
  de su memoria y no del juego (Mejora sin efecto en los scripts del disco).
- **Puertos** (`story_ports.hpp`): `StoryStatePort` (disparadores y variables; dice cuántos tiene: 800 y 256 son el
  conjunto base del original y un mod puede ampliarlo) y `StoryRandomPort` (el generador de la sesión, de 0 a 0x7FFF).

Las demás piezas del runtime, una por cabecera: `dialogue.hpp` (las cajas de texto, sus páginas y elecciones),
`frame_systems.hpp` (lo que corre en el fotograma de la historia además de las secciones), `story_task.hpp` (las
esperas que termina otro sistema), `story_battle.hpp` (`start_battle` y su resultado), `story_routines.hpp` y
`partner_routines.hpp` (`call_routine` y `call_partner_routine`, que registra cada sistema), `npc_orders.hpp` (poner y
quitar los NPC del mapa), `new_day.hpp` (lo que hace un día nuevo a las variables), `prosperity.hpp` (la prosperidad de
la ciudad) y `medals.hpp` (las medallas que se ganan andando por el campo).

## El puntero en los diálogos

Mejora para PC (el original no tiene puntero) que no cambia ninguna regla, así que no tiene opción: en una elección
del cuadro de texto, `DialogueKeys::clicked` elige esa opción aunque el cursor esté en otra (con el sonido de
confirmar), y `DialogueKeys::pointed` lleva el cursor a la opción bajo el puntero cuando éste se mueve, sin sonido:
es el resaltado al pasar el ratón. Un índice fuera de las opciones no hace nada, y las teclas y el mando siguen como
siempre. Quien llama da las filas: en el juego, `optionPointer` (`apps/game/src/story_text_rows.hpp`) mira las cajas
`row-N` del documento `ui/story_textbox`. Fuera de una elección, un clic en cualquier sitio pasa la página, como
confirmar; en una elección, un clic fuera de las opciones no hace nada (Intro o el mando eligen la del cursor). Tests:
`packages/game/story/tests/dialogue_pointer_tests.cpp`.

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

Hablar con un personaje cuya sección no está en el guion del mapa no es un error: en el original
`getScriptSection` da un puntero nulo, el intérprete lee los bytes de la dirección 0 y el guion acaba al momento
(medido en el original con el personaje 0 de room08 apuntado a la sección 0x60: no pasa nada). Así le pasa al
tendero de cartas de gias06b, el personaje 5 del guion 126, que solo tiene las secciones 0x51 y 0x52 del mostrador.
Lo mismo vale para una casilla del mapa (`tamer.c: checkMapInteraction`, `SectionSource::Cell`): en topn01 fuera de
la apertura la casilla de la sección 0x42 no tiene sección en el guion 193 y el runner la acaba sin anotar un error
(antes dejaba una línea `[error]` por segundo de pie en ella). La casilla pide su sección cada fotograma: tras la primera
vez el runner recuerda que falta y la acaba sin más líneas hasta el siguiente `set_script`; de una casilla con
sección solo se anota la primera petición mientras el jugador sigue en ella. Las demás fuentes sin sección siguen
siendo un error del contenido.

## Pruebas

Los tests del módulo usan un `SectionHost` con pasos fijos y puertos falsos, sin Lua. `story_names_tests.cpp` lee las etiquetas,
también las del paquete `game`. Los parches por sección (`story/patches/*.lua`) los carga `StoryLuaHost` en
`packages/session` y los prueba `packages/session/tests/story_patch_tests.cpp`
([moddabilidad](../../architecture/interprete-historia.md#moddabilidad-cómo-un-mod-cambia-una-sección)). Sobre el disco extraído,
`packages/session/tests/story_lua_disc_tests.cpp` («the training loop of script 49 runs as tail jumps») corre varias
vueltas de la sección `0x52` del script 49, que salta a la `0x33` de los scripts 199–202 y vuelve sin fin en el
original: es la prueba de que un `jump_to_file` no apila nada.
