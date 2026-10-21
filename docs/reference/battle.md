# Ficha de referencia: batalla

Cómo funcionaban los combates del juego original, según el decomp. Es el paso 1 del
[ciclo de diseño](../conventions/design-cycle.md): describe el original, no lo que hará el mod.

Marcas: **(comprobado)** quiere decir que se leyó en el código del decomp; **(suposición)** quiere decir que es una
interpretación nuestra que falta confirmar. Los tiempos se dan en fotogramas de batalla: el bucle de batalla
avanza una vez por fotograma.

## Qué hace

El combate es en tiempo real. El partner pelea solo, con su propia IA, y el jugador le da órdenes generales
("ataca", "guarda distancia", "defiende") o le pide técnicas concretas. Cuántas órdenes entiende depende de su
inteligencia, y que obedezca depende de su disciplina. Cada técnica gasta MP y "velocidad" acumulada; el daño
depende del ataque, la defensa y la afinidad entre especialidades. Hay estados alterados y un ataque de remate que
se carga con el tiempo. Al terminar, el partner gana stats, dinero, objetos y quizá técnicas nuevas, pero se cansa y
puede lesionarse.

## Reglas

### Bucle y resultado

- `BTL_battleMain` prepara el combate y en cada fotograma ejecuta la IA del partner, la de los enemigos y los
  estados de cada luchador, hasta que el combate termina. Devuelve 1 si se gana, 0 si se huye y −1 si cae el partner
  **(comprobado)**.
- Hay como mucho 3 enemigos **(comprobado)**.
- **El daño se aplica poco a poco.** Cada golpe se suma a una reserva de daño pendiente (tope 9999) que se va
  restando a la vida a un ritmo que depende de su tamaño (`damageTick`). Un luchador cuenta como derrotado cuando su
  vida menos el daño pendiente llega a 0 (`BTL_isFighterDefeated`) **(comprobado)**.
- El MP del partner también se descuenta con una reserva; el de los enemigos, al momento **(comprobado)**.

### Stats y su papel

| Stat | En batalla |
|---|---|
| HP / MP | Reservas de vida y de energía para técnicas. Con un 20 % de HP o menos el luchador usa la animación de herido. |
| Ataque / defensa | Entran en la fórmula de daño. La defensa también puntúa a qué enemigo atacar. |
| Velocidad | Llena la reserva de velocidad, sube la esquiva, acorta el remate y da probabilidad de contraataque. |
| Inteligencia | Número de órdenes disponibles, desobediencia, puntería al elegir objetivo, uso de técnicas de apoyo y de área, y descuento de MP. |

Todo **(comprobado)** en `BTL_initializeCombat`, `BTL_calculateDamage`, `BTL_calculateHitChance` y la IA.

- **Partner enfermo o lesionado:** al empezar el combate pierde un 20 % de ataque, defensa, velocidad e
  inteligencia, y sus técnicas cuestan un 50 % más de MP (`BTL_initializeCombat`, `BTL_isMoveUsable`)
  **(comprobado)**.
- Los cambios temporales se deshacen al final a partir de una copia de los stats iniciales
  (`resetStatsAfterCombat`, `BTL_deinitializeCombat`) **(comprobado)**.

### Órdenes del jugador

Las órdenes son: huir, atacar, a su aire (la de por defecto), moderado, distancia, defensa, cambiar de objetivo,
usar una de las tres técnicas equipadas y remate (`command_shout.c`) **(comprobado)**.

El menú crece con la inteligencia (`BTL_initializeCombat`) **(comprobado)**:

| Inteligencia | Órdenes visibles |
|---|---|
| < 100 | Huir, a su aire |
| 100–199 | + atacar |
| 200–299 | + moderado |
| 300–399 | + distancia y cambiar |
| 400–499 | + defensa |
| ≥ 500 | Huir, cambiar, defensa, distancia, una por cada técnica equipada (de la tercera ranura a la primera), a su aire. **Atacar y moderado desaparecen.** |

En combates de guion la orden de huir no aparece en el menú (`BTL_initializeCombat`) **(comprobado)**.

Qué hace cada una (`BTL_tickPartnerAI`, `BTL_handlePartnerMoveCommand`) **(comprobado)**:

