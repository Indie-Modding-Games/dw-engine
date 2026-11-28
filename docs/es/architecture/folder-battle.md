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
  el sistema para todos los partners de la partida. Los comandos de la tira de un invitado viajan como acciones de
  combate `Command`, igual que en Classic, y el combate Folder del anfitrión los convierte en órdenes y posturas (Cambiar
  incluido, así las órdenes de un invitado conservan el enemigo que eligió). El protocolo tiene además las acciones
  `Order` (una orden a un objetivo concreto) y `Stance` (protocolo 13), que el anfitrión aplica, pero el cliente del
  invitado aún no las envía: su tira no tiene otra forma de nombrar un objetivo.
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

Una placa abajo a la izquierda (`battle.folder()` en la API de la interfaz) tiene cuatro filas de altura fija, así que
nunca crece sobre los luchadores: la barra de defensa del partner; un mensaje, primero la ruptura, si no, durante cinco
segundos, cómo terminó la última orden cuando no se completó (anulada, interrumpida, inválida o reemplazada, con el
motivo: MP insuficientes, su enemigo ya no está, fuera de alcance...), y si no, el estado alterado del partner; la
técnica, el enemigo y la fase de la orden activa con el progreso de la fase temporizada, o el enemigo al que apuntan las
próximas órdenes mientras no corre ninguna (Cambiar lo elige); y las órdenes en espera. La línea bajo la tira dice por
qué la técnica enfocada o el remate no se pueden ordenar ahora; en la skin moderna, cuyos botones ya nombran los
comandos y cuyo nombre de técnica se anuncia al empezar, esa línea solo muestra el motivo.

La placa se queda en su propia región para no tapar nunca otro recuadro: en la skin moderna está entre la tarjeta del
partner y las ayudas de teclas, bajo las tarjetas de los enemigos; en las skins original, pixel y DevVago ocupa la
esquina inferior izquierda y sube sobre la tira de un combate de copa. En todas las skins se aparta mientras la bolsa
está abierta, y todo el HUD se va al abrirse los resultados. Los números, las barras de los enemigos y los gritos sobre
los luchadores se apartan de los recuadros del HUD.

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
  (`core/battle_systems.cpp`) es el único sitio que construye un sistema, y `classic/` y `folder/` nunca se incluyen
  entre sí. Los puntos de entrada de Folder de la API de `Battle` (`order`, `cancelOrders`, `setStance`, `orderProblem`,
  `defenseBarCapacity`, `folderTechnique`, y `partnerMoveCost` en un combate Folder) llaman a `folder/` directamente; el
  frame solo llega a Folder por la interfaz `BattleSystem`.
