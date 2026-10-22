# Warps de script

Estado: **parte (a) hecha** (p4-scripted-warps, 2026-10-21); **parte (b) en su rama** (p4-scripted-warps-b,
2026-10-22), pendiente de revisión.

El original hace muchos cambios de mapa desde los scripts de eventos (`warp_to`, `teleport`), no desde la tabla de
warps del mapa. `dwx` los extrae todos, con lo que hace falta para llegar a cada uno, a `field/script_warps` del
paquete base, y el campo los toma. Todo sale de **una sola representación** de los scripts (`formats/script`), la
misma que usarán la conversión a Lua y el intérprete de historia.

## Comportamiento del original (referencia, sin copiar código)

| Hecho | Localizador |
|---|---|
| `warp_to` (0x4B): pantalla, salida y sección de llegada (`0xFF`: ninguna); nada de lo que sigue corre | `script_interp.c: scriptInstruction46to58`, `map.c: scriptTickChangeMap` |
| `teleport` (0x58): pantalla y salida de dos variables (247 y 248 en el disco); sin sección de llegada | `script_interp.c: scriptInstruction46to58` |
| Entrar en un mapa corre la sección de `MAPHEAD` con el número del mapa, que carga su script con `set_script` (en el disco, siempre con el mapa igual a la sección); luego corre la sección `0xFE` del script | `map.c: changeMap`, `script_interp.c: runMapHeadScript` |
| Casillas 51–79: la sección del mismo número al pisarlas; 80–109: al pulsar confirmar; 110–119: la tabla; hablar con un NPC corre la sección con su id de script | `tamer.c: checkMapInteraction` |
| Una partida nueva corre la sección 0xCC de `MAPHEAD` (`twna01`) con todos los disparadores apagados | `main.c: newGameScene`, `script_interp.c: initializeScripts` |
| `IF`: entradas que se acumulan de izquierda a derecha (sustituye, `and`, `or`) y un salto si el resultado es verdadero o falso | `script_interp.c: scriptIfInstruction` |
| Una sección se busca por su número en la tabla del script y gana la primera entrada: otra con el mismo número no corre nunca (los scripts 4 y 52 del disco repiten alguna) | `script_interp.c: getScriptSection` |
| `give_item` deja su resultado en el disparador 0; `compare_date`, en su disparador | `script_interp.c: scriptInstruction28to3F`, `script_value.c: scriptCompareDate` |
| `call_routine` y `call_digimon_routine` corren código del juego que puede poner o quitar disparadores (el menú de tirar objetos pone el 3, las tiendas, los trueques, el torneo, la evolución y la muerte otros) | `script_interp.c: scriptInstruction64to7E, tickScript`, `partner.c: callDigimonRoutine`, `script_common.c: openDiscardItem` |
| Otros cambios de mapa desde el código: la muerte (a 0xCD o 0xDA) y las secciones de `MAPHEAD` que no cargan un script (transporte, objetos) | `map.c: changeToDeathMap`, `dooa.c`, `murd.c` (`setDeathMap`) |

## La representación (formats/script)

1. **`ScriptListing`** con operandos y condiciones con nombre. Dos constructores, `readScriptListing` (YAML) y
   `listingOf` (desensamblado), que un test de disco compara en todos los scripts.
2. **`FlowRole::MapChange`**: `warp_to` y `teleport` acaban el flujo, como `end_section`.
3. **`walkSection`** (`guarded_walk.hpp`) recorre el árbol estructurado de una sección (`structureScript`) y da
   cada orden que pide un filtro con su camino: las decisiones (`BranchTerm` de un `IF`, con sus pruebas y el
   resultado que deben dar; `ChoiceTerm` de un `SWITCH` o de un `SET_SELECTION`, con `selection` si lo elige el
   jugador), los disparadores que pone y los que conoce al llegar. Sigue llamadas, colas y `jump_to_file`. Un `IF`
   cuyas pruebas son de disparadores conocidos toma su rama sin dejar decisión; las ramas que vuelven a juntarse con
   lo mismo se juntan. Un bucle o una máquina de estados dejan el camino `unstructured`.
