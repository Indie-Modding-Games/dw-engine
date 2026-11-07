# Ficha de referencia: scripts de eventos

Cómo ejecutaba el juego original sus scripts de eventos (`MAPHEAD.SCN` y `DG.SCN`), según el decomp: el modelo de
ejecución, el estado que leen y escriben, por dónde entran, qué hace cada una de las 78 órdenes que usa el disco
americano, cómo se evalúa un `IF`, el protocolo del combate y las rutinas del motor (`call_routine`). Es la referencia de
comportamiento de R4.1: `dwx` convierte estos scripts a Lua legible y el motor solo ejecuta Lua. El formato binario y el
listado YAML están en [Scripts de eventos](../formats/scripts.md); aquí solo se describe qué **hace** cada cosa.

Los nombres de orden son los del listado (`show_textbox`, `jump_to`...). "Variable" es una `pstat` (un byte de
`ScriptState.pstats`) y "disparador" un bit de `ScriptState.triggers`. Las cuentas de uso son del disco americano.

## Qué hace

Los scripts mueven la historia: diálogos, menús de tiendas, combates con NPC, escenas con cámara y movimiento, cambios
de mapa, premios y castigos. Cada mapa tiene un script en `DG.SCN` y una sección en `MAPHEAD.SCN` que lo carga. Un
script es una lista de **secciones**, puntos de entrada que elige el motor: al entrar al mapa, al hablar con un NPC, al
pisar o pulsar sobre una casilla, al morir el partner... Una sección corre hasta su `end_section`; mientras, el resto del
juego se congela (si la sección lo pide) y el jugador solo responde a cajas de texto y menús.

## Reglas

### Modelo de ejecución

- **Un solo script a la vez** (comprobado, `script_interp.c: callScriptSection`). Hay un único intérprete: un puntero
  de instrucción, una orden "en espera" y una pila de llamadas. `callScriptSection` no comprueba si ya corre otro:
  sobrescribe al que hubiera. Por eso cada llamador del motor mira antes que el intérprete esté libre; una petición con
  otro script en marcha **se pierde**, no se encola (comprobado, `tamer.c: checkMapInteraction`, `partner_impl.c`,
  `tournament.c`, `item.c`). La única excepción es el final de la pesca, que llama sin mirar (comprobado,
  `fish.c: tamerTickFishing`).
- **Arranque:** `callScriptSection(script, sección, esEvento)` solo prepara: carga el fichero, apunta a la sección,
  pone el modo de cierre de texto a 0, el dueño del diálogo al domador, libera los 22 huecos de movimiento, guarda la
  variable 0 (la velocidad del reloj) para restaurarla al final y apunta como rival por defecto la entidad con la que se
  habló. **No** vacía la pila de llamadas (comprobado). La primera orden corre en el siguiente fotograma, salvo con
  `runMapHeadScript`, que hace un primer paso en el acto (comprobado, `runMapHeadScript`).
- **Un fotograma** (`tickScript`, comprobado): si no hay script, nada. Si lo hay, avanza las cajas de texto y los 22
  huecos de movimiento, el cambio de mapa si hay uno en curso, y comprueba la orden en espera. Si sigue esperando, no
  ejecuta nada más. Si no, ejecuta órdenes **seguidas en el mismo fotograma** hasta que una cede el fotograma.
- **Ceder el fotograma.** Cada orden acaba de tres maneras (comprobado, `tickScript` y sus `scriptInstruction*`):
  sigue con la siguiente en el mismo fotograma; cede el fotograma (y, si deja una orden en espera, el script se queda
  ahí hasta que la espera acabe); o empieza un cambio de mapa. No hay corrutina: el estado entero vive en globales.
  - **Bloquean** (esperan algo): `show_textbox` (hasta que se pasa la página), `set_selection` (hasta elegir o
    cancelar), `wait_for_entity` (hasta que acaban los movimientos), `delay` (N fotogramas), `start_battle` (todo el
    combate; tras ganar, el regreso de la cámara) y las rutinas de `call_routine` con menú o escena (comprobado).
  - **Ceden un solo fotograma** sin esperar nada: `fadeout_hud`, `move_boulder`, las rutinas 16, 19, 20, 21 y 55,
    y los opcodes `FC` y `FD` (comprobado).
  - **No ceden:** todas las demás, incluidas las de movimiento y cámara, que solo encargan el movimiento (comprobado).
- **Congelar el juego** no ocurre al empezar, sino en la primera orden que llama a `scriptPauseGame`: casi todas las
  de entidades, cámara, warps y daño, `set_textbox_size`, y `set_dialog_owner` cuando cambia el dueño (comprobado).
  La primera vez por script congela al domador y al partner, suelta la cámara del jugador, pone la variable 0 a 3
  (reloj parado), para el tiempo del juego y limpia el área de texto. Si la sección es una charla con un NPC y el
  disparador 44 está quitado, el NPC y el domador se giran para mirarse. Cada llamada quita el disparador 44
  (comprobado, `scriptPauseGame`). Una sección que nunca congela (una casilla que solo pone disparadores) corre con el
  jugador moviéndose.
- **Fin.** `end_section` y `end_script` hacen lo mismo: desapilan marcos hasta vaciar la pila (abajo) y, con la pila
  vacía, el script acaba (comprobado, `returnFromScriptFile`). Al acabar se cierran las cajas de texto, la variable 0
  vuelve a su valor guardado y, si se congeló, todo se descongela: control del domador y el partner, cámara y reloj
  (comprobado, `tickScript`). Una caja con animación de cierre mantiene el intérprete ocupado hasta que se cierra, así
  que no puede empezar otro script aunque el jugador ya se mueva (comprobado, `closeAllTextboxes`).
- **Un opcode desconocido** (fuera de `10`–`3F`, `46`–`58`, `5A`–`5F`, `64`–`7E`, `FB`–`FF`) aborta el script sin
  vaciar la pila (comprobado). En el disco no hay ninguno alcanzable.

### La pila de llamadas

Ocho marcos, sin comprobar desbordamiento (un noveno escribe fuera; comprobado, `pushScriptStack`). Cada marco guarda
un puntero, un script, un tipo y un argumento. La pila **no se vacía** entre scripts: los marcos que queden se
desapilan en el siguiente fin (comprobado).

