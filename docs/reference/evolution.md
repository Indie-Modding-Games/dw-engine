# Ficha de referencia: evolución

Cómo cambiaba de forma el partner en el juego original, según el decomp. Es el paso 1 del
[ciclo de diseño](../conventions/design-cycle.md): describe el original, no lo que hará el mod.

Marcas: **(comprobado)** quiere decir que se leyó en el código del decomp; **(suposición)** quiere decir que es una
interpretación nuestra que falta confirmar. Los identificadores de Digimon son índices de la tabla de parámetros
(`DIGIMON_DATA`); el nombre entre paréntesis sale del orden de esa tabla en `src/main/main.c`.

## Qué hace

El partner nace como bebé y pasa por cinco niveles. Cada cierto número de horas de juego el juego mira qué formas
siguientes tiene disponibles, puntúa cada una según cómo se ha criado al partner (stats, peso, errores de cuidado y un
requisito extra) y, si alguna aprueba, evoluciona a la mejor. Además hay evoluciones especiales que dependen de
eventos (dormir en un sitio, regañar en un estado concreto, la suciedad acumulada) y objetos que fuerzan una
evolución. Al evolucionar, los stats se mezclan con los valores de referencia de la forma nueva.

## Reglas

### Niveles y contador de evolución

- Hay cinco niveles: 1 bebé, 2 entrenamiento, 3 novato, 4 campeón, 5 definitivo. El número sale del campo `level`
  de la tabla de parámetros **(comprobado)**; los nombres de los niveles son los habituales en la comunidad
  **(suposición)**.
- El contador de evolución (`evoTimer`) cuenta horas de juego desde el último cambio de nivel. Sube una unidad por
  cada hora del reloj (`clock.c`), y también al dormir (suma las horas de sueño), al saltar horas y en cada
  entrenamiento del gimnasio (`trn_reward.c`, `trn2_hud.c`) **(comprobado)**.
- El contador vuelve a 0 solo si la evolución cambia de nivel; una evolución dentro del mismo nivel lo conserva
  (`EVL_applyEvolution`) **(comprobado)**.

### Cuándo se comprueba

`tickConditions` (en `partner_impl.c`) mira la evolución mientras el juego corre, el partner está en estado normal,
el tamer está libre y no hay una transición de mapa en marcha **(comprobado)**. Que la comprobación sea continua y no
una vez por hora es lo que se lee en el código; la habilita `DAYTIME_TRANSITION_FRAME` con 25 o más **(comprobado)**,
y que eso signifique "sin fundido de paleta del cambio de hora" es **(suposición)**. La barra de virus se mira
después y no espera a que el tamer esté libre **(comprobado)**.

| Nivel actual | Umbral del contador | Función que elige el destino |
|---|---|---|
| 1 bebé | 6 horas | `getFreshEvolutionTarget` |
| 2 entrenamiento | 24 horas | `getInTrainingEvolutionTarget` |
| 3 novato | 72 horas | `getRookieEvolutionTarget` |
| 4 campeón | 144 horas | `getChampionEvolutionTarget` |
| 5 definitivo | — (no evoluciona de forma natural) | — |

Todo **(comprobado)**. Si la función no encuentra destino, no pasa nada y se vuelve a intentar en la siguiente
comprobación.

### Bebé → entrenamiento

El destino es fijo: cada uno de los cuatro bebés tiene una única forma siguiente, escrita en el propio código, sin
mirar la crianza (`getFreshEvolutionTarget`) **(comprobado)**. La función pone además el contador a 0 cada vez
que se la llama, aunque la forma no sea uno de los cuatro bebés **(comprobado)**; ver rarezas.

### Puntuación de un candidato

Cada forma tiene hasta seis destinos posibles (tabla `EVOLUTION_PATHS`). Para cada destino,
`calculateRequirementScore` suma puntos **(comprobado)**:

1. **Errores de cuidado:** 1 punto si los errores son *al menos* el valor del requisito. Si el destino lleva la marca
   "máximo" (bit `0x10` de `flags`), el punto es para *como mucho* ese valor.
