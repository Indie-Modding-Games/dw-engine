# Sistemas de combate: Classic y Folder

El juego tiene dos sistemas de combate. **Classic** es el combate del original, sin cambios: el menú de comandos, la
obediencia, la elección de técnica del propio partner, el daño y el bloqueo del original. **Folder** es el combate del
[diseño del sistema de combate](combat-design.md) (issue #406): el entrenador ordena una técnica y un objetivo, el
partner la ejecuta de forma autónoma, se posiciona según una postura y el daño sigue la fórmula del diseño, con
cobertura y barra de defensa.

Esta página es la auditoría que pide la issue, la arquitectura que mantiene separados los dos sistemas, las decisiones
tomadas donde el diseño deja algo abierto y el plan por fases con su estado.

## Elegir el sistema de combate

- Un [modo de juego](../modules/game/modes.md) nombra su sistema de combate con la clave `battle` de `mode.yaml`:
  `classic` (el valor por defecto si falta la clave) o `folder`. **Classic Enhanced** tiene `battle: classic` y no
  ofrece otro. **Folder Mode** tiene `battle: folder`, y su `battles: [folder, classic]` permite a la partida nueva
  volver a Classic.
- El sistema se elige al crear la partida y se guarda en la sección `mode` del guardado (`battle`). Un modo que lista
  más de uno deja cambiarlo con la opción **Sistema de batalla** (`battle_system`, pestaña Juego): la próxima batalla
  juega el sistema nuevo y el próximo guardado lo conserva. La opción queda fija en un modo con un solo sistema, fuera
  de una partida y para un invitado de co-op. Un guardado escrito antes de que existieran los sistemas de combate no tiene la clave `battle` y
  se carga como `classic`: cada partida existente conserva el combate que tenía.
- Una partida nueva iniciada desde la línea de comandos (rutas de prueba, el oráculo) juega `classic`, de modo que las
  rutas y el oráculo siguen comprobando el combate del original. `--battle classic|folder` fija el sistema de la
  partida en curso, nueva o cargada, por encima del del modo y del guardado.
- **Co-op:** el combate corre en la simulación del anfitrión (multijugador §6), así que el guardado del anfitrión decide
  el sistema para todos los partners de la partida. Los comandos de la tira de un invitado viajan como antes y el
  combate del anfitrión los convierte en órdenes y posturas; una orden a un objetivo concreto viaja como la acción de
  combate `Order`, y una postura como `Stance` (protocolo 13).
  El mensaje de inicio del combate dice si se juega Folder, así que la tira de un invitado nombra las posturas y la
  cancelación igual que la del anfitrión.
- **Versus:** una partida versus no es una partida de historia y mantiene Classic en ambas máquinas (intercambia
  comandos classic y compara el checksum classic).

## La tira

Folder conserva la tira de comandos de la pantalla de combate, sus iconos y sus skins: en un combate Folder la tira de
cada luchador comandado tiene todas las entradas, sea cual sea su cerebro, y cada entrada significa otra cosa.

| Entrada de la tira | Folder |
|---|---|
| Técnica 1–3, remate | Una orden de esa técnica al objetivo actual, que la orden conserva aunque Cambiar pase a otro |
| `Attack`, `Moderate`, `Distance`, `Defend` | Las posturas Agresiva, Mantener posición, Alejarse y Protegerse; la actual aparece resaltada |
| `Change` | Pasa al siguiente enemigo: la postura lo sigue y las órdenes siguientes lo apuntan |
| `YourCall` | Descarta las órdenes en espera; la que se está ejecutando continúa |
| `Run` | La huida, como en Classic |

Una placa abajo a la izquierda (`battle.folder()` en la API de la interfaz) muestra la barra de defensa del partner y su
ruptura, el enemigo al que apuntan las próximas órdenes (Cambiar lo elige), la técnica y la fase de la orden activa con
el progreso de la fase temporizada, y las órdenes en espera con su enemigo.

## Arquitectura

```text
packages/game/battle/src/
├── core/        Battle, BattleContext, the BattleSystem interface and its factory (battle_systems.cpp)
├── rules/       ruleset readers (rules_reader.cpp, folder_rules_reader.cpp), moves, boosted moves, co-op scaling
├── mechanics/   what both systems share: decisions, actions, attacks, movement, status, animation, effects, player,
│                strategies (the original AI, obedience and damage)
├── classic/     ClassicBattle: the command strip, the commanded partner and enemy AI, the original hit and block
└── folder/      FolderBattle: fighters, strip, orders (order book, checks, cycle), stances, defense, formulas
```

- `BattleSystemId` (`Classic`, `Folder`) forma parte de `BattleSetup` (`system`). `makeBattleSystem`
  (`core/battle_systems.cpp`) es el único sitio que conoce ambos sistemas; nada más en el combate nombra uno, y
  `classic/` y `folder/` nunca se incluyen entre sí.
- `BattleSystem` (`core/battle_system.hpp`) es una estrategia con los puntos en los que Folder se diferencia: el inicio
  del combate (`start`), una orden de la tira (`command`; Huir es común y la maneja `Battle::command`), las decisiones de
  un frame (`tickDecisions`), cómo se resuelve un ataque que alcanza a un luchador (`handleAttack`) y lo que corre al
  final del frame (`endFrame`). `ClassicBattle` reenvía cada uno a las funciones que corrían ahí antes (`tickPartnerAi`,
  `tickEnemyAi`, el golpe y el bloqueo originales), en el mismo orden y con las mismas tiradas, así que Classic es el
  mismo combate bit a bit.
- El ciclo de órdenes de Folder es una máquina de estados (`folder/orders/order_cycle.cpp`): un manejador por fase
  (idle, processing, positioning, preparing, recovering) sobre un `OrderTurn`; `order_book.cpp` encola y termina
  órdenes y `order_checks.cpp` dice si una puede ejecutarse.
- Folder guarda su propio estado en `BattleState::folder` (un opcional que solo tiene un combate Folder): la cola de
  órdenes de cada luchador, la fase de ejecución, la postura, la barra de defensa y la ruptura. Sus números son el grupo
  `battle.folder` del ruleset (`content/packs/game/rules/original/battle.yaml`), así que se pueden balancear sin
  código.
- Folder reutiliza `mechanics/`, nunca la IA del partner original, la obediencia ni la fórmula de daño original.
- Los tests siguen el mismo árbol (`tests/core`, `tests/rules`, `tests/mechanics/...`, `tests/classic`, `tests/folder`).

### Código compartido modificado

| Componente | Cambio | Efecto en Classic |
|---|---|---|
| `Battle::step`, `Battle::start`, `Battle::command` | Llaman al sistema de combate en lugar de a las funciones libres | Ninguno: Classic llama a las mismas funciones en el mismo orden |
| `mechanics/attacks/attack_hits.cpp` | `hit()` se divide en la tirada de daño y `landHit()`; `handleAttack` pregunta primero al sistema | Ninguno: Classic ejecuta el camino original (`classic/attack_resolution.cpp`) |
| `Fighter` | `moveScalePercent` (100) para el multiplicador de movimiento de VEL | Ninguno: solo Folder lo fija, y 100 omite el escalado |
| `BattleState` | `folder` (opcional) | Ninguno: vacío en Classic; entonces no forma parte del checksum |
| `BattleRules` | Grupo `folder` opcional | Ninguno: Classic nunca lo lee |

Vanilla se comprueba con los tests del paquete de combate, los tests de combate de la sesión y la comparación con el
oráculo (`_work/oracle/tools/oracle.py`), que juegan Classic.

## Auditoría

La auditoría compara el combate actual (hasta ahora el único, que ambas variantes jugaban) con el diseño, mecánica por
mecánica. **Implemented**: el combate actual lo hace como indica el diseño; **partial**: tiene una versión; **missing**:
no hay nada parecido; **contradicts**: hace lo contrario. La última columna indica en qué punto está Folder.

| § | Mecánica | Combate actual | Notas | Folder |
|---|---|---|---|---|
| 2.2 | Orden de técnica | Partial | Existen los comandos `Technique1..3`, pero son comandos persistentes, y la desobediencia (disciplina) puede ignorarlos | Órdenes |
| 2.3 | La IA nunca cambia la técnica ordenada | Contradicts | `YourCall`, `Attack` y `Moderate` dejan elegir al partner; la desobediencia lo deja inactivo; la confusión elige al azar | Órdenes |
| 2.4 | Selección de objetivo | Partial | `Change` recorre los objetivos; el partner se reorienta hacia quien lo golpea (`BTL_retargetAfterHit`) | Órdenes |
| 2.8 | Cola de 2 órdenes como máximo | Missing | Un comando en búfer, retrasado por la disciplina | Órdenes |
| 2.8 | Sin MP: solo movimiento | Partial | Si no hay ningún movimiento utilizable, espera y carga (`waitWithoutMove`) | Órdenes |
| 2.8 | Fuego amigo de los movimientos de área | Partial | Los enemigos pueden golpearse entre sí; los partners nunca se golpean | Pendiente (ver decisiones) |
| 2.9 | Posturas | Partial | `Distance` retrocede ante el mayor alcance del objetivo; `Defend` se queda y bloquea; los modos de carga marcan el ritmo de las esperas | Posturas |
| 2.9 | Guardia: DEF ×2 | Missing | `Defend` bloquea todos los golpes con la reserva de velocidad | Daño |
| 2.9 | Cobertura pasiva `DEF/1000 × 30 %` | Missing | La evasión es una tirada de velocidad, y un fallo siempre bloquea | Daño |
| 2.9 | Barra de defensa y ruptura | Missing | — | Daño |
| 3.2 | Funciones de técnica | Partial | Alcance (cuerpo a cuerpo, a distancia, área, uno mismo), buffs, estado, contraataque, remate | Datos de técnicas |
| 3.6 | Efectos | Partial | Veneno, confusión, aturdimiento, aplastamiento, buffs; sin ralentización, derribo, escudos ni regeneración | Catálogo pendiente |
| 3.8 | Elemental 1 a 1 ±20 % y STAB | Contradicts | La tabla 7×7 del original en décimas, multiplicada por la especialidad de cada defensor | Daño |
| 3.8 | Vaccine/Data/Virus ±25 % | Missing | El tipo existe en los datos, pero el daño del original lo ignora | Daño |
| 3.10 | Datos de técnica | Partial | Poder, MP, distancia, alcance, estado, precisión, iframes; sin tiempo de ejecución ni de recuperación, tamaño de área ni funciones | Datos de técnicas |
| 4.2 | Ciclo de la orden | Missing | Los comandos fijan un comportamiento persistente | Ciclo de ejecución |
| 4.3 | Estados de ejecución | Partial | Indicadores internos (planificado, cargando, enfriando, atacando) pero ningún estado que el jugador vea | Ciclo de ejecución |
| 4.5 | Interrupción, cancelación, reemplazo | Partial | Un golpe cancela el ataque; nada se lo indica al jugador | Ciclo de ejecución |
| 4.6 | Resolución de orden inválida | Partial | Los ataques a distancia se rinden tras una paciencia; sin aviso | Ciclo de ejecución |
| 6.2 | Un solo atributo de velocidad (VEL) | Implemented | El combate solo tiene velocidad | — |
| 6.2 | Resta plana de DEF | Contradicts | `(ataque − defensa)` escala el poder, acotado | Daño |
| 6.4 | Multiplicador de movimiento de VEL | Missing | Los clips mueven al luchador a la velocidad del original | Fórmulas |
| 6.4 | Multiplicador de ejecución de VEL | Missing | — | Fórmulas |
| 6.5 | Multiplicador de procesamiento de INT | Partial | El cerebro decide los menús, el descuento de MP y el uso de buffs; la disciplina decide el retraso del comando | Fórmulas |
| 7.2 | Distancia y alcance | Implemented | Distancias de mundo al cuadrado de la tabla de movimientos | — |
| 7.2 | Hitboxes, tamaño | Implemented | Radio del cuerpo según los datos de la especie, alcance de contacto | — |
| 7.2 | Obstáculos, colisiones | Implemented | Colisión con mapa y luchadores, `collisionGrace` | — |
| 7.4 | La distancia no atenúa el daño | Implemented | — | — |
| 7.4 | Bonus de bloqueo por distancia | Missing | Pendiente de balance en el diseño | Pendiente |
| 7.5 | Áreas de efecto en píxeles | Partial | Las áreas son las comprobaciones de los scripts de efecto; sin campo de datos | Catálogo pendiente |
| 8 | Co-op | Partial | Aliados con comandos, objetos y resultados propios en la simulación del anfitrión; sin colas de órdenes | Co-op |
| 9 | Objetos | Implemented | Objetos lanzados: recuperación de HP/MP, curación de estados, protección, revivir; uno en vuelo por jugador | Compartido |
| 10 | Interfaz | Partial | Menú de comandos, HP/MP, barras de HP enemigas, medidor de remate; sin cola, estado ni barra de defensa | Interfaz |
| 11 | La IA ejecuta órdenes | Contradicts | La IA del partner decide según el estilo del comando y desobedece | IA |
| 12 | Métricas y escenarios | Partial | Simulador de combate (`apps/sim`) y tests | Tests por fase |

Otros hallazgos:

- **Acoplamiento.** La IA del partner original, la obediencia y el daño son estrategias, pero los comandos, la carga,
  las esperas y el bloqueo están cableados en el bucle de frames. Por eso Folder reemplaza en bloque las decisiones del
  frame y la resolución de ataques en lugar de añadir más estrategias.
- **Dependencias de Vanilla.** Folder usa el caminar, las animaciones, los efectos, los estados y el remate del
  original. Son piezas compartidas del motor, no reglas; un cambio en ellas debe mantener en verde los tests de Classic
  y el oráculo.
- **Redundancia.** La reserva de velocidad, los modos de carga y el retraso del comando se solapan con VEL e INT en el
  diseño. Folder no los usa.
- **Multijugador.** El checksum y el versus son los de Classic. El estado de Folder vive en `BattleState::folder`, que
  el checksum incluye cuando está presente.

## Decisiones sobre los puntos abiertos

El diseño marca varios puntos como pendientes. Folder adopta estas interpretaciones; cada una es un valor del ruleset o
una función pequeña, así que puede cambiar sin tocar el resto.

| Punto | Decisión |
|---|---|
| Sin órdenes | El partner no ataca por su cuenta: sin orden solo se mueve según su postura (§2.3, §2.8). |
| Cola llena | Una orden nueva reemplaza a la última en cola; la que se está ejecutando nunca se reemplaza (§2.8). |
| Objetivo derrotado o ausente | La orden se **cancela** con un motivo; el partner conserva su postura (§2.4). |
| Objetivo de una orden | Cada orden conserva el enemigo que Cambiar tenía elegido al darla, así dos órdenes pueden repartirse dos enemigos. Un partner golpeado ya no se gira hacia el atacante (`BTL_retargetAfterHit` del original): solo cambia de objetivo cuando se le ordena o cuando su objetivo desaparece (§2.4, §4.3). Los enemigos siguen girándose. |
| MP insuficiente | La orden es **inválida** y sale de la cola; el partner sigue moviéndose según su postura (§2.8, §4.6). |
| Interrumpida por un golpe | Un golpe mientras prepara o ejecuta conserva la orden: tras la reacción al golpe vuelve a posicionarse, sin procesarla de nuevo; el MP ya pagado se gasta (§2.8 "la reintenta"). Un estado (aturdido, confusión, aplastado) la devuelve en cambio al frente de la cola. |
| Agresivo y alcance | Agresivo persigue a su objetivo hasta quemarropa durante `folder.chase_frames` y luego usa la técnica desde su alcance, así un objetivo que se aleja también recibe el golpe (§2.9 deja abiertas las tolerancias). La técnica sale al terminar la preparación si el objetivo sigue a su alcance; una a distancia conserva la banda de Classic alrededor de su distancia. |
| Tiempos de técnica | Cada técnica prepara y se recupera según su coste de MP en los tramos de `folder.timings` (cuanto más cara, más lenta), los remates según `folder.finisher_timing`, y `folder.techniques` le da a una técnica los suyos; la VEL sigue acortando la preparación (§5, §6.4). Área, funciones y fuego amigo por técnica esperan al catálogo. |
| Alcance inalcanzable | Tras `folder.give_up_frames` posicionándose sin llegar al alcance, la orden falla por inalcanzable (§4.6). |
| Modificador de atributo (ATK) | `ATK × folder.attack_percent / 100`, provisional 200 %: con 100 % la DEF pesa más que el ATK y un enemigo algo más fuerte siempre gana. La sonda de balance de `folder_battle_tests.cpp` (`-tc='*balance probe*' --no-skip`) juega 40 duelos con semilla por enfrentamiento en Classic y en Folder; con 200 % las probabilidades de Folder quedan más cerca de las de Classic y un duelo dura cerca de un tercio. |
| Base elemental | Los pasos de ±20 % y el STAB se calculan sobre el poder de la técnica (§3.8 "daño base"). |
| Daño mínimo | 1 para un golpe, 0 para un golpe cubierto (§2.9 "puede quedar en 0" al cubrirse). |
| Probabilidad de acierto | Todo ataque que alcanza a un luchador acierta; la tirada defensiva es la cobertura. No se usa la evasión del original. |
| Descanso antes de vaciarse | La barra de defensa empieza a vaciarse `folder.bar_rest_frames` después del último golpe. |
| Base de ejecución y procesamiento | `folder.preparation_frames` (VEL) y `folder.processing_frames` (INT). |
| Enemigos | Conservan la IA de enemigos del original (que elige sus técnicas), pero reciben el daño, la cobertura y la barra de Folder. |
| Fuego amigo | Aún no se añade: las técnicas de área actuales no tienen indicador de aliado. Pendiente del catálogo de técnicas (§2.8). |
| Rango de estadísticas | Las estadísticas llegan hasta 999; las fórmulas las normalizan a 1000 (§6.3). |
| Confusión, aturdimiento, aplastamiento | Los estados alterados siguen anulando las órdenes mientras duran: son efectos, no elecciones de la IA. |

## Plan

Cada fase se entrega con tests y, donde se ve, con capturas de pantalla.

| Fase | Contenido | Estado |
|---|---|---|
| 0 | Auditoría y plan (esta página) | Hecha |
| 1 | Separación: `BattleSystem`, `ClassicBattle`, selección por modo y guardado, `--battle` | Hecha |
| 2 | Órdenes: cola de 2, técnica y objetivo, validación, reemplazo | Hecha |
| 3 | Ciclo de ejecución: procesamiento, posicionamiento, preparación, ejecución, recuperación, interrumpida, inválida, cancelada | Hecha |
| 4 | Fórmulas de estadísticas: movimiento y ejecución de VEL, procesamiento de INT, centralizadas en `folder_formulas` | Hecha |
| 5 | Daño: elemental 1 a 1, STAB, tipo ±25 %, DEF plana, cobertura, Guardia, barra de defensa, ruptura | Hecha |
| 6 | Posturas: `Aggressive`, `Hold`, `Distance`, `Guard`; la ruptura pasa por defecto a `Hold` | Hecha |
| 7 | IA: ejecuta órdenes, nunca cambia de técnica ni de objetivo; los enemigos conservan la IA original | Hecha |
| 8 | Co-op: la tira del invitado se convierte en órdenes en el anfitrión; acciones de combate `Order` y `Stance`; una cola por partner; la tira del invitado muestra las etiquetas de Folder | Hecha |
| 9 | Objetos: los objetos lanzados compartidos, sin cambios en Folder | Hecha (compartida) |
| 10 | Interfaz: la tira da órdenes y posturas, `Change` elige el objetivo; la placa muestra la cola, la fase, la barra de defensa y la ruptura | Hecha (primera versión) |
| 11 | Datos de técnicas: ejecución y recuperación por tramo de MP con entradas por técnica; área, funciones y fuego amigo por técnica | Parcial (tiempos hechos) |
| 12 | Balance: escala de atributos (provisional, de la sonda), bonus de bloqueo por distancia, costes (`costo-mp.md`) | Parcial |
| 13 | Volver a Classic desde el juego en una partida de Folder Mode: la opción `battle_system` | Hecha |
| 14 | Objetivos: Cambiar elige el enemigo, cada orden lo conserva, el partner nunca cambia solo, y la placa nombra el enemigo de cada orden | Hecha |

## Balance pendiente

- La escala del modificador de atributo (`attack_percent`), ajustada solo con las técnicas de prueba; las técnicas reales pueden moverla.
- El bonus de probabilidad de bloqueo por distancia (§7.4).
- Los tramos de tiempos y el tiempo de los remates (`folder.timings`, `folder.finisher_timing`), y las entradas por técnica.
- Costes de MP: los costes del original hasta que se aplique `costo-mp.md`.
- Dificultad del co-op con Folder: las tablas de escalado del co-op son las de Classic.