| Tipo | Lo apila | Al desapilarlo en un fin |
|---|---|---|
| 1 | `jump_and_link`, `jump_to_file_and_link` (la vuelta) | se descarta y sigue desapilando |
| 3 | `set_script` | lee el fondo del mapa (`readMapTFS`, `setupMap`) y corre la sección `0xFE` del script de mapa actual; si no la tiene, sigue desapilando |
| 4 | `warp_to`, `teleport`, `section_on_exit`, la huida de un combate | cede un fotograma y corre su sección del script de mapa actual; con `0xFF`, el final de `MAPHEAD` (acaba) |

- `jump_return` desapila el marco de arriba **sea del tipo que sea** y vuelve a su puntero (comprobado). Con la pila
  vacía o sobre un marco 3 o 4 salta a basura (comprobado; en el disco no pasa en el flujo que se alcanza).
- Por eso un `end_section` dentro de una subrutina de `jump_and_link` acaba **todo** el script, no la subrutina
  (comprobado). En el disco, 136 `jump_and_link` y solo 14 `jump_return`: la mayoría de subrutinas acaban el script.
- El marco de vuelta guarda el script de mapa cargado, no el que corre: un `jump_and_link` hecho desde código de
  `MAPHEAD` vuelve al puntero correcto pero con el fichero de mapa como base (comprobado; no ocurre en el disco).
- `jump_to_file` no apila nada: cambia de fichero y sección (comprobado).

### Movimientos con guion

- 22 huecos, que se avanzan cada fotograma dentro de `tickScript` y se vacían al empezar cada script (comprobado,
  `tickScriptedMovements`). Hueco 0 el domador, 1 el partner, 2–9 los NPC, 10 la cámara, 11 las puertas y objetos 3D,
  12–21 los objetos del mapa (`move_object_to`).
- Las órdenes de mover, girar, caminar y cámara **no esperan**: escriben el hueco y siguen. Una orden nueva sobre un
  hueco ocupado lo sobrescribe, sin cola (comprobado). Para esperar está `wait_for_entity`.
- `wait_for_entity` espera según su operando (comprobado, `scriptInstruction46to58`, `tickScript`):

  | Operando | Espera a |
  |---|---|
  | `0xFF` | todos los huecos libres y la marca de sincronía puesta |
  | `tamer`, `partner`, id de NPC | el hueco de esa entidad; un NPC que no está cuenta como `0xFF` |
  | `0xC8` | la cámara (hueco 10) |
  | `0xCA`–`0xD4` | los huecos 11–21 |
  | `0xC9` | la marca de sincronía (la ponen las rutinas de `call_digimon_routine` al acabar) |
  | `0xD5` | quita la marca de sincronía y espera a que vuelva |
  | `0xD6` | que acabe una carga asíncrona (rutinas 35, 39) |

### Estado

Lo que guarda `ScriptState` y lo que el motor reserva (comprobado, `script.h`, `readPStat`, `getTriggerOffset`).

- **Disparadores:** 800 bits. No se comprueban los límites: un número ≥ 800 escribe en las variables (comprobado).
  Los que el motor lee o escribe:

  | Disparador | Significado |
  |---|---|
  | 0 | el último `give_item` falló (se pone al fallar, se quita al acertar) |
  | 1 | combate sin huida; los NPC cercanos siempre se unen; al ganar no se espera a la cámara |
  | 2 | combate sin domador |
  | 3 | modo tienda: la rutina 8 abre el reciclaje, la 14 vende cartas; también lo usa la rutina 0 |
  | 4 | con el 3 quitado, la rutina 14 compra cartas sueltas en vez de un sobre |
  | 5–13, 14 | intercambios de `species_bn` (rutina 2); el 14 cuando están los nueve |
  | 15–36 | copa de torneo ganada (15 + id de copa) |
  | 37, 38, 39, 40 | inscrito en torneo; aviso pendiente antes y después de las 20:00; nivel demasiado alto |
  | 41 | no cargar la librería de tiendas en la próxima carga de mapa (lo pone la sección de muerte) |
  | 44 | no girar al NPC y al domador al congelar; cada congelación lo quita |
  | 45, 46 | cañas de pescar |
  | 47, 48 | iconos de la ficha del jugador |
  | 49 | las selecciones no se pueden cancelar (nombres de la partida nueva) |
  | 50 | juego terminado una vez (gramola, medallas) |
  | 200 + Digimon | Digimon reclutado en la ciudad; cuenta para la prosperidad |
  | 350 | Toy Town; el 148 y el 341 son la niebla |
  | 364–382 | medallas |
  | 384 + objeto | objeto a la venta en las tiendas |
  | 512 + Digimon | Digimon criado alguna vez |
  | 640 | umbral de energía alcanzado |

- **Variables:** 256 bytes sin signo. Las reserva el motor:

  | Variable | Significado |
  |---|---|
  | 0 | velocidad del reloj; 3 = parado. Se fuerza a 3 al congelar y se restaura al acabar; la rutina 25 cambia el valor que se restaurará; al guardar se pone 0 |
  | 1 | prosperidad, recalculada de los disparadores 200 + Digimon |
  | 2, 3, 4 | torneo: día (bit 7 = sin aviso), copa (`0xFF` ninguna), tipo del partner inscrito |
  | 5 | la especie que era el partner antes de volverse `species_p` |
  | 6 | nombre del mapa en el próximo warp con guion: 0 según el tipo de sección, 1 siempre, 2 nunca; se vuelve a 0 |
  | 28–31 | contadores de días: +1 cada día salvo si valen `0xFF` |
  | 121, 122 | curling: resultado y dificultad |
  | 200 | pilas de objetos perdidas en la última derrota |
  | 243, 244 | byte alto y bajo del acumulador de tienda (rutinas 30, 31, 56); al olvidar técnicas, la técnica olvidada y las que faltan |
  | 245 | modo de música: 0 día/noche, 1 siempre de día, 2–10 una variante fija |
  | 246 | parámetro de entrenamiento (suposición: sin confirmar su uso) |
  | 247, 248 | burbuja de humor de `species_ai` y argumentos de la rutina 21; destino del transporte de `species_e` |
  | 249 | borrador de menús: carta sacada, pista de la gramola, tipo de menú |
  | 250–253 | combate con guion (abajo) |
  | 254 | argumento general de las rutinas: tienda (el NPC que la atiende), tipo de entrenamiento, cofre, objeto a quitar, opciones de nombre, especie de la evolución, elección de huevo de la partida nueva (0 `species_a`, otro `species_an`) |
  | 255 | resultado: del combate (−1, 0, 1), del torneo (rondas ganadas), 0 al morir por enfermedad o vejez |

