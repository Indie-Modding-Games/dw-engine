# Guardar y cargar

Estado: **diseño aprobado** (save-load-ui, tramo 1; decidido por el coordinador el 2026-10-22: las 11 preguntas,
como se recomienda). La implementación empieza cuando la parte (b) de los warps de script esté en `main`, porque la
partida que se guarda ya lleva su sección `story`.

Este documento dice cómo guarda y carga el jugador: dónde puede guardar, cómo se elige la ranura, cómo se vuelve a la
portada y cómo se carga desde ella. El formato del fichero, las migraciones y el autoguardado ya están en
[save.md](save.md); aquí solo se cambia lo que la interfaz necesita. Las citas son al decomp (`dw_decomp`,
`src/main/`, versión `us`) como `fichero.c: función: línea`, para leer, nunca para copiar código ni tablas.

## 1. Cómo lo hace el original

### 1.1 Lo esencial

En el original **el jugador nunca elige ranura al guardar**. La ranura (tarjeta y bloque) se elige una vez, al empezar
una partida nueva o al continuar una, y cada guardado posterior escribe encima de ese fichero sin preguntar dónde
(`main_menu.c: tickMainMenu: 3929-3953`, que busca el fichero de la ranura ya elegida). Si al empezar se eligió «sin
tarjeta», ningún guardado de la partida ocurre (`main_menu.c: tickMainMenu: 3646-3647`; `partner.c: partnerTickSleep:
330-334`; `main_menu.c: openSaveMachine: 5284-5286`).

### 1.2 Dónde se guarda

| Sitio | Cómo se llega | Condiciones | Localizador |
|---|---|---|---|
| **Dormir** | Menú de triángulo, opción de dormir (`partnerSetState(3)`) | Solo si el partner tiene sueño (`condition & 1`); en cualquier sitio, sin cama; el menú de triángulo solo se abre andando, sin script, fundido ni cuadro de texto | `game_menu.c: handleGameMenuSelection: 2178-2185`, `game_menu.c: addGameMenu: 1319-1323`, `tamer.c: tamerTickWalkingState: 517-528` |
| | Tras el fundido a negro se cuenta la noche (`sleepRegen`, `handleSleeping`: el reloj salta a la hora de despertar) y **después** sale el menú «Dormir / Guardar y dormir»; lo guardado es ya la mañana | Sin tarjeta no sale el menú | `partner.c: partnerTickSleep: 278-366` (326-340) |
| | «Dormir» cierra; «Guardar y dormir» guarda; cancelar no hace nada | | `main_menu.c: tickMainMenu: 3881-3898` |
| | Al cerrar: fundido de entrada, hora del día, evoluciones especiales del despertar, el reloj sigue | | `partner.c: partnerTickSleep: 346-361` |
| **Máquina de guardado** | Rutina 48 (`call_routine` 0x30) de un script de mapa: se pulsa la acción ante la máquina, sale un sí/no y luego la rutina | Scripts 165 y 178, mapas 205 (`ROOM10`) y 218 (`ROOM08`), los dos la casa del anciano de la ciudad. Sin coste | `script_interp.c: scriptInstruction64to7E: 1826-1830`, `script_interp.c: tickScript: 496-500`, `main_menu.c: openSaveMachine: 5273-5286` |
| | Primero ofrece **registrar el partner para el modo versus** (40 huecos); después «¿Quieres guardar?» | Si el partner es de nivel menor que 3 no se registra (aviso) y pasa a la pregunta de guardar | `main_menu.c: tickMainMenu: 4204-4451`, `main_menu.c: registerBattleData: 5230-5245` |
| **Fin del juego** | Rutina 54 (`call_routine` 0x36) del script 192 (mapas 236 y 237, el final) | Guarda con la ciudad como mapa de vuelta y el estado «juego acabado», y regala un registro de versus fijo en el hueco 39 | `script_interp.c: scriptInstruction64to7E: 1832-1836`, `main_menu.c: gameClearSave: 5318-5329`, `main_menu.c: awardMachinedramonData: 5299-5316` |

No hay más sitios: solo `partner.c` y `script_interp.c` abren estos menús. El reloj del juego está parado mientras
están abiertos (`partner.c: partnerTickSleep: 282`; `script_interp.c: scriptPauseGame: 2736-2738`), y la partida se
escribe con el reloj en marcha para que corra al cargarla (`main_menu.c: writeSavegame: 5080-5083, 5131-5133`).

### 1.3 El guardado en sí

`main_menu.c: tickMainMenu: 3900-4057` (y su copia para la máquina, 4259-4416):

1. Comprueba la tarjeta (`0x29`). Si falta, está cambiada, sin formato o sin el fichero, enseña el error con
   «Se sobrescribirá el fichero / ¿guardar?» (`0x30`) y deja reintentar; **en partida nunca se formatea ni se crea un
   fichero** (4405-4413, 4046-4054).
2. Escribe (`0x2B`): `writeSavegame` llena la copia 0, se calcula la suma, se duplica en la copia 1 y se escribe el
   bloque (`main_menu.c: tickMainMenu: 3955-4011`; `main_menu.c: writeSavegame: 5071-5134`;
   `main_menu.c: createSavegameChecksum: 5004-5014`).
3. Si va bien, el menú se cierra **sin mensaje** (`main_menu.c: tickMainMenu: 3448-3452`). Si falla, error y vuelta al
   menú de dormir o a la pregunta de la máquina (`main_menu.c: setMemoryCardReadError: 4732-4737`).

### 1.4 La portada