2. **Peso:** 1 punto si el peso está dentro de ±5 del peso pedido.
3. **Stats:**
   - Si el destino es de nivel novato, 1 punto si el stat más alto del partner es uno de los que el destino marca
     como favorecidos. Para comparar, HP y MP se dividen entre 10. En caso de empate gana el último stat en el orden
     HP, MP, ataque, defensa, velocidad, inteligencia.
   - En los demás niveles, 1 punto si *todos* los stats llegan al mínimo pedido (HP y MP, también divididos entre 10).
4. **Bonus:** 1 punto si se cumple *cualquiera* de estos: venir de una forma concreta, disciplina mínima, felicidad
   mínima, batallas (mínimo, o máximo si `flags` tiene el bit `0x01`), o un número mínimo de técnicas aprendidas.
   Las técnicas aprendidas se cuentan como bits activos en una máscara de 64 (`getNumMasteredMoves`).

Un destino aprueba con **3 puntos o más** de los 4 posibles **(comprobado)**. Un requisito sin valor (−1) se trata
como cumplido en los stats y se ignora en el bonus **(comprobado)**.

**Preferencia por lo nuevo:** cuando ya hay un mejor candidato provisional, si el destino evaluado ya se crió alguna
vez y el provisional no, el destino queda a 0 puntos; en el caso contrario recibe 1 punto extra. El registro de
"ya criado" se guarda al evolucionar o renacer (`setDigimonRaised`) **(comprobado)**.

### Cómo se elige entre varios candidatos

- **Entrenamiento → novato:** gana el candidato con más puntos; en empate, el primero de la lista
  (`getInTrainingEvolutionTarget`) **(comprobado)**.
- **Novato → campeón y campeón → definitivo:** entre los que aprueban, gana el que tiene la media más alta de los
  stats que su requisito pide (HP y MP divididos entre 10) (`getRookieEvolutionTarget`,
  `getChampionEvolutionTarget`) **(comprobado)**. Ver la rareza del acumulador más abajo.
- **Forma de descarte:** si un novato no tiene ningún candidato y su contador llega a **96 horas**, evoluciona al
  Digimon 11 (Numemon) **(comprobado)**. Los campeones sin candidato se quedan como están **(comprobado)**.

### Evoluciones especiales

Todas en `handleSpecialEvolutions`, salvo la de la suciedad. Solo se aplican si el tamer está libre
**(comprobado)**.

- **Al despertar** (modo 2, desde `partnerTickSleep`):
  - Con un 30 % de probabilidad se prueban tres casos fijos de campeón a otra forma, que piden disciplina 100 y
    además, según el caso, felicidad 100 y cansancio 0, 50 batallas o más, o defensa de 500 o más **(comprobado)**.
  - Con un 50 % de probabilidad, una forma de entrenamiento que despierta en la pantalla 1 evoluciona al Digimon 32
    (Kunemon) **(comprobado)**. Que la pantalla 1 sea la de la cama de ese personaje es **(suposición)**.
- **Tras elogiar o regañar** (modo 3, desde `partnerTickPraiseScold`), y también cuando un campeón llega justo a las
  360 horas (`tickConditions`, sin aviso de regaño, así que el caso de Nanimon no aplica); en ese caso el resultado
  sustituye al de la elección normal de esa misma comprobación, ver rarezas **(comprobado)**:
  - Dos campeones concretos con el contador exactamente en 200 horas: 30 % de evolucionar a otra forma concreta
    **(comprobado)**.
  - Cualquier campeón con 240 horas o más: 50 % de evolucionar al Digimon 28 (Vademon) **(comprobado)**.
  - Si se regaña a un novato que tenía disciplina 0 y felicidad −100, se activa un aviso y evoluciona al Digimon 53
    (Nanimon) (`handlePraiseScold`) **(comprobado)**.