- **Cartas:** 66 cuentas de 4 bits; `give_card` no pasa de 9 y `take_card` no baja de 0 (comprobado).
- **Dinero:** `add_money` no pasa de 999 999; `reduce_money` no baja de 0 (comprobado).
- **Méritos:** `set_merit` y `add_merit` no pasan de 9999 (comprobado). `reduce_merit` escribe `MERIT = -valor` y
  lo detiene en 0: deja los méritos en 0 sea cual sea la cantidad (`script_interp.c:1173`). Ningún guion del disco la
  usa; el motor resta la cantidad y se detiene en 0, como dice su nombre, para los guiones de mods.
- **Contadores:** combates empezados y huidas, +1 por `start_battle` y por huida, con tope 9999; los de torneos los
  lleva la rutina 6 (comprobado).
- **Fecha:** año, día, hora y minuto; un año son 30 días (comprobado).
- **Qué se guarda en la partida** (comprobado, `main_menu.c: writeSavegame`): solo los primeros 0x17C bytes de
  `ScriptState`: el inventario diario y de reciclaje, las cartas, los 800 disparadores y las variables **0–34**. Las
  variables 35–255 y la pila **no se guardan**: son de usar y tirar. Al cargar, el mapa guardado corre su cadena de
  `MAPHEAD` como al entrar.

### Puntos de entrada

| Cuándo | Script y sección | Tipo de sección |
|---|---|---|
| Al entrar en un mapa (cualquier cambio de mapa, carga de partida) | `MAPHEAD`, sección = id del mapa | evento |
| Partida nueva | `MAPHEAD` `0xCC` (carga el script 164 y el mapa 204) | evento |
| Hablar con un NPC | script del mapa, sección = id de script del NPC | evento |
| Casilla 51–79 | script del mapa, sección = valor de la casilla, **al pisarla** | casilla |
| Casilla 80–109 | igual, **al pulsar confirmar** sobre ella | casilla |
| Casilla 120 con el partner con ganas de ir al baño | `MAPHEAD` `0x4E2` | casilla |
| Muerte o vida perdida | `MAPHEAD` `0x4DE` | casilla |
| Usar el objeto 22 | `MAPHEAD` `0x4DD` | casilla |
| Aviso de torneo / torneo perdido | `MAPHEAD` `0x4D9` / `0x4DA` | evento |
| Fin de la pesca de `species_n` | `MAPHEAD` `0x4E4` (salta al script 7, sección `0x52`) | casilla |
| Transporte de `species_e` | `MAPHEAD` `0x4E3`, cambiando el puntero del script en curso | — |

- **La cadena de carga de mapa** (comprobado, `map.c: changeMap`, `MAPHEAD.SCN`): `runMapHeadScript(mapa)` corre la
  sección del mapa en `MAPHEAD`. Esta hace `set_script` (script y mapa), que carga el mapa en el acto y apila un marco
  3; luego sigue con lo suyo (en los datos: modo de música, `set_bgm`, `if`). Al acabar, el marco 3 prepara el fondo y
  corre la sección `0xFE` del script del mapa hasta su primera espera. Todo ocurre en la misma llamada, bajo el fundido
  a negro; si `0xFE` espera (texto, `delay`), sigue durante el fundido de entrada.
- **Warp andando** (casillas 110–119): sin script; fundido de 20 fotogramas, la cadena de carga y fundido de entrada
  (comprobado, `tamer.c: tamerTickChangeMap`).
- **Hablar** (comprobado, `checkMapInteraction`): el domador choca con un NPC y, o el NPC tiene `autotalk` 1, o se acaba
  de pulsar confirmar; y no corre otro script. También cuando un NPC con `autotalk` choca con el domador quieto.
- **Casillas** (comprobado): solo con el domador en su estado normal y sobre la casilla que pisa. Las de pisar
  **vuelven a dispararse cada fotograma** mientras el domador sigue encima y no corre otro script: la sección debe
  quitarse de en medio (mover al domador, poner un disparador) o se repite.
- **Muerte** (`0x4DE`, comprobado): por derrota en combate (abajo), por enfermedad (lleva 12 o más de enfermedad; vida
  −1 y variable 255 = 0, `partner_impl.c`) o por vejez (`tickDeathCondition`; variable 255 = 0). La sección empieza
  quitando el disparador 1 y poniendo el 41.
- **Torneo** (comprobado, `updateTournamentRegistration`, cada fotograma con el intérprete libre): inscrito, con el
  partner del tipo inscrito y desde las 12:00: si el día no es el inscrito o pasan de las 23:01, `0x4DA` y se borra la
  inscripción; si no, `0x4D9` una vez antes de las 20:00 y otra después.
- Las secciones `0x4D3`–`0x4D5`, `0x4DB`, `0x4DC`, `0x4E5` de `MAPHEAD` no tienen llamador en el motor
  (suposición: son textos que leen las rutinas, o se llega con `jump_to_file`).

### `IF`

Una lista de entradas que se evalúan **todas**, de izquierda a derecha, sin cortocircuito (comprobado,
`scriptIfInstruction`). El resultado empieza en 0 (falso).

- **Combinación** (byte de código): bit 7 → `resultado = resultado AND valor`; si no, bit 6 → `OR`; si no,
  `resultado = valor`. Se mira el bit 7 primero (comprobado).
- **Prueba** (`código & 0x38`), comprobado en `scriptIfInstruction` y `script_value.c`:

  | Prueba | Operandos | Verdadero si |
  |---|---|---|
  | `00` disparador | disparador | bits bajos 0: está puesto; otro valor: está quitado |
  | `08` variable | variable, valor | `variable OP valor`, OP = bits bajos |
  | `10` saltar si cierto | destino | salta si el resultado es 1; no combina |
  | `18` saltar si falso | destino | salta si el resultado es 0; no combina |
  | `20`+0 estadística | stat, OP, valor u16 | `stat OP valor` sin signo (16 bits); la felicidad (9) con signo |
  | `20`+1 cartas | carta, OP, valor | cuántas tiene `OP valor` |
  | `20`+2 técnica | técnica, negar | 0: el partner la sabe; otro: no la sabe |
  | `20`+3 condición | máscara, negar | 0: tiene algún bit de la máscara; otro: ninguno |
  | `20`+4 objetos | objeto, OP, valor | cuántos tiene (la primera pila) `OP valor` |
  | `20`+5 dinero | OP, valor s32 | `dinero OP valor`, sin signo |