| Paso | Qué hace | Localizador |
|---|---|---|
| Menú 0 | NEW GAME, CONTINUE GAME, DELETE GAME, BATTLE MODE; continuar y borrar en gris sin tarjeta | `main_menu.c: tickMainMenu: 3349-3385`, `main_menu.c: renderInitialMenu: 5359-5363` |
| Partida nueva | Tarjeta 1, tarjeta 2 o **sin tarjeta** (aviso: no se podrá guardar). Una tarjeta sin formato se puede formatear aquí | `main_menu.c: tickMainMenu: 3454-3507` |
| | Con 15 bloques usados: error «no hay bloque libre» | `main_menu.c: tickMainMenu: 3509-3520`, `main_menu.c: getCardUsedBlockCount: 4757-4790` |
| | Solo se elige una ranura **vacía** (no se sobrescribe una partida) y se confirma «¿Grabar una nueva aventura?» («Record new adventure?», con el cursor en «Sí») | `main_menu.c: tickMainMenu: 3522-3556` |
| | Se crea el fichero ya, con una partida por defecto en estado «partida nueva» | `main_menu.c: tickMainMenu: 3558-3636`, `main_menu.c: initializeDefaultSavegame: 4911` |
| Continuar | Tarjeta, lista de ranuras (solo las válidas se eligen), «¿Continuar la aventura?», leer | `main_menu.c: tickMainMenu: 3659-3776` |
| Comprobación | Copia 0 si su suma cuadra; si no, la copia 1; si ninguna, «error de datos» y vuelta a la portada | `main_menu.c: tickMainMenu: 3758-3774` |
| Tarjeta quitada | Durante las listas, vuelta a la portada | `main_menu.c: getMenuOnCardChange: 4708-4730` |

**La lista de ranuras** lee solo la cabecera de la tarjeta de cada fichero, no la partida
(`main_menu.c: loadSaveSlotData: 4792-4863`): número de ranura, nombre del domador, **especie** del partner (no su
mote) y una línea de lugar (`main_menu.c: drawSaveSlotText: 3193-3198`). La versión `us` deja el lugar vacío; `jp` y
`eu` escriben el mapa (`main_menu.c: tickMainMenu: 3982-3994`). Una ranura vacía dice «Sin usar»
(`main_menu.c: renderSaveSlotBox: 1750-1775`). No enseña tiempo de juego, fecha ni dinero. Hay 15 ranuras por tarjeta
(`main_menu.c: SAVEGAME_SLOT_INFO: 208`) y dos tarjetas.

### 1.5 Volver al campo al cargar

`main.c: main: 3411-3529`. Tras la portada, según el estado guardado: 0 es la escena de partida nueva (un fichero que
se creó y nunca se guardó en partida, 3439-3458), 1 es una partida normal y 2 una partida acabada, que pone antes un
vídeo (3459-3491). En los dos últimos se crea el domador, el partner con su especie, el mapa guardado con su script de
cabecera (y su música), el reloj, y se colocan domador y partner donde estaban
(`main.c: initializeLoadedMap: 3791-3899`). Si el partner choca con ciertos NPC al aparecer, los dos van a la salida
guardada (3821-3857). Luego fundido desde negro (3494).

### 1.6 Volver a la portada

**No existe.** Ni opción de menú, ni combinación de botones (`main.c: pollInputGame: 4035-4054`,
`main.c: gameLoop: 3933-3962`); la pausa solo congela (`utils2.c: handlePause: 402-432`) y la muerte del partner lleva
a renacer, no a «fin del juego». La única vuelta es tras los créditos: la rutina 53 pone el estado 3, el bucle sale y
vuelve a la portada (`script_interp.c: 516-520`, `main.c: main: 3522-3524`).

## 2. Lo que hay hoy en el motor

- El formato, las ranuras (`slot_01`…`slot_15`, `autosave_01`…), la copia `.bak`, el estado «dañada» de la lista y las
  migraciones: [save.md](save.md), `game/save` (`SaveSlots::list`, `SlotInfo`).
- `GameSession` ([apps/session](../modulos/apps/session.md)) guarda y carga, pero **solo se carga al arrancar**
  (`--load`): `FieldScene` se crea una vez (`apps/game/src/game.cpp: runGame`) y no hay vuelta a la portada ni
  máquina de estados de la app. `GameSession::loadFile` no es transaccional: si una sección falla, el estado queda a
  medias.
- La portada (`content/packs/game/ui/main_menu`) se pinta **encima de una partida ya empezada** (nueva o `--load`): su
  «Jugar» solo la oculta. No hay «Continuar».
- El menú de pausa (`content/packs/game/ui/pause_menu`) no tiene «Dormir»: dormir es un comando de depuración
  (`apps/game/src/care_commands.hpp`, `sleep`). Su «Salir» cierra el programa.
- No hay intérprete de los scripts de historia: las rutinas 48 y 54 no corren en ningún sitio (solo el extractor las
  conoce, `formats/src/script/routine_writes.cpp`).
- Guardar desde una ruta: el comando de depuración `save("slot_NN")` (`apps/game/src/save_command.hpp`) y el par
  `route-save-load` + `route-save-load-check`, dos ejecuciones unidas por `scripts/gate/route_jobs.sh`.
- Parte (b): `StoryState` y su sección `story` (versión 1, disparadores y las 35 variables que guardaba el original;
  `readMissing` empieza de partida nueva y marca criadas las especies de la sección `evolution`). Esta parte no la
  cambia; al recrear la sesión se recrea también su adaptador.

## 3. El diseño

### 3.1 Las piezas y sus dueños