- **Atacar:** modo de carga agresivo y elige la técnica más potente (en empate, la de mejor afinidad; luego al azar).
- **Moderado:** modo de carga prudente y elige la técnica más barata en MP.
- **A su aire:** vuelve al modo de carga propio del partner y deja que la IA elija (ver más abajo).
- **Distancia:** se aleja hasta quedar fuera del alcance de la técnica más larga del objetivo, con margen. No ataca.
- **Defensa:** se queda quieto mirando al objetivo y confía en el bloqueo.
- **Cambiar:** con dos enemigos alterna el objetivo; con tres, rota.
- **Técnica concreta:** la pone en cola si se puede usar (MP suficiente y demás condiciones de `BTL_isMoveUsable`).

**Retraso de órdenes desactivado:** `handleCommands` calcula un retraso según la disciplina y acto seguido lo pone
a 0, así que las órdenes son inmediatas **(comprobado)**.

### Obediencia

- **Desobediencia:** con inteligencia menor que 301, cada (inteligencia / 2 + 1) × 20 fotogramas el partner tiene un
  (70 − disciplina) % de quedarse parado 100 fotogramas. Recibir un golpe lo saca de ese estado
  (`BTL_tickPartnerAI`, `BTL_tickFighterAction`) **(comprobado)**. En los enemigos la probabilidad es
  (300 − inteligencia) / 4 % **(comprobado)**.
- **Cambio de objetivo al recibir un golpe:** el partner se vuelve hacia quien le golpeó con una probabilidad que
  depende del modo de carga (70, 40, 30 o 0 %) menos la disciplina; siempre lo hace si su objetivo ya cayó
  (`BTL_retargetAfterHit`) **(comprobado)**.

### IA del partner

**Elección de objetivo** (`BTL_selectFighterTarget`, `BTL_calculateTargetScore`) **(comprobado)**:

- Inteligencia ≥ 300: prefiere un enemigo con estado alterado. Si no hay, el de menor puntuación, y solo cambia si
  el actual puntúa al menos 40 más.
- Inteligencia ≥ 150: el de menor puntuación.
- Menos: el más cercano.
- Puntuación = % de HP restante + (afinidad invertida × defensa relativa) / 10. Un enemigo débil y vulnerable puntúa
  bajo... en teoría: `BTL_calculateTargetScore` usa la vida y la defensa **del propio luchador**, no las del
  candidato, así que solo la afinidad distingue a los enemigos (ver rarezas) **(comprobado)**.
- `BTL_getWeakestEnemy` devuelve la puntuación del último enemigo que miró, no la del elegido, y con ella se decide si
  cambia de objetivo (ver rarezas) **(comprobado)**.

**Elección de técnica "a su aire"** (`BTL_selectPartnerMove`) **(comprobado)**. Sorteo ponderado entre las tres
técnicas equipadas (la cuarta ranura es el remate):

- Peso base según el modo de carga: agresivo, por orden de potencia (50, 20, 5); normal, 20 cada una; prudente, por
  orden de coste de MP (50, 20, 10).
- Bonus de afinidad contra el objetivo: de +10 (muy eficaz) a +1 (muy poco eficaz).
- Técnicas de apoyo: + un contador de prioridad que nace de la inteligencia (inteligencia / 10 + 5) y baja con el
  tiempo.
- Técnicas de área, con inteligencia ≥ 200 y al menos dos enemigos vivos: + 10 o 15 por enemigo según el modo.
- En modo prudente con menos del 31 % de HP pesa el alcance de la técnica.
- Una ranura vacía pesa 5; si sale, el partner carga 80 fotogramas en lugar de atacar.

**IA enemiga** (`BTL_selectEnemyMove`, `BTL_tickEnemyAI`) **(comprobado)**: cada enemigo tiene una prioridad por
técnica en sus datos de mapa, con extras para técnicas de área y de apoyo, y lanza su remate en cuanto tiene la
barra llena. Su modo de carga es aleatorio, o prudente si está fuera de pantalla.

### Técnicas