- **Suciedad:** cada caca hecha en el suelo (`createPoopPile`) suma 1 a la barra de virus, salvo si ya es el
  Digimon 39 (Sukamon). Con la barra en **16**, el partner evoluciona a Sukamon; antes se guardan sus stats y su forma
  anterior, y la barra vuelve a 0 (`tickConditions`) **(comprobado)**. Un evento de script puede devolverlo a la
  forma guardada (`callDigimonRoutine`, rutina 2) **(comprobado)**; qué evento del juego lo hace es
  **(suposición)**. La rutina 10 hace evolucionar al partner a la forma que el script deja en `PSTAT_254`
  **(comprobado)**.

### Objetos de evolución

`handleEvoItems` (en `item.c`) **(comprobado)**:

- Los objetos de un rango fijo usan la tabla `EVOLUTION_ITEM_TARGET` (objeto → destino). Solo funcionan si el
  destino es exactamente un nivel por encima del partner.
- Tres objetos especiales llevan a tres formas fijas sin comprobar el nivel.
- Usar un objeto de evolución marca `HAS_USED_EVOITEM`. Con esa marca, la evolución **no aplica ganancias de stats ni
  el bonus de vida**. Ver rarezas.
- Antes, el partner puede rechazar el objeto (`partnerWillRefuseItem` en `partner_impl.c`): acepta un objeto de
  evolución cuyo destino está 1 o 3 niveles por encima o 2 por debajo, una regla distinta de la de `handleEvoItems`
  **(comprobado)**; qué casos de juego cubre es **(suposición)**. Esa regla es del sistema de objetos, no de la
  evolución.

### Ganancias de stats al evolucionar

`EVL_applyEvolution` usa la fila del destino en `EVOLUTION_STATS_GAINS` **(comprobado)**:

- **Caso normal:** para cada stat, si el partner ya tiene al menos el valor de referencia, suma una décima parte de
  ese valor; si no, el stat pasa a ser la media entre el actual y el de referencia.
- **Casos de escala:** si el destino es bebé o de entrenamiento, si es uno de cuatro Digimon concretos (6, 11, 39 y
  53) o si el partner actual es Sukamon, los stats se multiplican por un factor en décimas que se lee del campo de
  inteligencia de la fila (`EVL_scaleBaseStats`). Si el partner era Sukamon, en cambio, cada stat se reconstruye a
  partir de los stats guardados al convertirse: guardado + (actual − guardado / 2) / 2 **(comprobado)**. Por qué
  el Digimon 6 está en esa lista no está claro **(suposición: un caso de diseño o un error)**.
- **Topes:** HP y MP a 9999; ataque, defensa, velocidad e inteligencia a 999 (`EVL_clampBaseStats`)
  **(comprobado)**.
  - En `dwe`, los topes son los del partner (`care.limits` de `care.yaml`): la evolución da el cambio de cada stat
    sin topar y el partner lo suma con `addStatGains`, que además deja **cada ganancia en 999 como mucho**
    (desviación aprobada, 2026-10-20: en el original solo la primera ganancia de cada sesión de entrenamiento tiene
    ese tope, y la evolución solo para en los topes de arriba).
  - Al salir de Sukamon, los stats reconstruidos desde la copia se restauran sin pasar por `addStatGains`, así que
    no tienen el tope de 999 por ganancia, pero sí el tope de cada stat (9999 HP y MP, 999 el resto), como hace el
    original con `EVL_clampBaseStats` (decidido por el responsable de diseño, 2026-10-20). Con una copia y unos stats
    altos la reconstrucción da más del tope (9999 y 9999 dan 12499) y se queda en 9999.
  - La copia de Sukamon es del partner: sus stats en `virusBackup` del estado del partner (`sukaBackup*`) y la forma
    en la variable de script `PSTAT_SUKAMON_BACKUP_DIGIMON` (`PSTAT_5`), como en el original.
- **Además, siempre:** el peso pasa al peso por defecto de la forma nueva (`RAISE_DATA`), y los errores de cuidado y
  las batallas vuelven a 0 **(comprobado)**.