- **Comparaciones** (OP): 0 `==`, 1 `!=`, 2 `>=`, 3 `<=`, 4 `>`, 5 `<`. Siempre **valor del juego OP constante del
  script** (comprobado, `scriptCompareValue`). 6 y 7 dan basura.
- Un salto a mitad de lista deja el resto sin evaluar; si no salta, la evaluación sigue con las entradas siguientes.
  La lista acaba con un byte `19`; después sigue la instrucción siguiente.
- `28`, `30`, `38` y las extendidas 6 y 7 no leen operandos ni dan valor: dejan el del paso anterior (basura en la
  primera). El disco no las usa (comprobado: el desensamblador las marca `unknown` y hay 0).
- Un `IF` sin entradas de salto no hace nada. El formato admite varias entradas de salto en la misma lista (varios
  destinos), y el análisis de control las trata como una cadena de ramas; el disco no tiene ninguna: cada `IF` acaba
  en una sola entrada de salto (comprobado, test lento).

### `SWITCH` y `SET_SELECTION`

- **`switch`**: salta al caso que dice la variable; un valor ≥ número de casos va al **último** (comprobado,
  `scriptInstruction10to27`). Con 0 casos leería basura (el disco no lo hace). Solo 6 usos en el disco.
- **`set_selection`**: muestra las opciones en la caja del dueño del diálogo y espera (comprobado,
  `scriptShowSelection`, `tickSelectionDialogue`). Arriba y abajo dan la vuelta; el cursor empieza en la primera.
  - **Confirmar** salta al destino de la opción. La elección **no se guarda** en ninguna variable: solo la registra el
    salto (comprobado).
  - **Cancelar** cierra la página y sigue **justo después** del bloque de la selección (la marca `12 00` si la hay,
    que no hace nada). Con el disparador 49 puesto no se puede cancelar (zumbido). En los menús del motor, cancelar
    elige la última opción (comprobado, `tickScriptDialogueBox`).
  - En el disco, tras casi cada selección hay un `jump_to` que lleva la cancelación a una de las opciones.

### Texto

- `set_dialog_owner` o `set_textbox_size` **crean** la caja; `show_textbox` escribe una página y espera (comprobado).
- La página siguiente se compone mientras se muestra la actual: si la siguiente orden es otro `show_textbox`, la página
  no final se suelta en cuanto se ve y el script corre hasta el siguiente `show_textbox`, que compone y espera. La caja
  **se queda abierta** tras la última página hasta `fadeout_hud`, otro dueño o el fin del script (comprobado,
  `script_textbox.c: showTextbox`, `tickConfirmDialogue`).
- Cómo se pasa la página lo dice `set_textbox_mode` (0 por sección): 0 esperar a confirmar (soltando antes el botón),
  1 en cuanto se ha escrito, 2 cuando acaba una cuenta de fotogramas que no se rearma por página. Otro valor deja la caja
  sin cerrar (bloqueo) (comprobado).
- La primera fila es el nombre del que habla, en un color: el domador, el partner (su nombre), el tipo de Digimon del
  NPC, o un nombre especial (`0xC8`–`0xCD`); `0xFF` sin nombre. Tras `set_textbox_size` no hay nombre (comprobado).
- Los códigos de control del texto solo insertan nombres, números, colores y tabuladores; **ninguno espera**
  (comprobado). Las referencias a variables (número, Digimon, técnica, objeto, música, torneo) se resuelven al componer
  la página.

### Combate (`start_battle`)

- **Rival** (comprobado, `scriptInstruction64to7E`, `btl.c: handleBattleStart`): si la variable 250 vale 1, combate
  predefinido: las variables 251–253 tienen hasta tres ids de script de enemigos (`0xFF` vacío), que el intérprete
  reescribe en su sitio con índices de entidad; los demás NPC visibles huyen. Si no, el rival es el NPC con el que se
  habló al empezar la sección, y hasta dos NPC cercanos pueden unirse (tabla de huida por tipos, línea de visión,
  objetos que cambian la tirada; el disparador 1 los obliga). El operando de la orden se lee y se ignora.
- Para la música, suma un combate empezado y corre **todo el combate dentro de la orden**; pasan siempre 20 minutos
  de juego (comprobado, `tamer.c: startBattle`).
- **Resultado** en la variable 255, con signo:
  - **−1, derrota:** felicidad −30, disciplina −20; se pierde el 30 % (redondeo abajo) de las pilas ocupadas, al azar,
    que pasan al reciclaje, y la cuenta queda en la variable 200; vida −1 y, si llega a 0, la vida restante a 0. El
    puntero salta a `MAPHEAD 0x4DE` **sin apilar**: el resto del script se pierde y sus marcos se desapilan al acabar
    `0x4DE` (comprobado, `handleItemLoss`).
  - **0, huida:** felicidad −10, disciplina −6, cansancio +2; suma una huida; warp con guion a la pantalla y salida
    anteriores, sin nombre, con un marco 4 vacío. El resto del script no corre (comprobado).
  - **1, victoria:** vuelve la música del mapa, felicidad +2. Con el disparador 1, sigue en el acto tras la orden;
    sin él, la cámara vuelve al domador y la orden espera a que llegue (comprobado).
- Con la vida restante a 0, la muerte por vejez dispara `0x4DE` después. La rutina 47 olvida técnicas
  (abajo); que `0x4DE` la llame solo con 0 vidas es suposición.

### Las órdenes

Las 78 que usa el disco, por grupos. "Espera" dice si cede el fotograma; "congela" si llama a `scriptPauseGame`.
Todo comprobado en `script_interp.c` y los ficheros que se citan, salvo lo marcado.

#### Flujo