- **Coste de MP:** el valor guardado × 3 (`BTL_isMoveUsable`, `BTL_setupMoveExecution`) **(comprobado)**.
  - Descuento del partner por inteligencia: 700–799 −5 %, 800–899 −10 %, 900–998 −15 %, 999 −20 %.
  - Partner enfermo o lesionado: × 1,5.
  - Los remates no gastan MP.
- **La potencia también es un coste de velocidad:** usar una técnica resta su potencia a la reserva de velocidad,
  que no baja de −155 **(comprobado)**.
- **Alcance:** 1 cuerpo a cuerpo, 2 a distancia, 4 apoyo sobre uno mismo (`range`) **(comprobado)**; el 3 se usa
  como técnica de área **(suposición, por cómo lo trata la IA)**.
- **Especialidades:** cada técnica tiene una especialidad (0–6) y cada Digimon hasta tres. La afinidad entre ellas
  viene de una tabla de 7 × 7 cuyos valores 20, 15, 10, 5 y 2 significan ×2, ×1,5, ×1, ×0,5 y ×0,2
  (`TYPE_FACTORS`) **(comprobado)**.
- **Bloques de índices:** las técnicas por debajo de 0x3A se pueden aprender; 0x2D es el contraataque; de 0x3A a
  0x70 son remates; 0x79 es el proyectil que se usa aplastado **(comprobado)**.
- **Técnicas de apoyo:** cinco técnicas concretas suben ataque, defensa o velocidad entre un 5 y un 30 % durante el
  combate, con tope 999 (`BTL_applyBuffMove`) **(comprobado)**.

### Velocidad y ritmo

- La reserva de velocidad empieza en 100 y gana velocidad / 100 + 1 cada 2 fotogramas, con tope 100
  (`BTL_increaseSpeedBuffer`) **(comprobado)**.
- Modos de carga: agresivo, actúa en cuanto la reserva es positiva; normal, espera a tener la potencia de la técnica
  (o 100); prudente, espera a 100 mientras retrocede (`BTL_applyChargeRequirement`, `BTL_tickChargeState`)
  **(comprobado)**.
- Pausas: 40 fotogramas tras cada ataque y 80 si no puede pagar ninguna técnica **(comprobado)**.
- Las técnicas a distancia buscan su distancia de alcance; si en 80 fotogramas no la consiguen, disparan igual
  (`BTL_tickRangedAttack`) **(comprobado)**.

### Daño

`BTL_calculateDamage` **(comprobado)**. Sea **A** la suma de las tres afinidades de la técnica contra las
especialidades del defensor (10 si la ranura está vacía), así que A / 30 es el multiplicador medio.

- **Técnica normal:**
  - dif = ataque − defensa, limitado a ±500.
  - daño = A × (potencia + dif × potencia / 500) / 30 × azar(90..110) / 100.
  - El contraataque usa solo el 30 % de la defensa.
- **Remate:**
  - daño = (ataque + potencia) × A / 30, sin defensa.
  - Partner: × carga / 40 si la carga pasa de 40 (hasta × 2).
  - Enemigo: × azar(100..200) / 100.
  - Después, × azar(90..110) / 100.
- El resultado queda entre 1 y 9999.

### Acierto, bloqueo y contraataque

`BTL_calculateHitChance` **(comprobado)**:

- **Acierta siempre** si el defensor es bebé o de entrenamiento; si está aturdido, aplastado o desobedeciendo; si el
  partner está enfermo (vale para los dos bandos); si la reserva de velocidad del defensor es 0 o menos; si el
  defensor está en mitad de una técnica; o si es un remate (que además rompe el bloqueo).
- **Falla siempre** si el defensor ya está bloqueando.
- **Si no:**
  - x = precisión / 2 × (velocidad del defensor − velocidad del atacante / 10) / 999.
  - x × 1,2 si el defensor está quieto.
  - acierto = precisión − x, entre 0 y 100 %.

Al fallar (`BTL_tickAttackHits`) **(comprobado)**:

- Contra un ataque cuerpo a cuerpo, el defensor puede contraatacar si tiene la técnica de contraataque y al menos
  165 de MP, con probabilidad velocidad / nivel %.
- Si no, bloquea: 3 fotogramas de invulnerabilidad, y un ataque cuerpo a cuerpo bloqueado aún hace entre el 10 y el
  30 % del daño.