- **Definitivo:** al llegar a nivel 5 sin objeto, la vida restante sube **96 horas** **(comprobado)**.
- **Nivel del tamer:** al llegar a campeón hay un 20 % de subir un nivel de tamer, y al llegar a definitivo un 100 %,
  pero solo si la marca `IS_SCRIPT_PAUSED` está activa **(comprobado)**. Qué representa esa marca en este momento es
  **(suposición)**.

### Técnicas al evolucionar

`EVL_applyEvolution` **(comprobado)**:

- La forma nueva aprende la técnica más débil (menor potencia distinta de 0) de su lista que cae en el bloque de su
  primera especialidad. Cada especialidad ocupa un bloque de 9 técnicas en la tabla de técnicas. Las formas de
  entrenamiento no tienen ninguna técnica en su bloque (su única técnica está fuera de todos): el código pasa la marca
  de fin de lista a `learnMove` y no aprenden nada **(comprobado)**.
- Las ranuras de técnicas equipadas se reinician: la primera queda con esa técnica (o con el ataque básico si la
  forma es bebé o de entrenamiento) y la cuarta con la última técnica de la lista que esté en un rango alto de
  índices. Que ese rango sea el de las técnicas exclusivas o de remate es **(suposición)**.

### Renacer

Al morir, `reincarnatePartner` guarda los stats del partner muerto (para la herencia), elige el bebé según el huevo
escogido (`REINCARNATE_BABY_TYPE`), pone las vidas a 3 y marca el bebé como criado **(comprobado)**. El huevo sale
al azar entre los cuatro (`rand() % 4` en `dooa.c`), y el bebé queda con el ataque básico en la primera ranura y las
demás vacías **(comprobado)**. La herencia de
stats y el resto del ciclo de vida están en la [ficha de cuidado](care.md).

## Datos

Tablas del ejecutable principal. `dwx` las extraerá a YAML desde el disco del usuario; aquí solo se describe su forma.

| Tabla | Entrada | Entradas | Campos |
|---|---|---|---|
| `EVOLUTION_REQS` | 28 bytes | 63 | Forma de origen para el bonus; mínimos de HP, MP, ataque, defensa, velocidad e inteligencia (en novato, 1 marca un stat favorecido); errores de cuidado; peso; disciplina; felicidad; batallas; técnicas; `flags` (bit 0: batallas como máximo; bit 4: errores como máximo); relleno. −1 significa "sin requisito". Indexada por la forma destino. |
| `EVOLUTION_PATHS` | 11 bytes | 62 | Hasta 5 formas de origen y hasta 6 de destino (−1 vacío). Indexada por forma − 1. También la usa la carta de evolución del menú. La fila de un definitivo (forma 59) tiene un −2 y un 0 en los destinos; como los definitivos no evolucionan, la elección nunca la lee **(comprobado)**. |
| `EVOLUTION_STATS_GAINS` | 14 bytes | 66 | Valores de referencia de HP, MP, ataque, defensa, velocidad e inteligencia, y la forma real que se carga. Indexada por la forma destino. |
| `EVOLUTION_ITEM_TARGET` | 1 byte | 44 | Forma destino de cada objeto de evolución, indexada por objeto − primer objeto de evolución. `dwx` la extrae en `evolves_to` de `data/item_effects`; la lista opcional `item_targets` de `data/evolution` la sustituye objeto a objeto, o la suple si el ruleset no nombra `item_effects`. |
| `DIGIMON_DATA` | 52 bytes | 180 | Parámetros de cada Digimon: nombre (no en la versión europea), huesos, radio, altura, tipo, nivel, tres especialidades, objeto que suelta y su probabilidad, 16 técnicas posibles. |
| `RAISE_DATA` | — | — | Datos de crianza por Digimon; aquí solo se usa el peso por defecto. Ver la [ficha de cuidado](care.md). |
| `REINCARNATE_BABY_TYPE` | 2 bytes | 4 | El bebé que sale de cada huevo al renacer. |