| Pieza | Dónde | Qué hace |
|---|---|---|
| `SaveSummary` (cabecera) | `game/save` | Lo que enseña la lista: domador, especie y mote del partner, mapa. Ver [3.3](#33-las-ranuras) |
| `SaveSlots` | `game/save` (existe) | Lista, carga, carga de la copia, guarda. Sin cambios de comportamiento |
| `AppFlow` | `apps/game` (`app_flow.hpp`, plantilla sobre la partida para poder probarla sin ventana) | La máquina de estados de la app: `Title` → `Playing` → `Title`. Crea y destruye la partida en juego; la anterior se va antes de crear la nueva |
| `PlaySession` | `apps/game` (`play_session.{hpp,cpp}`) | Todo lo que vive lo que dura una partida: `GameSession`, `FieldScene`, la batalla, el menú del partner, el HUD y el panel de ajuste. Se construye entero desde una ranura o desde partida nueva y se destruye entero. La VM de scripts es de la app: sus comandos de partida se encolan en `SessionCommands` y, sin partida, se descartan con un aviso |
| `SaveFlow` | `apps/game` (nuevo, `save_flow.{hpp,cpp}`) | La **ranura de la partida** (la que se eligió al empezar o al continuar, o ninguna) y los guardados del jugador: dormir, máquina, fin del juego. Publica el resultado para la interfaz |
| Documentos | `content/packs/game/ui/` | `main_menu` (la portada, existe), `save_slots` (lista de ranuras, nuevo), `dialog` (sí/no y avisos, nuevo y común), `sleep_menu` (nuevo), `pause_menu` (añade «Dormir» y «Volver a la portada») |
| Textos | `content/packs/game/localization/{en,es}/` | `save.yaml` (amplía), `main_menu.yaml`, `pause_menu.yaml` |
| Rutas | `tests/autotest/`, `apps/game/src/autotest.cpp` | La ruta de un solo proceso ([sección 6](#6-rutas-y-tests)) |

`game/save` sigue sin conocer los sistemas ([regla 4](dependency-rules.md)): el resumen lo rellena la app al guardar.
La interfaz habla con la app como ya lo hace, con `ui.send` y ajustes publicados (`ui.md`, «Comandos y ajustes»).

### 3.2 Los estados de la app

```text
arranque ──► Title ──(Nueva partida / Continuar)──► Playing ──(Volver a la portada)──► Title
   │                                                   ▲
   └── --load, --start, ruta sin main_menu ────────────┘
```

- **Title**: sin partida. La portada se pinta sobre negro, como el original (`ui.md`, tabla del aspecto original). No
  hay `GameSession` ni `FieldScene`.
- **Playing**: una `PlaySession`. Entrar en `Playing` es siempre construir una `PlaySession` nueva; salir es
  destruirla. Así **recargar en mitad de la partida** es salir a `Title` y entrar otra vez: no hace falta un «warp a
  un sitio cualquiera» ni deshacer estado a mano, y lo que haya dejado la partida anterior (scripts en cola, batalla,
  eventos) desaparece con ella.
- **Carga transaccional**: la `PlaySession` nueva se crea y carga la partida **antes** de soltar nada. Si la carga
  falla, se destruye la nueva, se enseña el error y se sigue en `Title`. Esto arregla también que `loadFile` deje
  estado a medias: nadie usa esa sesión.
- `--new-game`, `--load`, `--start` y las rutas sin `main_menu: true` entran directas en `Playing`, sin portada.
  Al entrar desde la portada, el tiempo de la carga no cuenta como tiempo de juego (no hay ráfaga de ticks).

**Volver al campo** al cargar: `FieldScene::create` con el mapa y la posición guardados, como ya hace el arranque con
`--load` (`apps/game/src/game.cpp: runGame`). El partner aparece donde estaba. La recolocación del original cuando el
partner choca con un NPC (`main.c: initializeLoadedMap: 3821-3857`) no se hace en este paquete (pregunta 7).

### 3.3 Las ranuras

- **Cuántas**: las 15 manuales de `save/settings` (como una tarjeta del original). El autoguardado no tiene ranuras
  propias (desde 2026-11-04): escribe la ranura de la partida, como en [save.md](save.md). Una sola carpeta, no dos tarjetas (desviación, [sección 5](#5-diferencias-con-el-original)).
- **La ranura de la partida** (`SaveFlow`): se elige al empezar (ranura vacía o «sin guardar») o al continuar (la
  ranura que se carga). Todo guardado del jugador va a ella, sin preguntar, como en el original. `--load slot_NN` la
  fija a esa ranura; `--load autosave_NN`, a la que diga ese autoguardado antiguo (abajo); una ruta sin carpeta de partidas
  (`SessionDesc::storage` nulo) juega «sin guardar». «Jugar sin guardar» en la lista de ranuras también empieza sin
  carpeta (`PlayStart::withoutSaving`): esa partida no escribe nada, ni guardados ni autoguardados, como avisa su
  pregunta.
- **Autoguardado y ranura**: el autoguardado **escribe la ranura de la partida**, la misma que los guardados del
  jugador (`GameSession::writeRequestedAutosave`), con su escritura atómica y la partida anterior en `.bak`. Guardar y
  autoguardar son una sola partida: no hay dos ficheros que elegir. Sin ranura (una partida nueva fuera del título) va
  a la primera vacía; nunca escribe sobre otra partida; sin vacías se salta y lo dice el aviso (reglas en
  [save.md](save.md#ranuras-autoguardado-y-copias)). La opción «Autoguardado» (Sí/No, en las opciones del título y
  de la partida) lo apaga.
- **Autoguardados antiguos**: los `autosave_NN.yaml` de versiones anteriores apuntan a la ranura manual de su partida
  (`summary.slot`). Siguen en la lista mientras estén en el disco («Autoguardado antiguo N»), se cargan (la partida
  sigue en esa ranura manual si todavía es suya) y se borran, pero nunca se escriben. Borrar la ranura manual borra
  también sus autoguardados antiguos y sus `.bak` (`SaveSlots::erase`).
- **La lista** (`save_slots`, publicada como ajustes `save.slot.<n>.*`):

| Columna | De dónde | Original |
|---|---|---|
| Número de ranura | El nombre del fichero | Igual |
| Nombre del domador | `summary.tamer` | Igual |
| Especie del partner | `summary.species` (un ID; la interfaz pone el nombre en el idioma) | Igual (`us` enseña la especie, no el mote) |
| Lugar | `summary.map` (un ID de mapa, con su nombre traducido) | Como `jp`/`eu`; `us` lo dejaba vacío |
| «Sin usar» | Ranura vacía | Igual |
| «Dañada» / «Hay copia» | `SlotInfo::Damaged`, `hasBackup` | Nuevo (el original no la listaba como válida) |
| Tiempo de juego y fecha de guardado | `play_seconds`, `updated` | **Mejora** documentada, de la cabecera que ya existe |

Los autoguardados antiguos que queden van debajo de las manuales («Autoguardado antiguo N»), solo al continuar y al
borrar (al empezar no se eligen).

**Cabecera nueva** (pregunta 1, CONTRACT): `format_version: 3` añade a la cabecera

```yaml
summary:
  slot: "slot_03"          # la ranura manual de la partida; "" si se juega sin guardar
  tamer: "Juan"
  species: "base:<especie>"  # el ID de la especie, no el nombre
  nickname: "Chispa"
  map: "base:<mapa>"         # el ID del mapa
```

con su paso de migración `2 → 3`, que pone un `summary` vacío (`game/save` no conoce las secciones). La lista
rellena el de una partida así con lo que lee de sus secciones `partner` y `field` (`session::summaryFromSections`,
que la app da a `SaveSlots::setSummaryReader`), y el siguiente guardado lo escribe entero. `label` se queda como está. Los IDs siguen
la regla de la fase 4 (IDs, no índices).

### 3.4 Partida nueva

`Nueva partida` → lista de ranuras con **solo las vacías** elegibles y una entrada final «Jugar sin guardar» →
«¿Grabar una nueva aventura?» (sí/no; empieza en «Sí», como el original, y las demás preguntas en «No») → se crea
la `PlaySession` de partida nueva y **se guarda ya en la ranura**, como el original crea el fichero al empezar. Con las 15 ocupadas: aviso «No hay ranuras libres» y solo «Jugar sin
guardar» o volver. «Jugar sin guardar» avisa de que no se podrá guardar (como «sin tarjeta»).

Sin carpeta de partidas (las rutas de humo de hoy) `Nueva partida` entra directo, sin lista: es el camino «sin
tarjeta». Así `route-loop-keyboard` y las demás siguen igual, con el botón `play`.

### 3.5 Continuar

`Continuar` (en gris si no hay ninguna partida, como sin tarjeta) → lista con las ranuras guardadas elegibles →
«¿Continuar esta aventura?», **con el cursor en «Sí»** (mejora documentada: es la respuesta esperada; el original
pregunta igual) → carga transaccional ([3.2](#32-los-estados-de-la-app)) → `Playing` con fundido desde negro.

**Continuar una ranura carga lo último de esa partida**: la ranura tiene tanto los guardados del jugador como los
autoguardados, así que el «Sí» carga sin más preguntas (`SaveFlow::answer`). La lista empieza con el cursor en la
partida guardada más reciente (`save.slots.newest`, en las dos pieles; mejora documentada). Los autoguardados antiguos
siguen en la lista como filas propias («CONTINUAR ANTIGUO n»; el cuadro de abajo dice «Autoguardado antiguo n»).

Con el aspecto original la lista es la del original (`main_menu.c: renderSelectCardSlot`): una fila por ranura,
«CONTINUAR RANURA n» y luego, si quedan, «CONTINUAR ANTIGUO n», las vacías en gris y sin elegir, ocho a
la vista con flechas si hay más. El juego publica cuántas manuales hay (`save.slots.manual`) y cuántos autoguardados
antiguos (`save.slots.autosave`). Bajo la
lista, un cuadro con el resumen de la fila enfocada (compañero; lugar, tiempo de juego y fecha), que el original no
tiene (mejora documentada). La pregunta sale sobre la lista (`dialog.at_title`).

### 3.5b Borrar

`Borrar partida` (en gris sin partidas, como `Continuar`) → la misma lista con «BORRAR RANURA n» y las ranuras que este
juego puede escribir (no las de un juego más nuevo) → «¿Borrar esta aventura?» **con el cursor en «No»** → la
ranura queda vacía (`SaveSlots::erase`): se borran su fichero, su `.bak` y los autoguardados antiguos de esa partida (y sus
`.bak`), así que no queda copia de una partida que el jugador borró. Si no se puede borrar, aviso «save.erase_failed» y el error al registro. Tras borrar vuelve a la
lista. El original borra el fichero de la tarjeta sin copia (`main_menu.c: tickMainMenu`).

### 3.6 Dormir

`pause_menu` gana **«Dormir»**, activo solo si `Partner::canSleep()` (el original lo pone en gris igual) y el menú
podría abrirse sobre el campo: sin combate ni fundido (`tamer.c: 517-528`; las condiciones de guion y caja de texto
llegarán con el intérprete de historia y las cajas de texto). Al elegirlo, como `partner.c: partnerTickSleep`:

1. Se cierra el menú, el compañero se acerca al jugador y los dos juegan sus animaciones de dormir (`sleep.scene`,
   [módulo game](../modulos/apps/game.md#dormir)); después la pantalla funde a negro: `sleep.fade.out_frames` (40)
   frames del original, con la curva
   escalonada de `fade.c` (el paso es `160 / (frames + 1)`, el mismo cálculo que da el 7 del cambio de mapa). En el
   último frame, `GameSession::fallAsleep` (el reloj salta a la mañana; ya existía). Lo lleva `SleepScreen`
   (`apps/game/src/sleep_screen.cpp`); el reloj espera, el campo sigue sin el jugador y el menú de pausa no se abre
   encima.
2. Si la partida tiene ranura, `sleep_menu`: «Dormir» / «Guardar y dormir». Cancelar no hace nada, como el original.
   Sin ranura no sale nada.
3. «Guardar y dormir» guarda en la ranura (`GameSaves`, `apps/game/src/game_saves.cpp`). Antes mira la ranura
   (`SaveSlots::list`): si ya no tiene esta partida, pregunta o avisa en vez de escribir encima ([4](#4-errores)). Si
   falla, aviso con el error y vuelta al paso 2. Las preguntas empiezan en «No».
4. Fundido de entrada de `sleep.fade.in_frames` (40) frames. En el frame `sleep.fade.control_frame` (35) el jugador
   recupera el control, el reloj sigue y `GameSession::wakeUp` corre las evoluciones especiales del despertar
   (`SessionSystem::afterWakeUp`): el guardado del paso 3 guarda al partner de antes de evolucionar, como el original.
   Los últimos frames del fundido se dibujan con el jugador ya en marcha y el menú de pausa no se abre hasta que acaba
   (`FADE_PROTECTION`).

El reloj está parado mientras el menú está abierto (el bucle ya pausa con documentos modales). Al acabar bien no hay
mensaje, como el original; el aviso de autoguardado, si salta, es aparte.

### 3.7 Máquina de guardado y fin del juego

`SaveMachine` (`apps/game/src/save_machine.cpp`) abre la máquina cuando el guion de la casa del anciano llama a la
rutina 48 (`StorySceneGlue`; el guion sigue al cerrarse) o con la ayuda de depuración `save_machine("")`. Sus menús
son los de `main_menu.c: openSaveMachine` y `tickMainMenu` 0x3C a 0x46, en `SaveMachineFlow`
(`save_machine_flow.{hpp,cpp}`, sin interfaz):

- **0x3C, registro**: la ficha del compañero (nombre, especie, PV, PM, ataque, defensa, técnicas 1 a 3) y «¿Quieres
  registrarlo?». Sí lleva a la lista; no o cancelar, a «¿Quieres guardar?».
- **0x3D, lista**: diez filas que se desplazan por las 39 ranuras del registro (la 40 es el regalo del fin del juego,
  `awardMachinedramonData`); una vacía enseña un punto. Cancelar vuelve a 0x3C.
- **0x3E, confirmar**: «¿Quieres registrar a <nombre>?» y la nota de que se guarda al registrar. Sí registra
  (`registerBattleData`: `BattleRegistry::registerAt`) y guarda; no vuelve a la lista; cancelar, a 0x3C.
- **0x45**: «¿Quieres guardar?»; sí guarda, no cierra; no admite cancelar.
- **0x44**: sin ranura (la partida empezó sin guardar, el «sin tarjeta» del original), el aviso de que no se puede
  registrar; se cierra con cualquier botón o solo tras 0x79 cuadros del original (6,05 s).
- **0x46**: un compañero por debajo de Novato no se registra; el aviso y luego «¿Quieres guardar?».

El guardado es el de dormir ([4](#4-errores)): a la ranura de la partida, con la misma comprobación (otra partida,
dañada, de un juego más nuevo) y sus diálogos en la caja de la pregunta de sobrescribir del original (0x42). Las
comprobaciones de la tarjeta (0x3F a 0x43, «Comprobando la tarjeta», «Guardando») no tienen equivalente: el
guardado de PC no espera a nada y la máquina se cierra al acabar, como el original tras escribir (menú 9), sin aviso.
Un registro cuyo guardado no se escribe se deshace: el original lo guarda dentro del propio fichero
(`SAVE_FILE.saves[0]`). El registro es la sección de guardado `battle_registry` (`apps/session`,
`battle_registry.{hpp,cpp}`); una partida sin ella empieza sin registrados. El campo y el reloj esperan mientras la
máquina está abierta. Las cajas son las del original, donde el original las dibuja (`ui/save_machine`, en los dos
aspectos). El modo versus que lucha contra los registrados es de la fase 5 (pregunta 3). El guardado del fin del
juego (rutina 54, `gameClearSave`, 5b-2) está hecho: marca la partida terminada (`cleared: true` en la sección `field`,
el `MAIN_STATE` 2), la guarda con la ciudad de `getFileCityTopMap` como mapa de vuelta y la posición del jugador, y al
cargarla suena la película ED2 antes del campo (`apps/session/game_clear.hpp`, `SaveMachine::openClearSave`); la
máquina y el menú de dormir vuelven a `MAIN_STATE` 1.

### 3.8 Guardar, volver a la portada y salir del juego

Es una **mejora opcional de PC**, a propósito: el original no tiene forma de volver a la portada salvo acabar el
juego ([1.6](#16-volver-a-la-portada)). No cambia reglas y quien no la use juega como en el original.

`pause_menu` gana **«Guardar»**, el guardado rápido: envía `quick_save` → `PlaySession::quickSave` escribe la partida
en la ranura manual de la aventura (`GameSession::homeSlot`: la que la cargó o la que guardó por última vez) o, en una
partida nueva que aún no ha guardado, en la primera ranura manual libre; sin ranuras libres, un `dialog` lo avisa
(`save.slots.full`) y un fallo de escritura también (`save.failed`). Sirve en cualquier momento en que el menú pueda
abrirse (nunca durante un sueño, un combate o una escena de cuidados, igual que `to_title`), enseña el aviso de
guardado (`ui/save_indicator`) y deja el menú cerrado.

`pause_menu` gana **«Volver a la portada»** (encima de «Salir del juego»), que envía `to_title` → `dialog`: «Se
perderá lo que no hayas guardado. ¿Volver a la portada?» (no sale durante un sueño o un combate) → `AppFlow` destruye
la `PlaySession` y `TitleScreen::show()` vuelve al menú principal, como el original tras el final (`main.c: main`).
No guarda nada: quien quiera conservar la aventura pulsa antes «Guardar», o usa «Salir del juego», que sí pregunta.

`pause_menu` gana también **«Salir del juego»**, que en partida envía `quit` → `dialog`: «¿Guardar la partida antes de
salir del juego?», con el cursor en «Sí» — el recuerdo de guardar antes de cerrar. «Sí» escribe el guardado rápido y,
si lo consigue, `AppFlow` cierra la ventana; si el guardado falla, el aviso aparece y la partida sigue en marcha. «No»
(no, y también Cancel) cierra el diálogo sin salir. Desde la portada, la salida del menú principal sigue cerrando sin
preguntar. La elección de destino —la portada o el cierre del juego— son las dos entradas del menú.

### 3.9 Textos (en/es)

Claves nuevas; los textos son nuestros, no los del disco.

| Clave | en | es |
|---|---|---|
| `main_menu.new_game` | New game | Nueva partida |
| `main_menu.continue` | Continue | Continuar |
| `save.slots.title_new` | Choose a slot for your new adventure | Elige una ranura para tu nueva aventura |
| `save.slots.title_continue` | Choose an adventure to continue | Elige la aventura que quieres continuar |
| `save.slots.slot` | Slot {number} | Ranura {number} |
| `save.slots.empty` | Not used | Sin usar |
| `save.slots.damaged` | Damaged save | Partida dañada |
| `save.slots.newer` | From a newer version | De una versión más nueva |
| `save.slots.autosave` | Old autosave {number} | Autoguardado antiguo {number} |
| `save.slots.no_save` | Play without saving | Jugar sin guardar |
| `save.slots.no_save_warning` | You won't be able to save this game. Start anyway? | No podrás guardar esta partida. ¿Empezar igualmente? |
| `save.slots.full` | There are no free slots. | No hay ranuras libres. |
| `save.confirm_new` | Record new adventure? | ¿Grabar una nueva aventura? |
| `save.confirm_continue` | Continue this adventure? | ¿Continuar esta aventura? |
| `save.sleep` | Go to sleep | Dormir |
| `save.sleep_and_save` | Save and go to sleep | Guardar y dormir |
| `save.ask_save` | Do you want to save? | ¿Quieres guardar? |
| `save.saving` | Saving… | Guardando… |
| `save.failed` | The game could not be saved. | No se ha podido guardar la partida. |
| `save.load_failed` | The game could not be loaded. | No se ha podido cargar la partida. |
| `save.start_failed` | The game could not start. | No se ha podido empezar la partida. |
| `save.damaged_use_backup` | This save is damaged. Load the previous copy? | Esta partida está dañada. ¿Cargar la copia anterior? |
| `save.damaged_no_backup` | This save is damaged and has no copy. | Esta partida está dañada y no tiene copia. |
| `save.too_new` | This save is from a newer version of the game. | Esta partida es de una versión más nueva del juego. |
| `save.home_damaged` | This adventure's save is damaged. Save over it? | La partida de esta aventura está dañada. ¿Guardar encima? |
| `save.home_other` | This slot holds another adventure. Save over it? | Esta ranura tiene otra aventura. ¿Guardar encima? |
| `save.home_newer` | This slot holds a save from a newer version of the game. It cannot be saved over. | Esta ranura tiene una partida de una versión más nueva del juego. No se puede guardar encima. |
| `pause_menu.sleep` | Sleep | Dormir |
| `pause_menu.quick_save` | Save | Guardar |
| `pause_menu.to_title` | Back to title | Volver a la portada |
| `pause_menu.to_title_confirm` | Anything you haven't saved will be lost. Back to the title? | Se perderá lo que no hayas guardado. ¿Volver a la portada? |
| `save.quit_confirm` | Save the game before quitting? | ¿Guardar la partida antes de salir del juego? |
| `common.yes`, `common.no` | Yes, No | Sí, No |

El detalle técnico del error va en inglés al log; la interfaz enseña el texto traducido y, debajo, el nombre del
fichero (`dialog.file`).

## 4. Errores

| Caso | Original | Motor |
|---|---|---|
| Partida dañada (no se lee, cortada, suma o YAML mal) | Copia 1 si la 0 falla; si las dos, «error de datos» y a la portada (`main_menu.c: tickMainMenu: 3758-3774`) | La lista la marca «dañada». Elegirla: si hay `.bak`, «¿cargar la copia anterior?» → `loadBackup`; si no, aviso y se queda en la lista. Nunca se borra ni se sobrescribe sola |
| Una sección no se puede leer o migrar | — | Error de carga, carga transaccional: se sigue en la portada, la ranura no cambia |
| Versión más nueva que el juego | — | La lista la enseña como «de una versión más nueva» (el `problem` de `SlotInfo`), no se puede cargar ni sobrescribir al empezar |
| Versión más antigua | — | Se migra al cargar sin preguntar ([save.md](save.md)); el fichero del disco no cambia hasta el siguiente guardado, que deja la versión vieja en `.bak` |
| Partida sin una sección nueva (p. ej. `story` antes de la parte b) | — | El `readMissing` de su sistema ([save.md](save.md)); para `story`, partida nueva más las especies criadas |
| Todas las ranuras ocupadas al empezar | «No hay bloque libre», o jugar sin tarjeta (`main_menu.c: tickMainMenu: 3509-3520`) | «No hay ranuras libres»: jugar sin guardar o volver. No se ofrece sobrescribir (el original tampoco; para borrar, pregunta 4) |
| Falla la escritura en partida (disco lleno, permisos) | Error y vuelta al menú de dormir o a la pregunta (`main_menu.c: tickMainMenu: 4001-4009`) | Igual: `save.failed` y vuelta a `sleep_menu` o a la pregunta. La escritura atómica deja la partida anterior intacta |
| El fichero de la ranura desapareció durante la partida | «El fichero no existe», no se crea (`main_menu.c: tickMainMenu: 3929-3953`) | Se escribe de nuevo en la ranura (desviación menor: no hay tarjeta que cambiar) |
| La ranura de la partida cambió fuera del juego durante la partida | — (la tarjeta cambiada es el caso de arriba) | Al guardar en partida: dañada, «¿guardar encima?»; con otra partida, «¿guardar encima?»; de un juego más nuevo, aviso y no se escribe. «No» vuelve al menú de dormir sin escribir |

## 5. Diferencias con el original

| Qué | Tipo | Por qué |
|---|---|---|
| Elegir ranura solo al empezar o continuar, guardar sin preguntar dónde | Vanilla | |
| Dormir → «Dormir / Guardar y dormir» tras contar la noche, sin cancelar | Vanilla | |
| Máquina de guardado y fin del juego (diálogo y guardado) | Vanilla | Llegan con el intérprete de historia |
| Partida nueva solo en ranura vacía; «sin guardar» como «sin tarjeta» | Vanilla | |
| Lista: domador, especie, «Sin usar» | Vanilla | |
| Lista: lugar | Vanilla (`jp`/`eu`) | `us` lo dejaba vacío |
| Una carpeta de 15 ranuras, no dos tarjetas | Mejora documentada | No hay tarjetas en un PC; el número sale de `save/settings` |
| Lista: tiempo de juego, fecha, «dañada», «hay copia», autoguardados antiguos aparte, cursor en la más reciente al continuar | Mejora documentada | Datos que el formato ya tiene; el autoguardado es la desviación aprobada de [save.md](save.md) |
| Copia anterior en `.bak` en vez de dos copias en el fichero | Mejora documentada | Ya en [save.md](save.md); cubre el mismo caso |
| Volver a la portada desde la pausa | Mejora documentada | El original no tiene; es interfaz, sin efecto en las reglas |
| Sin formatear tarjetas, sin «quitar la tarjeta» | Sin equivalente | No hay tarjetas |
| Fichero de la ranura borrado fuera del juego: se reescribe | Mejora menor | |
| Al guardar durmiendo se mira antes la ranura de la partida (otra partida, dañada, de un juego más nuevo) | Mejora documentada | El original escribe encima sin mirar; un PC deja tocar la carpeta fuera del juego |
| Dormir: el partner no camina a la cama ni hay animaciones ni sonido del sueño (`partner.c: 281-321`) | Pendiente: VAN-04 | El fundido, el momento del guardado y el del despertar ya son los del original |
| La máquina registra para el modo versus; el modo versus no está | Vanilla; el modo, pendiente (fase 5) | Pregunta 3 |
| La máquina no enseña «Comprobando la tarjeta» ni «Guardando» | Sin equivalente | No hay tarjeta; el guardado de PC es inmediato |
| Borrar partida en la portada; se borran también su `.bak` y sus autoguardados antiguos | Vanilla + mejora documentada | Pregunta 4; el original borra el fichero sin copia |
| Continuar una ranura carga lo último de esa partida: el autoguardado escribe la misma ranura (opción «Autoguardado», Sí/No) | Igual + mejora documentada | El original no autoguarda: cargar la ranura es cargar lo último guardado; aquí también, con los autoguardados dentro |
| Las preguntas de continuar y borrar empiezan en «Sí» y «No» | Mejora documentada | La respuesta esperada y la segura |
| Recolocar al partner si choca con un NPC al cargar | Pendiente | Pregunta 7 |

## 6. Rutas y tests

**La ruta de un solo proceso** sustituye al par `route-save-load` / `route-save-load-check` y deja el comando
`save("slot_NN")` solo para depurar. `route-save-load.yaml` (`main_menu: true`, carpeta de partidas vacía):

1. Portada → `play` → `slot-1` → `yes`. Esperar el mapa inicial.
2. Lo de hoy: `give_item("meat 3")`, ir a `room08`, ir al punto `[-95, -900]`.
3. `clock_speed` hasta que el partner tenga sueño (`pause_menu.sleep_enabled` = sí), `clock_speed("0")`.
4. Pausa → `sleep` → `sleep-and-save`. Esperar `save.last_result` = `saved`; recordar la hora (`remember`).
5. Pausa → `to-title` → sí. Esperar la portada.
6. `continue` → `slot-1` → `yes`. Esperar `room08` cerca de `[-95, -900]`.
7. `expect_setting`: especie y edad del partner, día y la hora de después de dormir, dinero y la carne del inventario;
   `save.slot.0.state` = `saved`, `save.slot.0.species` la del partner. Capturas en cada pantalla nueva.

Corre en `en` y `es`. Cambios en el ejecutor: `shownMenu()` (`apps/game/src/autotest.cpp`) conoce `save_slots`,
`dialog` y `sleep_menu`; nada más, porque todo lo demás se comprueba con ajustes publicados. En el gate,
`scripts/gate/route_jobs.sh` pasa una carpeta de partidas vacía a las rutas que la piden (una lista, como hoy
`follow_up_routes`) y se quitan la segunda ejecución y `route-save-load-check.yaml` (pregunta 8).

**Tests unitarios** (ninguno cuenta para el límite de líneas):

- `game/save`: la cabecera v3 se escribe y se lee igual byte a byte; una partida v2 migra a v3 con el resumen vacío,
  y la lista lo rellena con el lector del juego. `apps/session`: el resumen sacado de las secciones; con secciones
  que faltan, vacío.
- `SaveFlow`: continuar empieza en la partida más reciente y carga la ranura sin más preguntas; un autoguardado
  antiguo sale aparte y se carga (`save_flow_tests.cpp`). `AppFlow`: tras seguir jugando, el autoguardado escribe la
  ranura de la partida y cargarla da el estado de después (`app_flow_tests.cpp`).
- `SaveFlow`: ranura de la partida al empezar, al continuar, desde un autoguardado y sin guardar; guardado al dormir
  con y sin ranura; fallo de escritura (`MemorySaveStorage::failNextWrite`) vuelve al menú; máquina y fin del juego
  con su mapa de vuelta.
- `AppFlow`: Title → Playing → Title → Playing con otra ranura no deja nada de la primera partida (reloj, inventario,
  `story`); una carga que falla deja la app en Title y la ranura sin tocar; partida dañada con y sin copia.
- La lista de ranuras: vacías, guardadas, dañadas, más nuevas, autoguardados antiguos, llena.

## 7. Plan de tramos

Cada tramo cabe en unas 800 líneas cambiadas sin contar tests ni documentos (el contenido `.rml`, `.rcss`, `.lua` y
`.yaml` de la interfaz **sí** cuenta), pasa su gate corto y se sube solo. Del tramo 2 en adelante, después de que la
parte (b) esté en `main`.

| Tramo | Contenido | Termina cuando | Líneas (estim.) |
|---|---|---|---|
| 1 | Este documento | Commit del documento | 0 de código |
| 2 | Cabecera v3 (`summary`, migración 2 → 3), la app la rellena al guardar y al autoguardar, `--list-saves` la enseña | Tests de formato y migración en verde; las partidas de hoy cargan | ~250 |
| 3 | `AppFlow` + `PlaySession`: sacar de `runGame` lo que dura una partida, Title sin partida, carga transaccional, `--load`/`--start`/rutas sin portada como hoy. La portada aún solo tiene «Jugar» (sin ranuras) | Las rutas de humo pasan igual; test de Title → Playing → Title | ~650 |
| 4 | Portada: «Nueva partida» con lista de ranuras vacías y «sin guardar», «Continuar» con lista y errores (dañada, copia, más nueva), documentos `save_slots` y `dialog`, `SaveFlow` con la ranura de la partida, textos en/es | Partida nueva en ranura y continuar a mano y con test; las rutas de humo siguen (sin carpeta = sin lista) | ~750 |
| 5 | En partida: «Dormir» en la pausa, `sleep_menu`, guardar al dormir, `openSaveMachine`/`gameClearSave` (sin llamadas), «Volver a la portada» con su diálogo, textos en/es | Test de `SaveFlow`; dormir y volver a la portada a mano | ~550 |
| 6 | La ruta de un solo proceso, `shownMenu()`, la carpeta vacía en `route_jobs.sh`, fuera el par y `route-save-load-check`; docs (`save.md`, `ui.md`, `prueba-vertical.md`, `gate.md`) | La ruta pasa en en y es en el gate corto | ~200 |

Si el tramo 3 o el 4 pasa de 800 al hacerlo, se parte en el sitio marcado: el 3 en «sacar `PlaySession`» y «`AppFlow`»;
el 4 en «nueva partida» y «continuar».

**Notas para el tramo 5** (de la revisión del 4):

- Antes de «Guardar y dormir», comprobar el estado de la ranura de la partida (`SaveSlots::list`): si ya no está como
  la dejó la partida (borrada fuera del juego, dañada o de un juego más nuevo), avisar en vez de escribir encima.
- Al volver a la portada, `TitleScreen::show()` (no `ui.show` del menú a secas), para que «Continuar» y la lista se
  vuelvan a leer.
- Partir el tramo al diseñarlo para que cada gate quede en unas 800 líneas: el 4 llegó a 960 y fue la última excepción.

**El tramo 5, partido** (aprobado por el coordinador y el gestor el 2026-10-22). Cada parte pasa su gate corto, con su
ruta en en y es (`--routes-extra`), y se sube sola; la 5b va después de la 5a.

| Parte | Contenido | Ruta | Líneas (estim.) |
|---|---|---|---|
| 5a | «Dormir» en la pausa, `SleepScreen` (fundidos, sueño, menú), `sleep_menu`, `GameSaves` con la comprobación de la ranura, textos | `route-sleep-save` | ~520 |
| 5b | «Volver a la portada» con su diálogo y `TitleScreen::show()`, `openSaveMachine`/`gameClearSave` (sin llamadas) | `route-to-title` | ~260 |

Decisiones del diseño (aceptadas):

- **(a)** Si el fichero de la ranura ya no está (borrado fuera del juego), se escribe de nuevo, como dice
  [4](#4-errores): no hay nada que perder. La nota de arriba lo contaba entre los avisos; manda la tabla de errores.
- **(b)** Ranura dañada o con otra partida: pregunta sí/no («¿guardar encima?»). De un juego más nuevo: solo aviso.
- **(c)** «La misma partida» es la misma `created` en la cabecera (la hora en que empezó, al segundo); no cambia el
  formato. **Límite:** dos partidas empezadas en el mismo segundo cuentan como la misma, así que guardar una sobre la
  otra no pregunta. Se acepta por ahora; si la cabecera gana un id de partida, la comprobación pasa a usarlo
  (pendiente en el BACKLOG).

**CAMBIO DE CONTRATO (aditivo)** de la interfaz ([ui.md](ui.md#comandos-y-ajustes)), cada uno en la parte en que
funciona: en la 5a, los comandos `sleep`, `sleep_only` y `sleep_and_save`, los ajustes `pause_menu.sleep_enabled` y
`save.last_result` y el documento `sleep_menu`; en la 5b, `to_title`. Un mod que reemplaza el menú de pausa tiene que
añadir «Dormir» ([ui.md](ui.md#comandos-y-ajustes)).

## 8. Preguntas (decididas)

El coordinador aceptó el 2026-10-22 todas las recomendaciones; la columna «Recomendación» es la decisión. La 1 es un
cambio de contrato del formato de la partida. Las 3, 4 y 7 van a la fase 5 (la fase 4 es solo vanilla).

| # | Pregunta | Tipo | Recomendación |
|---|---|---|---|
| 1 | ¿Cabecera v3 con `summary` (`slot`, `tamer`, `species`, `nickname`, `map`) o sacar el resumen de las secciones al listar? | CONTRACT (formato) | v3 en la cabecera: la lista solo lee la cabecera (como el original), `game/save` no conoce los sistemas y la migración rellena lo que pueda |
| 2 | ¿El autoguardado guarda la ranura manual de su partida (`summary.slot`) y continuar desde él sigue en esa ranura? | CONTRACT (formato) | Sí: si no, continuar un autoguardado dejaría la partida sin sitio donde dormir y guardar. Desde 2026-11-04 el autoguardado escribe la propia ranura; esto queda para los autoguardados antiguos |
| 3 | ¿El registro para el modo versus de la máquina (y el regalo del fin del juego) entra en este paquete? | FIDELITY | El registro sí (2026-10-30: la máquina registra y lo guarda, sección `battle_registry`); el modo versus y el regalo del fin del juego, en la fase 5 |
| 4 | ¿«Borrar partida» de la portada del original? | FIDELITY | Hecho (3.5b): borra a la copia `.bak` |
| 5 | ¿«Volver a la portada» siempre, o detrás de un ajuste por ser una mejora? | FIDELITY | Siempre: es interfaz como «Salir del juego», no cambia reglas; queda en la tabla de desviaciones |
| 6 | Una partida recién creada y nunca guardada en el juego: el original repite la escena de partida nueva al cargarla; ¿aquí? | minor | Cargar en el mapa de inicio con el estado de partida nueva; repetir la escena cuando la escena de inicio sea del intérprete de historia |
| 7 | ¿Recolocar al partner en la salida guardada si aparece encima de un NPC (`main.c: initializeLoadedMap: 3821-3857`)? | FIDELITY | A la lista de fase 5: necesita los NPC del campo, que aún no existen |
| 8 | ¿La lista «rutas que piden carpeta vacía» en `route_jobs.sh` cuenta como infraestructura nueva del gate (regla 5 de 4a)? | minor | No: sustituye a `follow_up_routes` y quita código; el manager lo confirma |
| 9 | ¿El comando `save("slot_NN")` se queda? | minor | Sí, para depurar, hasta la decisión de fase 5 sobre la consola en las versiones de jugador |
| 10 | ¿Se enseña tiempo de juego y fecha en la lista (mejora)? | minor | Sí: ya están en la cabecera y ayudan a elegir; documentado como mejora |
| 11 | ¿Quién llama a `openSaveMachine`/`gameClearSave` antes del intérprete de historia (p. ej. una tabla de máquinas extraída por `dwx`)? | CONTRACT (extracción) | Nadie en 4a: una tabla nueva cambiaría la extracción; esperar al intérprete (R4.1) |