Al acertar, el defensor recibe los fotogramas de invulnerabilidad de la técnica y la tirada de estado
**(comprobado)**.

### Estados alterados

Se aplican al acertar, con la probabilidad de estado de la técnica, salvo que el defensor esté protegido
(`BTL_applyMoveStatus`). Avanzan en `BTL_tickStatusEffects`. Todo **(comprobado)**:

| Estado | Efecto | Duración |
|---|---|---|
| Veneno | Cada 100 fotogramas quita entre el 1 y el 3 % de la vida máxima | Hasta que se cura o acaba el combate |
| Confusión | 70 % de atacar al aire en cada elección; si no, a un luchador al azar. Gira sin control | 200–300 fotogramas |
| Aturdimiento | No puede actuar | 200–240 fotogramas |
| Aplastado | Tras 65 fotogramas de animación, solo puede lanzar un proyectil cada 40 fotogramas y todo le acierta | 224–304 fotogramas |

- Solo se muestra un icono a la vez: aturdimiento antes que confusión y confusión antes que veneno.
- Algunos objetos curan los estados en combate (`BTL_healStatusEffect`).
- Que la confusión pueda hacer que los enemigos se golpeen entre ellos es **(suposición)**.
- La confusión de cualquier luchador, aunque se le acabe el tiempo, solo se quita en un fotograma en que el partner
  no está atacando (ver rarezas) **(comprobado)**.

### Remate y su barra

**(comprobado)**:

- Meta de la barra = 3000 − velocidad (`BTL_initializeCombat`).
- Progreso: +1 por fotograma; + meta × 2 / 50 por cada ataque completado; + meta × 3 / 50 por cada bloqueo; quien
  ve bloqueado su ataque gana + meta × 4 / 50 (`BTL_addFinisherProgress`, `BTL_tickFighterStates`,
  `BTL_tickAttackHits`).
- Solo se llena si la cuarta ranura tiene un remate.
- Con la barra llena, un botón lanza la orden de remate (`handleCommands`). El resto de la IA se detiene y empieza
  un aura de 80 fotogramas. Durante ese tiempo, cada pulsación de otros botones suma 2 a la carga, hasta 80; la carga
  multiplica el daño (`BTL_startQueuedMove`, `BTL_tickFinisherChargeup`). Qué botones exactos son es
  **(suposición)**.
- Al lanzarlo, el progreso vuelve a 0.

### Fin del combate y consecuencias

- **Victoria:** todos los enemigos han terminado su animación de muerte y el partner sigue vivo
  (`BTL_isBattleFinished`) **(comprobado)**.
- **Huida:** desactivada en combates de guion. El tamer retrocede y el combate acaba si el partner sigue vivo.
  Felicidad −10, disciplina −6, cansancio +2, más el cansancio de después del combate. Cuenta en un contador de
  huidas (`startBattle`) **(comprobado)**.
- **Derrota:** si el partner llega a 0 HP, hay una cuenta atrás durante la cual un objeto de revivir aún puede
  salvarlo. Al revivir se reinician todos los luchadores, no solo el partner: sin enfriamiento, sin técnica planeada,
  sin persecución, carga ni enfriamiento, y se cancela el remate (`BTL_isBattleFinished`) **(comprobado)**. La cuenta
  atrás se para mientras el tamer usa un objeto. Felicidad −30, disciplina −20 (`startBattle`). Después se pierden el
  30 % de los huecos ocupados de la bolsa, redondeando hacia abajo y cada uno con su pila entera (`handleItemLoss`, ver
  la [ficha de inventario](inventory.md)), y una vida; con 0 vidas, la vida restante pasa a 0 y el partner muere
  **(comprobado)**. Ver la [ficha de cuidado](care.md).
- **HP y MP tras perder:** ningún código de combate los toca; el partner pasa al mapa de la derrota
  (`murd.c: MURD_tick`, mapa 205 o 218) y el guion de ese mapa (script 212, sección 0x48) suma 9999 al HP y al MP
  actuales, que `script_value.c: enforceStatsLimits` deja en el máximo: vuelve con todo **(comprobado en el guion
  desensamblado)**.