Tamaños de entrada calculados a partir de los `struct` de `include/dw/evl.h` y `params.h` y de los tamaños de
`config/us/symbols.txt` **(comprobado)**.

## Rarezas

- **El objeto de evolución "contagia" la partida.** `HAS_USED_EVOITEM` solo se pone a 0 al arrancar el juego
  (`initializeStatusObjects`, llamada desde `main`). Tras usar un objeto de evolución, todas las evoluciones
  naturales siguientes de esa sesión se quedan sin ganancias de stats ni bonus de vida **(comprobado)**. Si cargar
  una partida lo arregla no está claro: la marca no parece formar parte de la partida guardada **(suposición)**.
- **Acumulador sin reiniciar.** Al elegir entre candidatos de novato o campeón, la suma y la cuenta de stats solo se
  reinician cuando un candidato pasa a ser el mejor. Si un candidato aprueba pero no mejora, su suma se arrastra a la
  media del siguiente **(comprobado)**. Si un destino no pidiera ningún stat, la media dividiría entre 0
  **(suposición: no sabemos si la tabla tiene algún caso así)**.
- **Bebé sin destino.** `getFreshEvolutionTarget` devuelve un valor sin inicializar si la forma no es uno de los
  cuatro bebés; el propio decomp lo señala **(comprobado)**. Aun así pone el contador a 0, así que un bebé sin
  destino vuelve a empezar su cuenta **(comprobado)**. En `dwe` se mantiene (`unlisted_baby_restarts_timer: true`): decidido
  por el responsable de diseño (2026-10-20): fiel al original.
- **Las 360 horas pisan la elección normal.** Cuando un campeón llega justo a 360 horas, `tickConditions` llama a
  las especiales del modo 3 después de la elección normal y guarda su resultado encima: si ninguna especial sale,
  tampoco evoluciona aunque la elección normal hubiera encontrado destino **(comprobado)**. En `dwe` se mantiene
  (`timer_special_overrides_normal: true`): decidido por el responsable de diseño (2026-10-20): fiel al original.
- **Empate de stats en novato.** Gana el último stat empatado, no el primero **(comprobado)**.
- **Regaño reforzado.** En `handlePraiseScold`, el regaño justo después de un rechazo de comida o de una caca
  en el suelo no inicializa el cambio de felicidad **(comprobado)**; el valor que acaba sumando es
  **(suposición)**.

## Dónde está en el decomp

| Qué | Dónde |
|---|---|
| Comprobación periódica, umbrales, barra de virus | `src/main/partner_impl.c`: `tickConditions` |
| Elección de destino y puntuación | `src/main/evolution.c`: `getFreshEvolutionTarget`, `getInTrainingEvolutionTarget`, `getRookieEvolutionTarget`, `getChampionEvolutionTarget`, `calculateRequirementScore`, `getNumMasteredMoves` |
| Evoluciones especiales | `src/main/evolution.c`: `handleSpecialEvolutions`; llamadas desde `src/main/partner.c`: `partnerTickSleep`, `partnerTickPraiseScold` |
| Aviso de Nanimon | `src/main/partner_impl.c`: `handlePraiseScold` |
| Caca y barra de virus | `src/main/partner_impl.c`: `createPoopPile` |
| Vuelta de Sukamon, evolución por script | `src/main/partner.c`: `callDigimonRoutine` (rutinas 2 y 10) |
| Objetos de evolución | `src/main/item.c`: `handleEvoItems`; rechazo en `src/main/partner_impl.c`: `partnerWillRefuseItem` |
| Aplicar la evolución (stats, técnicas, vida) | `src/evl/evl.c`: `EVL_applyEvolution`, `EVL_scaleBaseStats`, `EVL_clampBaseStats` |
| Contador por horas | `src/main/clock.c`; `src/main/partner_impl.c`: `skipHours`, `handleSleeping` |
| Renacer | `src/main/evolution.c`: `reincarnatePartner`; `src/dooa/dooa.c` |
| Estructuras | `include/dw/evl.h`, `include/dw/params.h` |