4. **Límites del recorrido**, para que acabe en todos los scripts del disco (`WalkOptions`):
   - `maximumPaths` (`DefaultMaximumPaths`, 64): si una sentencia deja más caminos, siguen como uno solo,
     `unstructured`, con las decisiones y los efectos con que empiezan todos y los disparadores que todos conocen
     igual (lo que conoce un camino es un mapa de disparador a puesto, quitado o **desconocido**; un disparador en el
     que no coinciden queda desconocido aunque los demás se den por apagados). Es seguro: toda orden a la que llegaba
     un camino sigue alcanzable (ante un disparador desconocido se recorren las dos ramas), una decisión que tomaron
     todos se conserva, y un cambio de mapa después de la mezcla nunca es cierto (`unstructured`), así que el campo
     no lo toma en vainilla. Un test sintético con 128 caminos lo comprueba contra la enumeración completa.
   - `maximumRoutineWalks` (`DefaultMaximumRoutineWalks`, 16): una rutina que el recorrido ya ha seguido tantas
     veces se **resume**. Cada camino que llega pasa por el recorrido de la rutina desde lo que ese camino conoce, que
     se hace una vez para todos los que conocen lo mismo, y se pone detrás de sus decisiones y efectos. Es lo que daría
     seguirla entera, porque el recorrido de una rutina solo depende del camino por lo que conoce; solo pueden cambiar
     las mezclas de `maximumPaths`, que dependen de cuántos caminos pasan juntos, y por qué rutinas se están
     recorriendo, porque la comprobación de recursión corta la llamada a una de ellas. Por eso cada resumen se guarda
     con las rutinas en que intentó entrar (también las de los resúmenes que usó) y cuáles de ellas se estaban
     recorriendo, y solo se usa otra vez donde se están recorriendo las mismas de ellas: así los cortes son los
     mismos. Una rutina que se llama a sí misma, como hace un bucle, se corta igual en cada uso y su resumen sirve.
     Sin los resúmenes, las cadenas de rutinas que se llaman desde varias ramas se recorren una vez por camino: el
     script 165 no acaba en minutos. Sin guardar los resúmenes con cortes pasa lo mismo: una versión anterior no los
     guardaba y la extracción no acababa. Un test sintético con una cadena de rutinas que se llaman a sí mismas lo
     comprueba por el número de recorridos de resumen.
   - **Recursión cortada**: la llamada a una rutina que el recorrido ya está recorriendo no se sigue. El camino que
     la hace no se pierde: sigue después de la llamada con un término **`RecursionTerm`** (la rutina, con su script y
     su offset) que dice que lo que decide esa llamada no se sabe, y con lo que conocía de los disparadores
     olvidado (la rutina pudo cambiarlos). Los efectos del camino (`sets_flags`) no llevan lo que ponga o quite esa
     llamada. Nunca se da por falso ni se descarta: un cambio de mapa después de una
     rutina que se llama a sí misma (aunque no sea su última orden) sale con ese término. Tests sintéticos: un `IF`
     cuya rama vuelve a llamar a la rutina deja el término (no una rama sola), y un cambio de mapa después de una
     autollamada que no es la última orden se encuentra.
   - **Código del juego que escribe disparadores** (`ForgetTerm`, **CAMBIO DE CONTRATO aditivo**): después de una
     orden que corre código del juego que puede poner o quitar disparadores, el camino sigue con un término
     **`ForgetTerm`** (la orden, con su script y su offset, y los disparadores que puede escribir) y sin saber esos
     disparadores: una prueba posterior de uno de ellos recorre las dos ramas. `give_item` olvida el 0;
     `compare_date`, su disparador; `call_routine` y `call_digimon_routine`, lo que diga la tabla
     `routineWrites()` (`routine_writes.hpp`), leída del código del juego con un localizador por entrada. Una rutina
     que no está en la tabla no escribe ninguno, y no deja término: así se separa lo que escribe de lo que no. Los
     disparadores del fotograma (37–40 y 44) nunca se saben y no cuentan. Sin esto el recorrido daba por decidida la
     prueba del 3 que sigue al menú de tirar objetos (script 206) y perdía dos cambios de mapa.
     - Con disparadores listados: `call_routine` 0x00, 0x01, 0x08 y 0x0E (menús de objetos y cartas) el 3;
       0x12 los del torneo (37–40); 0x16 las medallas (0x16C–0x17E, `script_common.c: unlockMedal`); 0x20 el 49
       (`shop/script_menu.c: newGameStateMachine`); 0x21 el 37, el 38 y el 39.
     - **Todos** (el código no acota lo que escribe), con su motivo: `call_routine` 0x02 (el trueque pone el
       disparador del objeto bajo el cursor, `setMojyaItemTradedTrigger`), 0x06 (el torneo: el disparador 15 más la
       copa de una variable, y sus combates llegan a la muerte, la evolución y otros scripts), 0x30 y 0x36 (pueden
       cargar una partida, `main_menu.c: loadSavegame`); `call_digimon_routine` 2 (evolución) y 4 (renacer), que
       devuelven el partner a toda su máquina de estados, y 3, 7 y 9 (muerte), que llegan al cambio a mapa de muerte y
       a otros scripts. Son 9.
     - Sobreaproximaciones (no escriben disparadores, pero el recorrido sigue tras ellas como si volvieran):
       `call_routine` 0x0A, el transporte (`shop/script_menu.c: tickBirdraTransport`): elegir un destino es un
       traspaso a la sección 0x4E3 de MAPHEAD, y el script que llamó no sigue; cancelar vuelve al script
       (`script_menu.c:1004-1006`), y ese camino sí es real. Los destinos salen de una tabla del ejecutable, no del
       recorrido de MAPHEAD (ver [El transporte](#el-transporte)). Y 0x35 termina el script
       (`script_interp.c: scriptInstruction64to7E`). Lo que el recorrido encuentra después de ellas en el script que
       llama corre en el original solo si se canceló el transporte.
     - Cómo se leyó: lo que cada rutina corre al empezar y mientras el script la espera, con todo lo que llama o deja
       como retrollamada, y los estados del partner y del domador a los que pasa; la búsqueda para en los estados que
       devuelven el control al juego (domador 0, 1 y 6; partner 1, 11 y 12). Un test comprueba que la tabla solo
       tiene rutinas que existen, sin repetir.
   - `WalkMonitor` (opcional): cuenta mezclas, llamadas resumidas, recorridos de resumen y recursiones cortadas
     (`recursionCuts`), y para el recorrido en una hora límite (para las pruebas que lo miden).
   - `GuardedInstruction::state` lista solo los disparadores que el camino conoce.
   - **Prueba en el disco** (opcional, fuera de la puerta: `DWE_WALK_PROOF`, `formats/tests/script_walk_proof_disc_tests.cpp`):
     script a script, comprueba que todo `warp_to` y `teleport` sale en el resultado o no lo alcanza el flujo de
     ninguna sección (salvo `StaticUnreachable`), hace la comprobación de mezclas y compara el recorrido con
     resúmenes con el completo, con 10 minutos por script. Si el completo no acaba, el script se prueba con el
     **respaldo**: los recorridos que resumen después de 256 y de 1024 recorridos deben dar lo mismo que el de 16,
     condiciones incluidas; la salida dice «proven by fallback: N (ids)». `DWE_WALK_PROOF` admite rangos
     (`0-59,60-119`) para correrla por trozos, cada uno en su contenedor con su límite de tiempo.
     `StaticUnreachable` son solo los 6 cambios de mapa del script 124 (offsets 0x5C0, 0x5D0 y 0x5D4 bajo el `IF` de
     0x59C; 0x620, 0x630 y 0x634 bajo el de 0x5FC): están en el lado falso de una prueba del disparador 221, que el
     script pone en 0x56C en todo camino; la prueba lo comprueba con el recorrido: todo camino a los cambios de mapa del lado
     verdadero de esas pruebas (0x5E4, 0x5F4, 0x5F8, 0x644, 0x654 y 0x658) sabe el 221 puesto (el recorrido no pasa
     un `IF` por el filtro, porque lo recorre como sentencia, así que se mira lo que corre después). Los del script 206 (0x836 y 0x1396) no son excepción: con `ForgetTerm` salen. En la misma fuente, un test que sí es de la puerta (si hay disco) comprueba que el
     recorrido que hace la extracción acaba en cada script antes de `ExtractionBudget` (2 minutos, en el build con
     sanitizers), y que ningún cambio de mapa pasa de `MaximumGuardTerms` (32) términos; en el disco el máximo es
     16 (script 174). La prueba opcional también recorre con un `maximumPaths` grande (`LargeMaximumPaths`, 4096)
     los scripts en que hay mezclas y comprueba que ese recorrido acaba sin mezclar, que la mezcla no junta cambios
     de mapa distintos y que no inventa condiciones; un script que no se pueda comparar va en `MergeExceptions` con
     su motivo (ninguno). Los que con 4096 caminos aún mezclan son una nota de precisión, no un fallo: van en
     `MergeStillMerges` con lo medido, se siguen comparando (mismos cambios de mapa, ninguna condición inventada) y
     la prueba falla si uno de la lista ya no mezcla. Son 9 (2026-10-22): 165 y 211 (150 mezclas, 130 con 4096;
     probados por respaldo), y 168 y 214 (69 y 11), 176 (4 y 2) y 199–202 (90 y 16), los cuatro grupos probados
     por el recorrido completo. Ninguno de los cambios de mapa de estos scripts en `field/script_warps` es
     `unstructured`, así que ninguno queda después de una mezcla (medido el 2026-10-22; no es un CHECK).
5. **`ScriptListing::runnableSections`** da las secciones que el juego puede correr (la primera entrada de cada
   número); `walkSection` toma la primera y la extracción y la prueba solo recorren esas. Un test sintético con dos
   entradas de la sección 51 lo comprueba.
6. **`findMapChanges`** filtra los `MapChange`; **`findScriptWarps`** recorre cada sección de `MAPHEAD`, mira qué
   script carga y con qué decisiones, y junta los cambios de mapa de cada sección de ese script, con qué los
   dispara (`warpViaOf`). **`findNewGameTriggers`** recorre la sección 0xCC y la `0xFE` del script que carga, con
   todo apagado, y da los disparadores puestos al tomar el control.

## Formato: `field/script_warps` (salida de dwx, paquete base)

Esquema `content/schemas/script_warps.schema.json`. Un registro por camino a un cambio de mapa:

```yaml
warps:
- map: room08          # el mapa (la sección de MAPHEAD que carga el script)
  via: step            # step (51–79) | confirm (80–109) | enter (su sección de MAPHEAD) | talk (la sección de un
                       # NPC del mapa) | other (otra sección)
  section: 51
  to: {map: twna01, exit: 2}   # o {screen: N, exit: N} si ningún mapa tiene ese número
  teleport: {pstat: 247}       # en vez de `to`: la variable con la pantalla; sin destino estático
  arrival_section: 5           # solo si no es 0xFF
  script_guard: {...}          # lo que pide MAPHEAD para cargar el script; sin él, nada
  guard:                       # lo que pide la sección; sin él, nada
    unstructured: true         # solo si el camino pasa por un bucle o una máquina de estados
    terms:
    - if: [{test: trigger, trigger: 203, clear: true}]   # las entradas del IF, como en el listado
      holds: true
    - switch: 1                # o selection: 0 (lo elige el jugador)
  transport: {entry: 0, cost: 1000, hand_off: 1251}   # solo en una elección del menú del transporte
    - recursion: {script: 63, offset: 288}   # una llamada que el recorrido cortó (RecursionTerm); offset opcional
    - forget: {script: 206, offset: 1694, triggers: [3]}   # código del juego que puede escribir esos disparadores
                                           # (ForgetTerm); sin triggers: cualquiera
  sets_flags: [{trigger: 83, set: true}]   # set/unset_trigger del camino, en orden; sin lo que ponga una
                                           # recursión cortada (`recursion`), que no se recorre
  source: {script: 178, section: 51, offset: 296}
```

`data/new_game_story` (esquema `new_game_story`): `map`, `script`, `triggers_set` y `agreed: false` si los caminos de
la partida nueva no acaban con los mismos disparadores (entonces `triggers_set` son los que comparten), y
`triggers_unknown`: los que el código del juego puede haber puesto antes de la sección de partida nueva y el recorrido
no modela, que el campo no da ni por puestos ni por apagados. Son los de partner criado de los dos partners con que
puede empezar la partida (0x203 y 0x211: `main.c: main` cría uno según el pstat 0xFE con
`script_common.c: setDigimonRaised`, que pone 0x200 más su id). En el disco americano: `twna01`, script 164, ninguno
puesto; desconocidos el 0x203 y el 0x211.

`opening` (opcional): el cambio de mapa que hace la partida nueva antes de que el jugador se mueva. La partida nueva
entra en el primer mapa por la salida 9 (`map.c: initializeMap` pone `CURRENT_EXIT`; `NewGameExit`), y las casillas de
pisar corren su sección en cada tick que el jugador está encima (`tamer.c: checkMapInteraction`), así que si la llegada
cae en una la corre enseguida. `dwx` lee la casilla bajo esa llegada en el `.MAP` del primer mapa (como
`map_collision.c: getTileTrigger`; `formats::map::collisionUnderSpawn`) y recorre su sección con el estado de partida
nueva (`findNewGameOpening`); si hace un único cambio de mapa cierto, lo guarda: `exit`, `section`, `to` (mapa o
pantalla y salida) y `sets`, los disparadores que deja puestos. Solo es cierto si todos los caminos de la partida nueva
acaban con los mismos disparadores (`agreed`), si da lo mismo con cada combinación de los desconocidos (encendidos o
apagados; como mucho `MaximumOpeningUnknowns`, 8) y si el camino no apaga ninguno de los que pone la partida nueva, que
el campo dejaría puesto; si no, no hay `opening` y `dwx` lo explica en una nota. En el disco americano la llegada cae en
la casilla 54 de `twna01`: con el 55 apagado salta al código de la 66 (la entrada «To Opening» de un menú de depuración,
que el camino vivo se salta) y, con el 75 y el 54 apagados, pone 75, 54 y 55 y va a `topn01` salida 9, la apertura.
Desde ahí, en vainilla no se vuelve a pisar `twna01` con esos disparadores apagados: la 54 es la puerta de `room08` y la
66 no hace nada.

```yaml
opening: {exit: 9, section: 54, to: {map: topn01, exit: 9}, sets: [54, 55, 75]}
```

En el disco americano salen 1870 registros (versión 5 del extractor): 1602 al pisar, 168 al confirmar, 100 al hablar
con un NPC y ninguno de otras secciones; 36 son elecciones del menú del transporte; 201 sin condición; ninguno a una pantalla sin mapa; 37 con un término `forget` y ninguno con `recursion`. Frente a la
extracción sin `ForgetTerm` ni `runnableSections`: 14 más del script 206 (offsets 0x836 y 0x1396, en `frzl15` y
`room20`) y 2 menos, los de la sección 81 repetida del script 52 (`yaka11b`), que no corre nunca. 26 secciones de
`MAPHEAD` no cargan script (partida nueva, muerte, transporte...) y quedan fuera; la del transporte (0x4E3) solo
enseña una ventana de texto y sigue en 0x4E5, cuyo `teleport` lleva al mapa del pstat 247, que pone la rutina del
transporte desde la tabla del ejecutable. La versión 4 tenía 1834: las 36 elecciones del transporte son nuevas (12
al confirmar, 24 al hablar) y los 76 de «NPC u otras secciones» son ahora `talk`. 3 mapas no se pueden leer, y sus
secciones de NPC no se distinguen.

## En el motor (parte a)

- `game/field` carga `field/script_warps` (`loadScriptWarps`; vacío si no hay) y el estado de partida nueva
  (`assets::loadNewGameStory`); `apps/game` le pasa un oráculo de disparadores (`TriggerState`) con ese estado.
- **La apertura que el motor se salta.** El motor aún no juega la apertura: `field/warps.yaml` empieza en `twna01`
  salida 1, no en la 9. `newGameTriggers(historia, mapa, salida)` da entonces por puestos también los `sets` de
  `opening` cuando el campo empieza en otro sitio que su llegada (el primer mapa en la salida de `opening`), que es
  el estado que deja la apertura. Sin eso, cruzar la 54 o la 66 de `twna01` llevaba a `topn01`, un estado al que el
  original nunca llega. Pendiente: empezar en la salida 9, correr la casilla en la que se llega (el original lo hace
  cada tick; el campo solo al entrar en una casilla) y jugar la apertura.
- Las caminatas del autotest (`walk_to`) y el clic para moverse evitan las mismas casillas: las que cambian de mapa
  ahora (`Field::warpsOn`: salidas de la tabla con destino y casillas de pisar cuyo warp de script se toma), salvo
  la de destino.
- Al entrar en una casilla 51–79, o al pulsar confirmar en una 80–109, el campo mira los warps de ese mapa, vía y
  sección, en orden, y toma **el primero que se cumple con certeza** (`evaluate` da `Truth::True`): toda decisión es
  un `IF` de disparadores que el oráculo sabe, el camino no es `unstructured` y el warp tiene mapa de destino. Los
  que no se pueden decidir (otras pruebas, elecciones del jugador o de una variable, caminos no estructurados) no se
  toman y se registran una vez por mapa y casilla (`ScriptWarpUndecided`).
- **Política de lo desconocido** (`UnknownPolicy`, `script_warps.hpp`): decide qué hace el campo con las dos cosas
  que la extracción dice no saber, y solo con ellas. `decide(warp, oráculo, política)` da la verdad y lo que la
  política dejó pasar.
  - Un término `recursion` (la recursión que el recorrido cortó).
  - Una prueba de un disparador que el código del juego del camino puede haber escrito (`forget`).
  - Un `IF` cuyas pruebas son **todas** de disparadores y alguno no lo sabe el oráculo: en vainilla, uno que no está
    entre los que comparten los caminos de la partida nueva (`agreed: false`; `newGameTriggers` lo da desconocido,
    y apagado solo si los caminos coinciden).

  Con **`Allow`** el término se cumple: el `IF` pasa sea cual sea el resultado que pide (las dos polaridades), y el
  warp se toma y se registra **una vez por warp** (`ScriptWarpAllowed`: mapa, casilla, origen y qué se dejó pasar:
  el sitio del corte, el disparador o el disparador y el sitio del código que pudo escribirlo). Con **`Deny`** el término es desconocido y el warp no se toma.
  `VanillaUnknownPolicy` es `Allow`: el original no pierde una salida por lo que la extracción no puede saber.
  `Deny` existe solo para tests y modo estricto; **no es una opción del usuario**. Todo lo demás desconocido (una
  prueba que no es de disparadores, aunque vaya con uno desconocido; una elección; un camino `unstructured`; un
  warp sin destino) sigue sin tomarse y registrado, con cualquier política. Un término que falla decide el warp
  aunque haya otros desconocidos.
  - **Después de una recursión** el camino no sabe ningún disparador (la rutina pudo cambiar cualquiera): toda prueba
    de disparadores posterior es desconocida, aunque el estado de partida nueva la sepa, y con `Allow` pasa en las dos
    polaridades. Una recursión en `script_guard` (el camino de `MAPHEAD`, que corre antes) hace lo mismo con las
    pruebas de `guard`.
  - **Después de código del juego** (`forget`, `GuardForget`) el camino no sabe los disparadores que lista (todos si
    no lista ninguno): sus pruebas posteriores son desconocidas y con `Allow` pasan, con el sitio en el registro; las
    de otros disparadores las sigue decidiendo el estado. También pasa de `script_guard` a `guard`.
  - **Cuál de la casilla** (`warpOnCell`): con `Allow`, dos caminos que se separan en un disparador desconocido
    pueden cumplirse los dos; el original corre la sección y solo toma uno. Si exactamente uno se cumple sin la
    política (no necesita nada desconocido), el campo toma ese; si no, el primero en el orden del fichero, y si van a
    sitios distintos la casilla es **ambigua** y se registra una vez por mapa y casilla (`ScriptWarpAmbiguous`). El
    test de alcance usa la misma función, así que no cuenta más aristas que las que el campo toma, y fija cuántas
    casillas y secciones son ambiguas con los disparadores de partida nueva, listadas por mapa, casilla y destinos.
- Un `teleport` no tiene destino estático: está en el registro (sin `to`), el campo nunca lo toma y el grafo de
  mapas (test de alcance) no le da arista. En los scripts de mapa del disco no hay ninguno.
- `sets_flags` aún no se aplica, ni hay estado de historia guardado; tampoco se disparan `enter` ni `other`.
- La puerta de `room08` sale de la extracción (disparador 203 apagado → `twna01` salida 2); la entrada escrita a
  mano desaparece.
- **Alcance** (test de disco y cifras en la [ficha del campo](../reference/campo.md#alcance-de-los-mapas)): en
  vainilla, con los disparadores de partida nueva y los de la apertura que se salta, 5 mapas, todos con vuelta; con cualquier warp, 236 y 233 con
  vuelta, con 8 excepciones documentadas. El test lista por nombre (mapa, casilla y origen), con su número, los warps
  con un término de recursión, los que en vainilla se toman por la política (`Allow`) y los alcanzables en vainilla
  que no se toman por ser `unstructured`, y los que en vainilla se toman solo porque el código del juego pudo
  escribir un disparador que prueban (con su sitio). En el disco americano (2026-10-22) son 0, 0 y 0: ningún warp de
  vainilla depende de lo desconocido; el test los fija y falla si cambian.

**CAMBIO DE CONTRATO (modders):** `field/warps.yaml` solo lleva `start`; `scripted_warps` sale del fichero y del
esquema `field_warps`, y un paquete que lo traiga deja de validar. Un mod que cambie warps de script sustituye
`field/script_warps` entero. `dw1fe` no lee ni escribe ninguno de los dos: sin impacto; el YAML de los scripts no
cambia ([registro](herramientas-externas.md#registro-de-cambios-de-contrato)).

**CAMBIO DE CONTRATO aditivo:** un término de `guard` y `script_guard` puede ser `forget` (`script`, `offset`
opcional, `triggers` opcional). Quien lea `field/script_warps` y no lo conozca debe tratarlo como desconocido, igual
que `recursion`.

## Parte (b)

Rama p4-scripted-warps-b. Lo que añade sobre la parte (a):

- **Condiciones de objetos, dinero y partner** (pregunta 6). Las entradas de un IF que miran cuántos hay de un
  objeto, el dinero, una estadística o las condiciones del partner se leen con sus operandos y se deciden con
  `GuardFacts`, que el campo pregunta en vivo (la sesión da los lectores: disparadores, bolsa, dinero, estadísticas,
  condiciones). Comparan como el original: un byte del número de objetos, dinero como 32 bits sin signo, la
  estadística 9 con signo y las demás de 16 bits sin signo, una condición por su máscara y negación. Un lector que
  falta deja la prueba desconocida, y la política de vainilla solo deja pasar lo desconocido en pruebas de
  disparador. Azar, elecciones y conversaciones siguen sin decidirse (cerradas y registradas).
- **`sets_flags` y la sección `story` de la partida** (pregunta 8). Al tomar un warp, el campo escribe sus
  `sets_flags` por su escritor de disparadores y vuelve a decidir las casillas del mapa (`refreshScriptWarps`), así
  que solo el campo decide qué warps están activos. `game/story` guarda 800 disparadores y 256 variables; la
  partida los escribe en una sección nueva y aditiva, `story` versión 1, con test de migración: una partida sin ella
  carga la historia como la deja una partida nueva y lo registra.
- **El disparador del partner.** El original pone, antes de la sección de partida nueva, 0x200 más la especie del
  primer partner (0x203 o 0x211, `main.c`). `dwx` no cambia: `data/new_game_story` los sigue dando como
  desconocidos (`triggers_unknown`), y es el motor el que pone el del partner con el que empieza la partida. El pin
  de 5 mapas no cambia.
- **Hablar con un NPC** (`via: talk`). `dwx` lee los NPC de cada mapa, y un cambio de mapa de una sección que corre
  un NPC del mapa es `talk` en vez de `other`. El campo no los toma (no hay intérprete de historia), pero entran en
  el grafo de mapas.

### El transporte

La llamada `call_routine` 0x0A (scripts 167 y 209) abre el menú del transporte. Sus destinos son una tabla del
ejecutable (`formats/exe`, `transportTargets`: 6 entradas de 8 bytes, mapa, salida, disparador y precio; solo la
disposición americana). Elegir un destino es un traspaso: la rutina pone el mapa y la salida en los pstat 247 y 248,
resta el precio y sigue en la sección 0x4E3 de MAPHEAD, que enseña una ventana de texto y pasa a 0x4E5, cuyo
`teleport` cambia de mapa. Cancelar vuelve al script (`script_menu.c:1004-1006`).

`dwx` escribe cada entrada como un cambio de mapa en el sitio de la llamada, con el destino de la tabla y como
condición el disparador de la entrada, dinero al menos su precio y la elección del menú, más el registro
`transport: {entry, cost, hand_off}` (hand_off 0x4E3). El motor no resta el precio: lo hará el menú del
transporte cuando exista.

### Exploración libre

Ajuste `free_exploration` (en el menú de pausa) y `--explore`, apagada por defecto. Vainilla nunca la enseña.
Con ella, un warp de script de una casilla lleva a cualquiera de los sitios a los que llevan los warps de esa
casilla, sea cual sea su condición (`explorationWarps`: el primer warp de cada mapa y salida distintos, solo a
mapas que el paquete tiene):

- un solo sitio: se toma, y si no es el de vainilla se registra la puerta que abre;
- varios: el campo se para y el selector `destination_choice` los enseña de 8 en 8, empezando por el de la historia;
  se elige con teclado, ratón o mando, en en/es; cancelar es quedarse;
- los `sets_flags` del warp elegido se aplican también.

En el disco americano hay casillas con 1, 2, 3, 6, 12, 13 y 24 sitios (las de 24 son las fases de las ciudades).
El grafo de la exploración libre, hecho con `explorationWarps`, llega a los mismos mapas que el grafo abierto de
los tests de alcance. Rutas: `route-free-exploration` (teclado), `-mouse` y `-gamepad`.

### Lo que la parte (b) no abre

**La parte (b) sola no abre el mundo vainilla más allá de los 5 mapas** de partida nueva (`mayo00`, `room08`,
`trai00`, `twna01`, `twnb01`): el disparador 203 lo pone una conversación (`mayo00`, script 105, sección 6). El
test de alcance aplica hasta el punto fijo los `sets_flags` de los warps que vainilla puede tomar y no se escribe
ninguno (los 54, 55 y 75 vienen del warp de la apertura, que la partida salta); el alcance queda igual.

## Después (pregunta 7)

- Las secciones globales de `MAPHEAD` (muerte, transporte, objetos) y sus rutinas, como el `teleport` de la variable
  247 en 0x6062.
- Tomar los warps al hablar con un NPC (`yaka26`), al entrar en un mapa y del menú del transporte: los dará el intérprete de historia, que ejecuta
  el script convertido. `field/script_warps` quedará entonces como grafo de mapas (exploración libre, rutas de clic,
  tests de alcance), no como lo que cambia de mapa.