- `BattleSystem` (`core/battle_system.hpp`) es una estrategia con los puntos en los que Folder se diferencia: el inicio
  del combate (`start`), una orden de la tira (`command`; Huir es común y la maneja `Battle::command`), las decisiones de
  un frame (`tickDecisions`), cómo se resuelve un ataque que alcanza a un luchador (`handleAttack`), lo que corre al
  final del frame (`endFrame`), el MP que paga un luchador comandado por una técnica (`commandedMpCost`), a quién alcanza
  un ataque lanzado (`reached`) y a quién puede tocar su efecto en el camino (`touches`). `ClassicBattle` reenvía cada
  uno a las funciones que corrían ahí antes (`tickPartnerAi`, `tickEnemyAi`, el golpe y el bloqueo originales); los
  tres últimos son los predeterminados de la clase base, que son el código del original movido tras la interfaz. Mismo
  orden, mismas tiradas: Classic es el mismo combate bit a bit.
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
| `mechanics/actions/fighter_actions.cpp` (`beginAttack`) | El pago de MP de un luchador comandado pregunta a `BattleSystem::commandedMpCost` | Ninguno: el predeterminado es `partnerMpCost` del original |
| `mechanics/attacks/attack_hits.cpp` (`releaseAttack`, espectadores) | A quién alcanza y toca un ataque pregunta a `BattleSystem::reached` y `touches` | Ninguno: los predeterminados devuelven `reachedBy` y `foes` como antes |
| `Battle::partnerMoveCost` | En un combate Folder, el coste de Folder | Ninguno: Classic conserva el del original |
| `core/checksum.cpp` | Incluye todo el estado de Folder (colas, orden activa y objetivo, descanso, retención, duración de la ruptura, escala de movimiento) | Ninguno: solo cuando `folder` está presente |

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
| 2.8 | Fuego amigo de los movimientos de área | Partial | Los enemigos pueden golpearse entre sí; los partners nunca se golpean | Catálogo |
| 2.9 | Posturas | Partial | `Distance` retrocede ante el mayor alcance del objetivo; `Defend` se queda y bloquea; los modos de carga marcan el ritmo de las esperas | Posturas |
| 2.9 | Guardia: DEF ×2 | Missing | `Defend` bloquea todos los golpes con la reserva de velocidad | Daño |
| 2.9 | Cobertura pasiva `DEF/1000 × 30 %` | Missing | La evasión es una tirada de velocidad, y un fallo siempre bloquea | Daño |
| 2.9 | Barra de defensa y ruptura | Missing | — | Daño |
| 3.2 | Funciones de técnica | Partial | Alcance (cuerpo a cuerpo, a distancia, área, uno mismo), buffs, estado, contraataque, remate | Datos de técnicas |
| 3.6 | Efectos | Partial | Veneno, confusión, aturdimiento, aplastamiento, buffs; sin ralentización, derribo, escudos ni regeneración | Los efectos del original; los nuevos esperan técnicas nuevas |
| 3.8 | Elemental 1 a 1 ±20 % y STAB | Contradicts | La tabla 7×7 del original en décimas, multiplicada por la especialidad de cada defensor | Daño |
| 3.8 | Vaccine/Data/Virus ±25 % | Missing | El tipo existe en los datos, pero el daño del original lo ignora | Daño |
| 3.10 | Datos de técnica | Partial | Poder, MP, distancia, alcance, estado, precisión, iframes; sin tiempo de ejecución ni de recuperación, tamaño de área ni funciones | Tiempos, catálogo |
| 4.2 | Ciclo de la orden | Missing | Los comandos fijan un comportamiento persistente | Ciclo de ejecución |
| 4.3 | Estados de ejecución | Partial | Indicadores internos (planificado, cargando, enfriando, atacando) pero ningún estado que el jugador vea | Ciclo de ejecución |
| 4.5 | Interrupción, cancelación, reemplazo | Partial | Un golpe cancela la técnica hasta que se compromete (el punto de compromiso del original); el veneno nunca interrumpe; nada se lo indica al jugador | Ciclo de ejecución |
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
| 7.4 | Bonus de bloqueo por distancia | Missing | Pendiente de balance en el diseño | Cobertura (provisional) |
| 7.5 | Áreas de efecto en píxeles | Partial | Las áreas son las comprobaciones de los scripts de efecto; sin campo de datos | Catálogo |
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
- **Multijugador.** El versus es el de Classic. El estado de Folder vive en `BattleState::folder`, que el checksum
  incluye cuando está presente: cada campo que mueve la simulación (la ranura, el objetivo y el número de cada orden, la
  orden activa, la fase y su duración, la barra, su descanso, la ruptura y su duración, la retención por estado, el
  compromiso, cómo terminó la última orden) y la escala de movimiento por VEL de cada luchador.

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
| Interrumpida por un golpe | Un golpe mientras prepara o ejecuta conserva la orden: tras la reacción al golpe vuelve a posicionarse, sin procesarla de nuevo; el MP ya pagado se gasta (§2.8 "la reintenta"). Un estado (aturdido, confusión, aplastado) la devuelve en cambio al frente de la cola. El diseño no fijaba un punto de compromiso, así que Folder toma el del original (#425): cuando el efecto de la técnica ya lanzó la parte que golpea (la bola de fuego de Spit Fire, hacia la mitad de su animación), un golpe al usuario ya no la para; llega y la orden cuenta como hecha, con su recuperación tras la reacción al golpe. Un tick de veneno nunca interrumpe. |
| Agresivo y alcance | Agresivo persigue a su objetivo hasta quemarropa durante `folder.chase_frames` y luego usa la técnica desde su alcance, así un objetivo que se aleja también recibe el golpe (§2.9 deja abiertas las tolerancias). La técnica sale al terminar la preparación si el objetivo sigue a su alcance; una a distancia conserva la banda de Classic alrededor de su distancia. |
| Tiempos de técnica | Cada técnica prepara y se recupera según su coste de MP en los tramos de `folder.timings` (cuanto más cara, más lenta), los remates según `folder.finisher_timing`, y `folder.techniques` le da a una técnica los suyos; la VEL sigue acortando la preparación (§5, §6.4). Los tramos leen el coste de la tabla de movimientos, no el de Folder, así que los tiempos no cambiaron al cambiar los costes. |
| Alcance inalcanzable | Tras `folder.give_up_frames` posicionándose sin llegar al alcance, la orden falla por inalcanzable (§4.6). |
| Modificador de atributo (ATK) | `ATK × folder.attack_percent / 100`, provisional 200 %: con 100 % la DEF pesa más que el ATK y un enemigo algo más fuerte siempre gana. La sonda de balance de `folder/balance_probe_tests.cpp` (`-tc='*balance probe*' --no-skip`) juega 40 duelos con semilla por enfrentamiento en Classic y en Folder; con 200 % las probabilidades de Folder quedan más cerca de las de Classic y un duelo dura cerca de un tercio. |
| Base elemental | Los pasos de ±20 % y el STAB se calculan sobre el poder de la técnica (§3.8 "daño base"). |
| Daño mínimo | 1 para un golpe, 0 para un golpe cubierto (§2.9 "puede quedar en 0" al cubrirse). |
| Probabilidad de acierto | Todo ataque que alcanza a un luchador acierta; la tirada defensiva es la cobertura. No se usa la evasión del original. |
| Descanso antes de vaciarse | La barra de defensa empieza a vaciarse `folder.bar_rest_frames` después del último golpe. |
| Base de ejecución y procesamiento | `folder.preparation_frames` (VEL) y `folder.processing_frames` (INT). |
| Enemigos | Conservan la IA de enemigos del original (que elige sus técnicas), pero reciben el daño, la cobertura y la barra de Folder. |
| Fuego amigo | Una técnica ofensiva de área golpea también al bando de quien la usa; ninguna otra lo hace; las de apoyo y las propias alcanzan a su usuario (§2.8). El catálogo puede quitárselo a una técnica ("no todos los ataques de área"): cuáles es una decisión de balance, así que aún ninguna entrada lo hace. Las técnicas de área de los partners golpean a los aliados del co-op; las de un enemigo, a los demás enemigos. Un enemigo alcanzado por el fuego de un aliado nunca se gira hacia él. |
| Áreas de efecto | Un movimiento de área de la tabla es un área alrededor de su usuario tan ancha como su distancia (el alcance desde el que lo usa el original), así el objetivo hacia el que se posicionó el partner queda dentro. El catálogo puede centrarla en el objetivo (`around_target`) o hacerla `global`. Todos los que están en el área se alcanzan a la vez, medido hasta el borde de su cuerpo; los scripts de efecto ya no añaden espectadores a una técnica de área. Líneas, conos y obstáculos dentro de un área no se modelan (§7.5 los deja por técnica). |
| Costes de MP | Las técnicas del entrenador cuestan lo que da `costo-mp.md` v0.2 (`folder.mp_costs`), a partir del poder, el estado y su probabilidad, el buff, el alcance y el área de la tabla (sección del catálogo, abajo). Una técnica potenciada conserva el descuento de la potenciación; un remate no cuesta nada (lo paga el medidor). Los enemigos conservan los costes de la tabla, porque conservan la IA del original, cuyas elecciones y reservas de MP están ajustadas a ellos. |
| Clases de alcance | Cuerpo a cuerpo es corto; una técnica a distancia es corta hasta 200 px, media hasta 500 y larga más allá; un área alrededor del usuario alcanza tan lejos como se extiende; el apoyo es personal. El catálogo original llama "Long" a todo movimiento a distancia (`tecnicas.md`), con lo que Spit Fire costaría 80; leer la distancia mantiene baratos los disparos baratos. Provisional. |
| Costes de efectos | Veneno 35, confusión 40, aturdimiento 50 (§4.1); el aplastamiento, que falta en la tabla del diseño, cuesta como un bloqueo de técnicas (60): un luchador aplastado solo puede lanzar el disparo del aplastado. Los estados de la tabla se leen como cortos y los buffs como largos, porque duran el combate. Provisional. |
| Costes de buffs | Por su subida más fuerte (ligera hasta 10 %, moderada hasta 20 %, fuerte más allá) y por si sube una estadística o varias, según las filas de §8.1. El ejemplo 4 del diseño suma dos costes de una estadística; se tomaron las filas de la tabla y la ambigüedad queda anotada aquí. |
| Descuento por cerebro | Folder quita el descuento de MP por cerebro del original: en el diseño la INT solo procesa (§6.5), y un descuento le daría un segundo papel. Un partner enfermo o herido sigue pagando el recargo del original: es su condición, no una estadística. |
| Bonus de bloqueo por distancia | La probabilidad de cobertura gana `cover_near_bonus_percent` puntos a quemarropa, bajando en línea recta hasta `cover_far_bonus_percent` a `cover_far_pixels` (provisional 10 → 0 en 800 px), medido desde donde se lanzó el ataque. Sigue la propuesta del diseño (cuanto más cerca, más fácil bloquear); dirección y tamaño son datos, así que el balance puede invertirlo sin código. Guardia sigue cubriendo siempre. |
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
| 10 | Interfaz: la tira da órdenes y posturas, `Change` elige el objetivo; la placa muestra la cola, la fase, la barra de defensa y la ruptura, el estado alterado y cómo terminó una orden no completada y por qué; la tira dice por qué no se puede ordenar una técnica; la placa se queda en su propia región en cada skin | Hecha |
| 11 | Datos de técnicas: ejecución y recuperación por tramo de MP con entradas por técnica; área, funciones y fuego amigo por técnica (el catálogo) | Hecha |
| 12 | Balance: escala de atributos (provisional, de la sonda), bonus de bloqueo por distancia y costes (`costo-mp.md`) aplicados, todos provisionales | Hecha (valores pendientes de balance) |
| 13 | Volver a Classic desde el juego en una partida de Folder Mode: la opción `battle_system` | Hecha |
| 14 | Objetivos: Cambiar elige el enemigo, cada orden lo conserva, el partner nunca cambia solo, y la placa nombra el enemigo de cada orden | Hecha |
| 15 | Dificultad del co-op en Folder: el escalado existente se aplica sin cambios, y cómo se aplicaría la alternativa del +20 % (abajo) | Hecha (documentada, con test) |

## Catálogo de técnicas

Folder juega cada técnica con un perfil (`folder::Technique`, `folder::techniqueOf` en
`folder/formulas/technique_catalog.cpp`): sus funciones, área, fuego amigo, clase de alcance y coste de MP. Se lee de la
tabla de movimientos con las reglas de las decisiones de arriba, y `folder.catalog` del ruleset cambia cualquier parte
para una técnica por su id:

```yaml
catalog:
  shot: {area: around_target, area_pixels: 250, functions: [offensive, control], friendly_fire: false, mp_cost: 12}
```

Las claves son `functions` (offensive, defensive, support, control), `area` (single, around_self, around_target,
global), `area_pixels`, `friendly_fire`, `effect_duration` (short, medium, long) y `mp_cost`. Una técnica nueva es una
fila de la tabla de movimientos y, solo donde la lectura no acierte, una entrada del catálogo: sin código por técnica.

La interfaz lo lee con `Battle::folderTechnique(slot, side)`; `Battle::partnerMoveCost` ya da el coste de Folder en un
combate Folder, así que el coste de MP de la tira lo sigue. Las 56 técnicas del original se leen así (los enemigos
conservan el MP del original):

| Especialidad | Técnica | Funciones | Estado | Alcance | Área (px) | Fuego amigo | MP original | MP Folder |
|---|---|---|---|---|---|---|---|---|
| Fire | Fire Tower | ofe+ctl | Aturdimiento 8 % | media | individual | no | 81 | 81 |
| Fire | Prominence Beam | ofe+ctl | Aplastamiento 3 % | larga | individual | no | 183 | 195 |
| Fire | Spit Fire | ofe | — | media | individual | no | 30 | 50 |
| Fire | Red Inferno | ofe | — | larga | alrededor 600 | sí | 171 | 163 |
| Fire | Magma Bomb | ofe+ctl | Confusión 4 % | larga | individual | no | 132 | 146 |
| Fire | Heat Laser | ofe+ctl | Aplastamiento 32 % | larga | alrededor 600 | sí | 105 | 145 |
| Fire | Inifinity Burn | ofe+ctl | Aturdimiento 11 % | media | alrededor 400 | sí | 264 | 222 |
| Fire | Meltdown | ofe+ctl | Aturdimiento 24 % | larga | alrededor 600 | sí | 318 | 232 |
| Air | Thunder Justice | ofe+ctl | Aturdimiento 32 % | larga | individual | no | 330 | 252 |
| Air | Spinning Shot | ofe | — | larga | individual | no | 150 | 177 |
| Air | Electric Cloud | ofe+ctl | Aturdimiento 9 % | larga | individual | no | 69 | 101 |
| Air | Megalo Spark | ofe+ctl | Aturdimiento 23 % | media | individual | no | 174 | 157 |
| Air | Static Elect | ofe+ctl | Aturdimiento 15 % | corta | individual | no | 45 | 33 |
| Air | Wind Cutter | ofe | — | larga | individual | no | 93 | 114 |
| Air | Confused Storm | ofe+ctl | Confusión 60 % | media | alrededor 400 | sí | 216 | 162 |
| Air | Hurricane | ofe+ctl | Confusión 13 % | media | alrededor 400 | sí | 255 | 185 |
| Ice | Giga Freeze | ofe+ctl | Aturdimiento 10 % | corta | individual | no | 120 | 85 |
| Ice | Ice Statue | ofe+ctl | Aturdimiento 38 % | larga | individual | no | 186 | 207 |
| Ice | Winter Blast | ofe+ctl | Aturdimiento 15 % | larga | alrededor 600 | sí | 165 | 144 |
| Ice | Ice Needle | ofe+ctl | Aturdimiento 5 % | larga | individual | no | 78 | 101 |
| Ice | Water Blit | ofe | — | larga | individual | no | 102 | 124 |
| Ice | Aqua Magic | apo | — | personal | individual | no | 36 | 68 |
| Ice | Aurora Freeze | ofe+ctl | Aplastamiento 24 % | larga | alrededor 600 | sí | 258 | 244 |
| Ice | Tear Drop | ofe+ctl | Aplastamiento 46 % | media | individual | no | 42 | 76 |
| Mech | Power Crane | ofe | — | larga | individual | no | 126 | 128 |
| Mech | All Range Beam | ofe | — | larga | alrededor 600 | sí | 330 | 272 |
| Mech | Metal Sprinter | ofe | — | media | alrededor 500 | sí | 165 | 115 |
| Mech | Pulse Laser | ofe | — | larga | individual | no | 168 | 177 |
| Mech | Delete Program | ofe+ctl | Aplastamiento 50 % | larga | individual | no | 219 | 219 |
| Mech | DG Dimension | ofe | — | media | alrededor 400 | sí | 420 | 287 |
| Mech | Full Potential | apo | — | personal | individual | no | 99 | 150 |
| Mech | Reverse Prog | ofe+ctl | Aplastamiento 12 % | larga | individual | no | 297 | 144 |
| Earth | Veneno Powder | ofe+ctl | Veneno 62 % | larga | alrededor 600 | sí | 171 | 157 |
| Earth | Bug | ofe+ctl | Aplastamiento 35 % | larga | individual | no | 354 | 231 |
| Earth | Mass Morph | apo | — | personal | individual | no | 30 | 105 |
| Earth | Insect Plague | ofe+ctl | Veneno 90 % | larga | individual | no | 96 | 109 |
| Earth | Charm Perfume | ofe+ctl | Confusión 57 % | larga | alrededor 600 | sí | 210 | 177 |
| Earth | Veneno Claw | ofe+ctl | Veneno 40 % | corta | individual | no | 51 | 33 |
| Earth | Danger Sting | ofe+ctl | Aplastamiento 26 % | corta | individual | no | 102 | 63 |
| Earth | Green Trap | ofe+ctl | Aturdimiento 55 % | larga | individual | no | 147 | 181 |
| Battle | Tremar | ofe | — | larga | alrededor 600 | sí | 168 | 154 |
| Battle | Muscle Charge | apo | — | personal | individual | no | 66 | 90 |
| Battle | War Cry | apo | — | personal | individual | no | 42 | 68 |
| Battle | Sonic Jab | ofe | — | corta | individual | no | 18 | 16 |
| Battle | Dynamite Kick | ofe+ctl | Aturdimiento 3 % | corta | individual | no | 99 | 60 |
| Battle | Counter | off+ctl+def | Confusión 10 % | corta | individual | no | 165 | 90 |
| Battle | Megaton Punch | ofe+ctl | Aturdimiento 9 % | corta | individual | no | 186 | 101 |
| Battle | Buster Dive | ofe+ctl | Confusión 14 % | larga | individual | no | 258 | 216 |
| Filth | Odor Spray | ofe+ctl | Aturdimiento 10 % | corta | individual | no | 75 | 32 |
| Filth | Poop Spd Toss | ofe+ctl | Veneno 11 % | larga | individual | no | 96 | 101 |
| Filth | Big Poop Toss | ofe+ctl | Confusión 17 % | media | individual | no | 192 | 101 |
| Filth | Big Rnd Toss | ofe+ctl | Confusión 20 % | media | alrededor 400 | sí | 282 | 142 |
| Filth | Poop Rnd Toss | ofe+ctl | Veneno 9 % | media | alrededor 400 | sí | 120 | 96 |
| Filth | Rnd Spd Toss | ofe+ctl | Veneno 10 % | media | alrededor 500 | sí | 216 | 111 |
| Filth | Horizontal Kick | ofe | — | corta | individual | no | 24 | 16 |
| Filth | Ult Poop Hell | ofe+ctl | Aplastamiento 83 % | larga | alrededor 600 | sí | 333 | 250 |

En total, 8928 MP del original pasan a 7656: los disparos individuales baratos se encarecen (Spit Fire 30 → 50, Tear
Drop 42 → 76), las técnicas más caras se abaratan (DG Dimension 420 → 287, Bug 354 → 231) y los buffs se encarecen (Mass
Morph 30 → 105). Los costes son los de costo-mp.md v0.2 y están pendientes de su balance (§11 allí).

## Dificultad del co-op en Folder

El escalado del co-op que existe ([dificultad del co-op](coop-difficulty.md) §5, §9) escala el HP, el MP, la ofensiva y
la meta del remate máximos de los enemigos al empezar el combate, sea cual sea el sistema, así que un combate Folder
recibe los mismos enemigos (test "co-op difficulty scales a folder battle's enemies as it scales a classic one's"). En
Folder la subida de ofensiva pesa algo más que en Classic, porque el ATK entra en el daño de forma aditiva a
`attack_percent` (200 %): +10 % de ATK es cerca de +20 % de la parte del ATK en un golpe. El escalado de MP alimenta la
IA y los costes originales de los enemigos, como en Classic.

Lo que Folder añade al co-op es el fuego amigo: una técnica ofensiva de área de un partner golpea a los partners que
estén a su lado. Las tablas de escalado no lo cuentan; el riesgo es que más partners juntos se hagan más daño, y las
posturas (Distance, Hold) y `friendly_fire: false` del catálogo pueden responderlo.

La alternativa plana de **+20 % por jugador** (una propuesta para coop-difficulty.md en `main`, no construida) se
aplicaría a Folder así, si se elige:

- **HP:** la misma columna (120/140/160 %), al empezar el combate, como hoy.
- **Daño:** multiplicar el daño de Folder de un atacante enemigo (`damageOf` en `folder/defense/defense.cpp`) por el
  factor después de la DEF y la cobertura y antes del tope de daño. La fórmula de Folder resta la DEF de forma plana,
  así que escalar el ATK daría mucho más de +20 % contra defensores débiles y menos contra fuertes; escalar el resultado
  mantiene una regla que el jugador puede leer. Un golpe cubierto que queda en 0 sigue en 0.
- **Barra de defensa:** se llena con el daño escalado, así que con más jugadores la defensa de un partner se rompe
  antes. Es la presión buscada; la capacidad de la barra (`6 × DEF`) no se escala.
- **Tirada de unión y alcance de contacto:** reglas del campo, iguales en ambos sistemas.

Nada de la alternativa se construye en Folder hasta que se elija ese modelo.

## Balance pendiente

- La escala del modificador de atributo (`attack_percent`), ajustada solo con las técnicas de prueba; las técnicas reales pueden moverla.
- El bonus de bloqueo por distancia (§7.4): dirección y tamaño (`cover_near_bonus_percent`, `cover_far_bonus_percent`,
  `cover_far_pixels`).
- Los tramos de tiempos y el tiempo de los remates (`folder.timings`, `folder.finisher_timing`), y las entradas por técnica.
- Costes de MP: los valores de costo-mp.md v0.2 (`folder.mp_costs`), los umbrales de alcance y área, el coste del
  aplastamiento y la duración de los buffs.
- Qué técnicas de área no deben golpear a los aliados (`catalog: friendly_fire: false`), y áreas centradas en el objetivo.
- Dificultad del co-op con Folder: las tablas existentes están ajustadas sobre Classic; el factor de daño de la
  alternativa del +20 % va sobre el resultado del daño de Folder.

## Huecos restantes

- **Guardia** sigue al aliado más cercano cuando lo hay y si no se queda quieta; nunca se interpone entre un aliado y un
  enemigo (el diseño deja abiertas las tolerancias, §2.9 "Pendiente").
- **El objetivo de la placa** sigue al enemigo de la última orden ejecutada: ejecutar una orden apunta al partner hacia
  su enemigo, así que tras repartir órdenes la siguiente orden de la tira apunta a ese enemigo, no al último que eligió
  Cambiar.
- **Abandono:** el posicionamiento abandona una orden tras `give_up_frames` sin llegar al alcance; una preparación que
  termina fuera de alcance vuelve a posicionarse con la cuenta a cero, así que un enemigo que sigue escapando se
  persigue hasta que la orden se reemplaza o se cancela (el "la reintenta" del diseño).
- **Las acciones Order y Stance** existen en el protocolo pero el cliente del invitado envía su tira como comandos (ver
  Co-op).
- **Interfaz:** las skins aún no dibujan el área, las funciones ni el fuego amigo de una técnica; la API los da.