- **Victoria, cuidado:** felicidad +2, batallas +1 (cuenta para la [evolución](evolution.md)) **(comprobado)**.
- **Cansancio y hambre tras ganar o huir** (`handlePostBattleTiredness`): cansancio +5, más un extra si el MP
  acabó por debajo del 80 % (ver rarezas), y la comida baja 15 **(comprobado)**.
- **Tiempo:** cada combate adelanta 20 minutos el reloj del juego (`advanceBattleTime`) **(comprobado)**.
- **Lesión:** si alguien recibió un golpe en el combate (no solo el partner: `handleBattleEndBox` mira
  `HAS_TAKEN_DAMAGE`, que marca cualquier golpe) y el partner no estaba lesionado, probabilidad = cansancio − % de HP
  restante (`handleBattleInjury`) **(comprobado)**.

### Recompensas

`battleStatsGainsAndDrops`, `handleBattleEndBox` **(comprobado)**:

- **Dinero:** la suma del dinero de cada enemigo, con tope de 999 999 en el monedero.
- **Objetos:** cada enemigo suelta su objeto con su probabilidad (salvo en una pantalla concreta).
- **Stats:** para cada stat se toma el valor más alto entre los enemigos (E) y el del partner (P), con un factor F
  que crece con el número de enemigos (10, 12 o 16):
  - Si E ≥ P: ganancia = redondeo hacia arriba de E × F / (P × 10).
  - Si E < P: probabilidad E × F × 100 / (P × 10) % de ganar 1.
  - Si un stat no ganó nada, hay otra tirada de +1 según cómo fue el combate: HP, % de vida perdida; MP y ataque,
    10 × ataques hechos; defensa, 10 × golpes recibidos de al menos el 20 % de la vida; velocidad, 10 × bloqueos +
    la mitad del % de vida perdida; inteligencia, 5 × ataques + 5 × golpes fuertes.
  - Las ganancias se suman tal cual. Topes: HP y MP 9999, el resto 999.

### Aprender técnicas en batalla

`battleMoveLearning`, `BTL_setupMoveExecution` **(comprobado)**:

- Durante el combate se apuntan hasta 12 técnicas aprendibles distintas que usan los enemigos.
- Tras ganar, cada una entra en sorteo si el partner no la sabe, si es de una de sus especialidades y si está en su
  lista de 16 técnicas posibles.
- Cada candidata tira su probabilidad en `MOVE_LEARN_CHANCES` (por técnica y por ranura de especialidad). De las
  que aciertan se aprende una al azar.
- No se pide inteligencia. Dos parejas de técnicas se aprenden juntas (`learnMove`).

### Enemigos que se unen

Al empezar un combate, otros enemigos visibles se unen con una probabilidad que depende del tipo del partner y del
enemigo (`FLEE_CHANCES`, 70 % si el partner no tiene tipo). Dos objetos suben o bajan esa probabilidad
(`handleBattleStart`) **(comprobado)**.

## Datos

Tablas del ejecutable principal. `dwx` las extraerá a YAML desde el disco del usuario; aquí solo se describe su forma.

| Tabla | Entrada | Entradas | Campos |
|---|---|---|---|
| `MOVE_DATA` | 16 bytes | 122 | Distancia de alcance (al cuadrado), potencia, coste de MP (÷ 3), fotogramas de invulnerabilidad que causa, alcance (1–4), especialidad, estado que causa (1 veneno, 2 confusión, 3 aturdimiento, 4 aplastado), precisión, probabilidad de estado, banderas (bit 0: necesita objetivo; bit 1: bloqueo compartido de 110 fotogramas) y dos campos sin identificar. |
| `MOVE_NAMES` | — | — | Nombres de las técnicas. |
| `MOVE_LEARN_CHANCES` | 3 bytes | 58 | Probabilidad (%) de aprender cada técnica aprendible según la ranura de especialidad del partner. |
| `TYPE_FACTORS` | 7 bytes | 7 | Afinidad de cada especialidad atacante contra cada especialidad defensora, en décimas. |
| `FLEE_CHANCES` | — | 3 × 3 | Probabilidad de que un enemigo se una al combate según los tipos. |
| `STAT_GAIN_ENEMY_FACTORS` | — | 3 | Factor de ganancia de stats según el número de enemigos. |
| `DIGIMON_DATA` | 52 bytes | 180 | Ver la [ficha de evolución](evolution.md). En batalla se usan el tipo, el nivel, las especialidades, el objeto que suelta y su probabilidad y la lista de técnicas. |