| Orden | Usos | Qué hace |
|---|---|---|
| `jump_to` | 3252 | salta a un destino del mismo fichero |
| `if` | 2985 | ver [`IF`](#if) |
| `end_section`, `end_script` | 2811, 222 | fin: desapila hasta vaciar la pila (ver [la pila](#la-pila-de-llamadas)) |
| `set_selection` | 330 | espera; ver [`SET_SELECTION`](#switch-y-set_selection) |
| `jump_and_link` | 136 | apila la vuelta (tipo 1) y salta |
| `jump_to_file` | 135 | salta a una sección de otro script, sin apilar; si la sección no existe, corre basura |
| `jump_return` | 14 | vuelve al marco de arriba |
| `switch` | 6 | ver [`SWITCH`](#switch-y-set_selection) |
| `set_script` | 255 | fija el script y la pantalla actuales, para la música, carga el mapa en el acto y apila un marco 3; sigue en el mismo fotograma |
| `section_on_exit` | 4 | apila un marco 4 con una sección del script de mapa, que correrá al acabar el actual; además restaura ya la variable 0 |
| `data` | 240 | tabla que leen los torneos; si se ejecutara no haría nada y sus bytes correrían como órdenes (no ocurre: siempre va tras un fin o un salto) |

`jump_to_file_and_link` existe pero el disco no la usa.

#### Texto y diálogo

| Orden | Usos | Qué hace |
|---|---|---|
| `show_textbox` | 4327 | escribe una página y **espera** (ver [Texto](#texto)) |
| `set_dialog_owner` | 3607 | si cambia el dueño (o la caja está cerrada): **congela** (y al NPC dueño) y crea la caja 0, que sale de la posición del dueño o del punto de `set_textbox_origin`; mismo dueño con la caja abierta: nada |
| `set_textbox_size` | 22 | **congela** y crea una caja centrada de N columnas y filas, sin nombre; confirmar pasa página y cierra tras la última; cancelar compone todas las páginas que quedan y cierra |
| `set_textbox_mode` | 33 | modo de paso de página (0, 1, 2) y fotogramas del modo 2 |
| `set_textbox_origin` | 19 | punto del mundo del que saldrá la próxima caja con dueño; se usa una vez |
| `fadeout_hud` | 560 | cierra la caja de interfaz N (0 = diálogo) y **cede un fotograma**; no espera a la animación |

#### Disparadores, variables y fecha

| Orden | Usos | Qué hace |
|---|---|---|
| `set_trigger`, `unset_trigger` | 1146, 227 | pone o quita un bit, sin límites |
| `set_pstat` | 1801 | escribe el valor |
| `add_to_pstat` | 272 | suma con tope 255 (sin dar la vuelta) |
| `copy_pstat` | 2 | copia una variable en otra |
| `store_random` | 466 | un número al azar de 0 a `max`, ambos incluidos |
| `store_digimon_type` | 39 | el Digimon del partner |
| `store_digimon_value` | 3 | el byte de tipo (suposición: vacuna/datos/virus) del Digimon cuyo id hay en una variable |
| `store_date` | 110 | año, día, hora y minuto en cuatro variables seguidas |
| `advance_to_date_at` | 17 | pone el reloj en la fecha de cuatro variables; si es posterior, antes simula las horas saltadas en el partner (hambre, vida, enfermedad...); si es anterior, solo la pone; si cambia el día, corre el cambio de día |
| `add_minutes_to_date_at` | 17 | suma minutos a la fecha de cuatro variables (no al reloj); por encima de 255 años queda en un valor raro (25 años, 14 días, 23:59) |

No las usa el disco: `reduce_pstat` (resta con suelo 0), `store_map_id`, `compare_date` (compara una fecha de
variables con una literal y deja el resultado en un disparador), `set_merit`, `add_merit`, `reduce_merit` y la `39`
(que **resta** minutos).

#### Objetos, dinero, técnicas, cartas y estadísticas

| Orden | Usos | Qué hace |
|---|---|---|
| `give_item` | 255 | da objetos; pone el disparador 0 si falla (pila a 99, bolsa llena) y lo quita si no. Ver [inventario](inventory.md) |
| `remove_item` | 63 | quita de **cada** pila del objeto; el 255 no hace nada |
| `set_inventory_size` | 11 | fija los huecos usables, sin tope ni limpiar los de fuera |
| `add_money`, `reduce_money` | 54, 57 | tope 999 999, suelo 0 |
| `learn_move` | 118 | el partner aprende la técnica; dos pares de técnicas se aprenden juntas |
| `give_card`, `take_card` | 67, 5 | +1 con tope 9, −1 con suelo 0 |
| `set_stat`, `add_stat`, `reduce_stat` | 71, 292, 54 | ver abajo |
| `remove_condition` | 8 | quita bits de la condición del partner (sueño, cansancio, hambre, baño, infelicidad, herida, enfermedad); no toca sus temporizadores |
| `deal_damage` | 32 | **congela**; quita al partner el porcentaje de su HP máximo que dice una variable (el operando es la variable, no el número), sin bajar de 1 HP; muestra el número |

Estadísticas de `set_stat`, `add_stat` y `reduce_stat` (y de la prueba de `IF`), comprobado en
`script_value.c: getStatsPointer`:

| Id | Estadística | Tope |
|---|---|---|
| 0–3 | ataque, defensa, velocidad, inteligencia | 999 |
| 4, 5 | HP y MP máximos | 9999 |
| 6, 7 | HP y MP actuales | el máximo actual |
| 8, 9 | cansancio, felicidad (con signo) | 100 |
| 10–13 | disciplina, energía, virus, vida restante | 999 |
| 14 | méritos | 9999 |
| 15, 16 | combates empezados, huidas | 9999 |
| 17–19 | torneos ganados, victorias en torneo, torneos perdidos | 999 |
| 20 | peso | 99 |
| 21 | nivel del domador | 10 |
| 22 | vidas del partner | lee fuera de la tabla (sin tope fiable) |

- `set_stat` pone `min(valor, tope)`; `add_stat` suma en 16 bits con signo y aplica el tope; ninguno tiene suelo.
  `reduce_stat` no aplica tope y tiene suelo 0 (−100 la felicidad). Después de cualquiera de los tres, la energía se
  recorta al tope de la especie y se quita el hambre si la energía llega al umbral (comprobado,
  `scriptUpdateEnergyBoundaries`). Cambiar el HP máximo no toca el actual.

#### Digimon y entidades

Las entidades son `tamer`, `partner` o el id de script de un NPC; un NPC que no está hace que la orden no haga nada
(comprobado, `scriptIdToEntityId`), salvo donde se dice.

| Orden | Usos | Espera | Qué hace |
|---|---|---|---|
| `load_digimon` | 502 | no | carga el modelo en el primero de 8 huecos; con los 8 llenos no hace nada |
| `set_digimon` | 756 | no, congela | aparece el NPC del hueco 0–7 del mapa si el Digimon coincide con el de la tabla del mapa (si no, nada), con su posición, giro y animación, y fija su `autotalk` |
| `unload_entity` | 119 | no, congela | quita la entidad; a los NPC además los libera. Un NPC que no está: comportamiento indefinido |
| `unload_digimon` | 10 | no | descarga un modelo si está entre los cargados |
| `set_autotalk` | 8 | no | cambia el `autotalk` de un NPC |
| `call_digimon_routine` | 55 | no, congela | pone al domador o al partner en una escena (abajo); al acabar pone la marca de sincronía; se espera con `wait_for_entity 0xC9` |
| `play_animation` | 850 | no | arranca una animación (estado de animación con guion); no espera a que acabe |
| `entity_set_rotation` | 336 | no, congela | gira la entidad en el siguiente fotograma (hueco de movimiento) |
| `entity_look_at_entity` | 607 | no, congela | gira la entidad hacia otra, como mucho 0x200 por fotograma |
| `entity_walk_to` | 547 | no, congela | camina a un punto con su animación de andar hasta pisar su casilla; luego vuelve a la animación 0 |
| `entity_walk_to_entity` | 38 | no, congela | igual hacia otra entidad; acaba también al chocar |
| `entity_walk_to_with_camera`, `entity_walk_to_entity_with_camera` | 57, 1 | no, congela | igual, con la cámara siguiendo al que anda |
| `entity_move_to` | 57 | no, congela | desliza a un punto en N fotogramas, sin animación |
| `entity_move_to_axis`, `entity_move_to_axis_with_camera` | 15, 27 | no, congela | desliza una coordenada (X, altura o Z) en N fotogramas |
| `move_camera_to`, `move_camera_to_entity` | 62, 158 | no, congela | lleva la cámara a un punto o a una entidad |
| `wait_for_entity` | 806 | **sí** | ver [Movimientos](#movimientos-con-guion) |
| `warp_to` | 723 | cambio de mapa, congela | ver abajo |
| `teleport` | 1 | cambio de mapa, congela | como `warp_to` con pantalla y salida de dos variables y sin sección de llegada |

- **`warp_to` (pantalla, salida, sección)** apila un marco 4 con la sección y empieza el cambio de mapa: cierra las
  cajas, restaura la variable 0, fundido de 20 fotogramas (el nombre según la variable 6), la cadena de carga del mapa
  nuevo (que **sustituye** el puntero del script) y fundido de entrada. **Las órdenes que siguen al warp nunca corren**.
  Tras la cadena, el marco 4 corre la sección de llegada del script del mapa nuevo (`0xFF`: ninguna) y luego se
  desapilan los marcos que quedaran (comprobado, `scriptInstruction46to58`, `map.c: scriptTickChangeMap`).
- Las órdenes de deslizar usan el operando de velocidad como **número de pasos**: 0 divide entre cero. Y guardan su
  avance en variables globales compartidas: solo puede correr un deslizamiento a la vez (comprobado,
  `tamer.c: tickEntityMoveTo`).

`call_digimon_routine` (comprobado, `partner.c: callDigimonRoutine`, `tamer.c`): 0 el partner va al baño del mapa;
1 curling; 2 deshace el `species_p` (vuelve a la especie de la variable 5); 3 muerte; 4 secuencia de renacer (la vida vuelve
a 360); 5 la apertura; 6 el final; 7 vida perdida por enfermedad; 8 aparece `species_t`; 9 vida perdida en combate;
10 evoluciona al Digimon de la variable 254. Otro id no hace nada (y no quita la marca de sincronía).

#### Mapa, objetos y efectos

| Orden | Usos | Qué hace |
|---|---|---|
| `set_obj_visibility` | 1669 | fija la marca de un objeto del mapa (`overworld.c: setMapObjectsFlag`); 1 lo oculta, como lee el dibujo de capas (`MapObjectInstance::HiddenFlag`) |
| `move_object_to` | 293 | lleva un objeto 2D del mapa a un punto de pantalla en N pasos (huecos 12–21, varios a la vez) |
| `rotate_3d_object` | 103 | anima una puerta, ascensor o corredera del mapa según su modelo (hueco 11, uno a la vez) |
| `spawn_item` | 241 | deja un objeto en el suelo (11 huecos; lleno: nada) |
| `spawn_chest` | 71 | pone un cofre (8 huecos) con su objeto; su disparador dice si ya se cogió (sale abierto y vacío) y se pone al cogerlo |
| `spawn_boulder`, `move_boulder`, `despawn_boulder` | 1, 13, 1 | la roca de un mapa concreto (posición fija); `move_boulder` cambia su altura y Z, la hace rodar y **cede un fotograma** (los scripts lo repiten para animarla) |
| `set_rect_impassable` | 16 | marca un rectángulo de casillas como no pisable hasta recargar el mapa |
| `spawn_sprite_at_entity` | 18 | centra objetos 2D del mapa en un nodo del modelo de una entidad |
| `call_routine` | 674 | ver [Rutinas](#rutinas-call_routine) |
| `start_battle` | 499 | ver [Combate](#combate-start_battle) |
| `delay` | 1817 | **espera** N fotogramas (0: uno) |

#### Sonido

| Orden | Usos | Qué hace |
|---|---|---|
| `play_sound` | 496 | toca un sonido de un banco |
| `set_bgm` | 266 | pone una pista (la reinicia aunque sea la misma); la variable 245 elige la variante de día o noche o una fija; `0xFF` para |

### Rutinas (`call_routine`)

Una orden con el número de rutina; el argumento suele ir en la variable 254. Todas las del 0 al 56 tienen caso; la 3,
4, 5 y 17 no hacen nada y las de 57 en adelante tampoco (comprobado, `scriptInstruction64to7E` y `tickScript`). Las
que "esperan" corren una máquina de estados propia cada fotograma hasta acabar. La columna **4a** propone cuáles hacen
falta para correr los eventos de la historia y las tiendas en la fase 4a (propuesta, la decide el manager).
Desde entonces la historia se juega entera y las rutinas que usa están en `packages/session/src/story_routines/`; qué
queda de cada una lo dice la [cobertura del port](port-coverage.md) (nota de 2026-11-06).

| Id | Usos | Qué hace | Espera | Decomp | 4a |
|---|---|---|---|---|---|
| 0 | 76 | elegir un objeto que se pueda tirar; el elegido en la 254 (`0xFF` al cancelar, y solo se puede cancelar con el disparador 3) | sí | `script_common.c: openDiscardItem` | sí |
| 1 | 2 | tienda de méritos (cartas por méritos, objetos con méritos) | sí | `shop/script_menu.c: openMeritShop` | más tarde |
| 2 | 4 | el trueque de objetos (tres ofertas, conjunto en la 249) | sí | `script_menu.c`, la tienda del trueque | más tarde |
| 6 | 3 | correr el torneo inscrito (copa en la 3; rondas ganadas en la 255) | sí | `tournament.c: scriptStartTournament`, `utils2.c: startTournament` | más tarde |
| 7 | 32 | entrenamiento del gimnasio (tipo en la 254); necesita la 39 antes | sí | `script_interp.c` + overlays de entrenamiento | sí |
| 8 | 13 | tienda de objetos; con el disparador 3, reciclaje | sí | `script_common.c: openShop`, `script_menu.c: openRecycleShop` | sí |
| 9 | 2 | gramola | sí | `script_menu.c: openJukebox` | más tarde |
| 10 | 2 | transporte de `species_e`: destino en 247/248, cobra y salta a `MAPHEAD 0x4E3` | sí | `script_menu.c: tickBirdraTransport` | más tarde |
| 11 | 2 | guardarropa de objetos | sí | `script_menu.c: tickItemKeeper` | más tarde |
| 12, 13 | 12, 69 | quitar uno / la pila entera del objeto de la 254 | no | `item.c: removeItem` | sí |
| 14 | 6 | cartas: vender (disparador 3), comprar sueltas (disparador 4) o abrir un sobre | sí | `script_menu.c: openSellCardMenu`, `openBuyCardMenu`, `rollCardPack` | más tarde |
| 15, 16 | 2, 2 | mostrar / cerrar la caja del dinero (la 16 cede un fotograma) | no | `script_common.c: createShopBitsBox` | sí |
| 18 | 2 | mostrador de inscripción de torneos (copa, día, tipo; disparadores 37–40) | sí | `dget.c: initTournamentSchedule` | más tarde |
| 19, 20, 26, 27 | 10, 11, 3, 1 | carro y montón de tierra (modelo, encoger) | 19, 20: un fotograma | `map_object.c` | más tarde |
| 21 | 107 | reiniciar la animación de objetos 2D del mapa (247, 248) | un fotograma | `overworld.c: resetMapObjectAnimation` | sí |
| 22 | 5 | comprobar y dar medallas de torneo | si da una | `tournament.c: scriptCheckTournamentMedal` | más tarde |
| 23, 24 | — | abrir / cerrar la bandeja del cofre de la 254 | sí, unos 8 fotogramas | `map_object.c` | no: el disco no la usa |
| 25 | 7 | el valor al que vuelve la variable 0 al acabar = la 254 | no | `script_interp.c` | sí |
| 28 | 140 | burbuja de humor de `species_ai` (247, 248) | no | `script_common.c: createMonochromonMoodBubble` | más tarde |
| 29 | 1 | elegir huevo al renacer | sí | `doo2.c: DOO2_openEggBox` | sí |
| 30, 31, 56 | 2, 97, 4 | acumulador de tienda: a 0, sumar 243/244, guardarlo en 243/244 | no | `script_interp.c` | sí |
| 32 | 4 | introducción y nombres de la partida nueva (elección de huevo en la 254) | sí | `script_menu.c: newGameStateMachine` | sí |
| 33 | 1 | inscribir a la fuerza en la copa especial | no | `script_interp.c` | más tarde |
| 34, 37, 38 | 1, 1, 1 | efectos de un mapa: temblor de `species_b`, quitar niebla (espera), `species_bo` | 37: sí | `map.c`, `overworld.c` | más tarde |
| 35, 39 | 3, 16 | carga asíncrona de la librería de tiendas / de entrenamiento (se espera con `wait_for_entity 0xD6`) | no | `script_common.c: loadShopLibrary`, `map.c: loadTrainingLibrary` | sí |
| 36 | — | recargar el fondo del mapa | no | `map.c: readMapTFS` | no: el disco no la usa |
| 40, 51 | 1, 1 | jefe final: precargar y efecto de aparición | no | `eab.c`, `script_interp.c` | más tarde |
| 41–44 | 1, 3, 1, 3 | cajas de Toy Town | no | `map_object.c` | más tarde |
| 45, 46 | 3, 2 | fundido a blanco y desde blanco en N fotogramas (254) | no | `fade.c` | sí |
| 47 | 2 | olvidar técnicas por perder todas las vidas (un texto por técnica) | sí | `script_common.c: lostAllLives` | sí |
| 48 | 2 | máquina de guardar | sí | `main_menu.c: openSaveMachine` | sí |
| 49, 50 | 1, 2 | pedestal de `species_ap` (aparecer; temblar, espera) | 50: sí | `map_object.c` | más tarde |
| 52, 53 | 1, 1 | precargar y correr el final; luego créditos y título | 53: sí | `tamer.c: tamerTickEnding` | más tarde |
| 54 | 1 | guardar la partida terminada | sí | `main_menu.c: gameClearSave` | más tarde |
| 55 | 5 | una sola vuelta de animación para la entidad de la 254 | un fotograma | `overworld.c: setLoopCountToOne` | sí |

## Datos

- **`MAPHEAD.SCN`** (script 0, residente) y **`DG.SCN`** (221 scripts con código): ver
  [el formato](../formats/scripts.md). `dwx` escribe un listado YAML por script; el formato del YAML es contrato con el
  editor externo de mapas.
- **Secciones de `MAPHEAD`** con tablas de texto que leen las rutinas (comprobado, `resolveMapHeadEntry`): `0x4C4`–`0x4CE`
  frases de los tenderos por tipo de Digimon, `0x4D3` sobres de cartas, `0x4D4` méritos, `0x4D5` guardarropa, `0x4D6`
  tiendas, `0x4D8` textos varios (torneos, técnicas olvidadas), `0x4DB` medallas.
- **Bloques `DATA`**: tablas de los torneos (rivales por copa, disparadores que habilitan) dentro de las secciones de
  las arenas; las leen `dget.c` y `utils2.c`, nunca el intérprete.

## Forma del control de flujo

Para R4.1 el Lua convertido no tiene saltos (decisión del usuario, 2026-10-21): `formats/script` construye el grafo de
control de cada rutina y lo estructura en `if`/`else`, bucles con `break`, elecciones y llamadas a colas compartidas
(`control_flow.hpp`, `structuring.hpp`). Lo que no se deja anidar (un bucle con dos entradas, una salida de dos bucles
a la vez, demasiadas copias) se vuelve una máquina de estados: un bucle que despacha según una variable de estado.
Cuentas sobre el disco americano (test lento `the user's disc, extracted: every event script routine structures and runs like its graph`):

<!-- Las cifras las da el test lento; se actualizan al cambiar el análisis. -->
- **222 scripts** (`MAPHEAD` y 221 de `DG.SCN`), **1553 secciones**, 7 subrutinas de `jump_and_link` y 353 colas
  compartidas: código al que saltan varias rutinas (un final común, un bucle en el que entran varias secciones), que se
  convierte una vez y se llama como última orden.
- **1550 de 1553 secciones (99,8 %) quedan anidadas sin máquina de estados.** Las 3 que no: 2 con un bucle de dos
  entradas (menús largos de diálogo que vuelven a mitad de una conversación) y 1 menú por páginas que salta de cada
  página a cualquiera de las anteriores (salidas de varios bucles a la vez). Todas convierten igual: la máquina de estados es el
  último recurso y vale para cualquier grafo, así que el 100 % de las secciones se convierte sin saltos.
- 3569 `if`, 104 bucles, 127 `break`, 372 elecciones (`SWITCH`/`SET_SELECTION`) y 1247 llamadas a colas. Anidamiento
  máximo 9 contando cada `elseif` al nivel de su primer `if`.
- **Copias**: 48 rutinas llevan algún trozo copiado (colas cortas, de menos de 3 órdenes, que se repiten en vez de
  llamarse). En total se emiten 31174 órdenes para 28282 distintas (1,10 veces).
- **Ningún `IF` del disco tiene más de un salto**; 0 `jump_to_file_and_link`, 135 `jump_to_file`, 255 `set_script`;
  ninguna referencia a una sección que no exista; 122 secciones comparten código con otra.
- `warp_to` y `teleport` acaban el flujo como `end_section` (nada de lo que les sigue corre): el código muerto tras
  ellos ya no cuenta, y un bucle de dos entradas que solo lo era por ese código pasa a anidarse.
- Cada rutina corre las mismas órdenes que su grafo para varias secuencias de resultados de condiciones (comprobación
  de paridad del test).

## Rarezas

- **`reduce_merit` pone los méritos a 0** sea cual sea el valor (los fija a −valor y luego recorta a 0) (comprobado).
  El disco no la usa; el motor la corrige (resta y se detiene en 0).
- **La orden `39` resta minutos** aunque parece la gemela de `add_minutes_to_date_at` (comprobado).
- **`add_stat` suma con signo de 16 bits**: un valor ≥ 0x8000 resta. `set_stat` y `add_stat` no tienen suelo
  (comprobado).
- **`give_item` suma en un byte**: 98 + 200 da la vuelta antes de topar en 99 (comprobado).
- **Las casillas de pisar se repiten** cada fotograma mientras el domador sigue encima (comprobado).
- **Las peticiones de script con otro en marcha se pierden** (comprobado).
- **La pila no se vacía entre scripts** ni comprueba desbordamiento; un `end_section` dentro de una subrutina acaba el
  script entero; `jump_return` con la pila vacía salta a basura (comprobado).
- **Tras `warp_to`, `teleport` o una huida no corre nada más** del script (comprobado).
- **`entity_move_to_with_camera` no se mueve a su punto**: guarda el tipo de "mover hacia entidad con cámara" y sigue un
  destino viejo; `entity_move_to_entity` y su variante con cámara comprueban la llegada contra (0, 0) (comprobado). El
  disco no usa ninguna de las tres rotas.
- **Solo un deslizamiento a la vez** (`entity_move_to*`): comparten variables globales (comprobado).
- **`wait_for_entity` con un NPC que no está espera a todo** (comprobado).
- **`set_textbox_mode` con un modo que no es 0, 1 o 2** deja la caja sin cerrar (comprobado).
- **La rutina 7 con el tipo 5 en un gimnasio de los nuevos** no prepara nada y espera para siempre; **la rutina 22** da
  como mucho una medalla por llamada (comprobado). **Ganar la copa especial** pone el disparador 37, que es el de
  "inscrito" (comprobado).
- **Disparadores ≥ 800** escriben en las variables (comprobado); el disco no los usa.
- **Un `IF` con prueba sin manejador** deja el resultado anterior; el disco no las usa (comprobado).

## Dónde está en el decomp

- `include/dw/script.h`: opcodes, operandos, `ScriptState`, tipos de movimiento.
- `include/dw/trigger.h`, `include/dw/pstat.h`: nombres de disparadores y variables.
- `src/main/script_interp.c`: `callScriptSection`, `runMapHeadScript`, `tickScript`, `scriptInstruction10to27`,
  `28to3F`, `46to58`, `5Ato5F`, `64to7E`, `FBtoFF`, `scriptIfInstruction`, `scriptPauseGame`, `returnFromScriptFile`,
  `pushScriptStack`, `tickScriptedMovement`, `getTriggerOffset`.
- `src/main/script_textbox.c`: `showTextbox`, `scriptShowSelection`, `tickSelectionDialogue`, `tickScriptDialogueBox`,
  `tickConfirmDialogue`, `tickTextboxHandling`.
- `src/main/script_value.c`: comparaciones de `IF`, `getStatsPointer`, `scriptIdToEntityId`.
- `src/main/script_common.c`: `resolveMapHeadEntry`, `handleItemLoss`, `lostAllLives`, `setDialogueOwner`, tiendas.
- `src/shop/script_menu.c`: tiendas, méritos, gramola, guardarropa, `species_e`, `newGameStateMachine`.
- `src/main/tamer.c`: `checkMapInteraction` (hablar y casillas), `startBattle`, movimientos.
- `src/main/map.c`: `changeMap`, `scriptTickChangeMap`; `src/main/main.c`: partida nueva y carga.
- `src/main/partner_impl.c`: muerte por enfermedad y por vejez; `src/main/btl.c`: `handleBattleStart`.
- `src/main/tournament.c`, `src/dget/`, `utils2.c`: torneos.