Los stats de combate de cada enemigo no están en una tabla global: vienen en los datos de cada mapa (vida, MP,
ataque, defensa, velocidad, inteligencia, dinero, modo de carga, cuatro técnicas con su prioridad y datos de huida),
leídos en `loadMapDigimon` (`overworld.c`) **(comprobado)**. Las estructuras están en `include/dw/move.h`,
`combat.h`, `entity.h` y `params.h`.

## Rarezas

Todas **(comprobado)** salvo donde se indica:

- **Cansancio por MP roto.** En `handlePostBattleTiredness` el extra por MP gastado divide antes de multiplicar, así
  que casi siempre suma 0. El propio decomp lo anota.
- **Órdenes sin retraso.** El retraso por disciplina se calcula y se descarta.
- **Enfermedad = acierto seguro.** Con el partner enfermo, todos los ataques del combate aciertan, también los suyos.
- **Atacar y moderado desaparecen** con inteligencia 500 o más, cuando parecería que el menú solo crece.
- **Contraataque con MP.** Aunque el contraataque no gasta MP, exige tener al menos 165.
- **Errores de bucle** en `BTL_initializeCombat` (índice cambiado) y `BTL_setupMoveExecution` (una condición que
  siempre se cumple). En la rama de poca vida de `BTL_selectPartnerMove` el bucle interno reutiliza el índice externo
  **(suposición: parece un error)**.
- En combates de guion sin huida los enemigos no gastan MP.
- **Puntuación de objetivo con stats propios.** `BTL_calculateTargetScore` usa la vida y la defensa del que elige.
- **El más débil informa mal.** `BTL_getWeakestEnemy` devuelve la puntuación del último enemigo mirado.
- **La confusión espera al partner.** Ninguna confusión se quita mientras el partner ataca.
- **Lesión por cualquier golpe.** La tirada de lesión tras ganar se hace si cualquier luchador recibió un golpe.
- **Sin límite de tiempo.** Dos luchadores sin MP para ninguna técnica esperan para siempre; el original no tiene
  tiempo máximo de combate.
- El torneo (`src/std/`) y el modo versus (`src/vs/`) tienen copias casi iguales de esta lógica; no se han comparado
  en detalle **(suposición: pueden diferir)**.

## Dónde está en el decomp

| Qué | Dónde |
|---|---|
| Bucle, IA, daño, acierto, estados, remate | `src/btl/battle_main.c`: `BTL_battleMain`, `BTL_tickPartnerAI`, `BTL_tickEnemyAI`, `BTL_selectPartnerMove`, `BTL_selectEnemyMove`, `BTL_selectFighterTarget`, `BTL_calculateDamage`, `BTL_calculateHitChance`, `BTL_tickAttackHits`, `BTL_applyMoveStatus`, `BTL_tickStatusEffects`, `BTL_addFinisherProgress`, `BTL_applyBuffMove` |
| Preparación, menú por inteligencia, fin | `src/btl/battle_setup.c`: `BTL_initializeCombat`, `BTL_isBattleFinished` |
| Órdenes y carga del remate | `src/btl/command_shout.c`, `src/btl/command_menu.c`: `BTL_tickFinisherChargeup`; `src/main/battle_tick.c`: `handleCommands` |
| Recompensas, ganancias, lesión, aprendizaje | `src/main/battle_ui.c`: `battleStatsGainsAndDrops`, `handleBattleEndBox`, `handleBattleInjury`, `battleMoveLearning`, `resetStatsAfterCombat` |
| Consecuencias de cuidado | `src/main/tamer.c`: `startBattle`, `advanceBattleTime`; `src/main/partner_impl.c`: `handlePostBattleTiredness` |
| Inicio y enemigos que se unen | `src/main/btl.c`: `handleBattleStart` |
| Daño gradual | `src/main/utils.c`: `damageTick` |
| Enemigos por mapa | `src/main/overworld.c`: `loadMapDigimon` |
