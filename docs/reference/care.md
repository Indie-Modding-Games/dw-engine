# Ficha de referencia: cuidado

Cómo se criaba al partner en el juego original, según el decomp. Es el paso 1 del
[ciclo de diseño](../conventions/design-cycle.md): describe el original, no lo que hará el mod.

Marcas: **(comprobado)** quiere decir que se leyó en el código del decomp; **(suposición)** quiere decir que es una
interpretación nuestra que falta confirmar. Salvo que se diga otra cosa, todo está en `src/main/partner_impl.c`.

## Qué hace

El partner vive en un reloj propio. Tiene hambre a ciertas horas, sueño a otras, necesita ir al baño, se cansa al
entrenar y pelear, y puede enfermar o lesionarse. Cada necesidad desatendida le baja la felicidad o la disciplina, y
algunas cuentan como **errores de cuidado**, que deciden en qué evoluciona. Tiene una vida limitada en horas: cuando
se acaba, muere y renace como bebé con parte de sus stats.

## Reglas

### El reloj

- Con la velocidad normal, 20 fotogramas son un minuto de juego, 1200 una hora y 28 800 un día (`tickGameClock`, en
  `clock.c`) **(comprobado)**. Que el juego avance unos 20 fotogramas por segundo, y por tanto una hora de juego dure
  un minuto real, es **(suposición)**.
- Cada hora: el contador de evolución sube 1 y la vida restante baja 1. Cada medianoche: la edad sube 1 día
  (`clock.c`) **(comprobado)**.
- Las reglas de cuidado se evalúan en `tickConditions`, solo con el partner en estado normal y el reloj en marcha.
  Casi todas usan intervalos fijos de fotogramas **(comprobado)**.

### Hambre

- **Horario** (`setFoodTimer`): los bebés tienen hambre cada 2 horas y los de entrenamiento cada 3. Desde novato, cada
  especie tiene una lista de hasta 8 horas de comida en `RAISE_DATA` **(comprobado)**.
- **Ventana de hambre** (`tickHungerMechanics`): al llegar la hora, el partner pide comida durante 1,5 horas. Si se
  cierra la ventana sin que su energía llegue al umbral de su especie, suma **un error de cuidado** **(comprobado)**.
- **Energía:** baja cada hora según el gasto de la especie. Con la energía a 0, el peso baja 1 cada 10 minutos
  **(comprobado)**.
- **Cansancio y hambre:** con el cansancio alto durante un tiempo, el hambre llega antes (3 minutos más por hora)
  **(comprobado)**.
- **Comer** (`partnerHandleFoodFeed`, `handleFood` en `item.c`):
  - Si tenía hambre y la energía llega al umbral: felicidad +5, disciplina +1 y se quita el hambre. La siguiente hora
    de comida se busca a partir de la última que se fijó, no de la hora actual (`setFoodTimer` de la versión
    americana) **(comprobado)**.
  - Si no tenía hambre (sobrealimentar): felicidad −3, disciplina −2.
  - La comida favorita de la especie da un 40 % más de energía y +2 de felicidad.
  - La energía tiene un tope por especie.
  - Todo **(comprobado)**.
- **Rechazar comida u objetos** (`partnerWillRefuseItem`) **(comprobado)**:
  - Algunos objetos se rechazan con una probabilidad que baja con la disciplina: aleatorio(100) < 110 − (disciplina
    + 10..19).
  - Si no tiene hambre, rechaza su comida favorita un 20 % de las veces.
  - Después de un rechazo, regañar da más disciplina (ver felicidad y disciplina).
  - Solo cuentan para el hambre las comidas; los demás objetos que pasan por `handleFood` cambian stats y necesidades
    pero no quitan el hambre ni cuentan como sobrealimentar (`partnerHandleFoodFeed`) **(comprobado)**.

### Sueño

- **Horario** (`setSleepTimes`, `SLEEP_PATTERN`) **(comprobado)**:
  - Desde novato, cada especie usa uno de varios patrones de 15 horas despierto y 9 dormido, elegido en `RAISE_DATA`.
  - Los bebés y los de entrenamiento duermen a partir de la hora actual: 3 o 7 horas despiertos y luego una siesta
    corta.
- **El cansancio adelanta el sueño** (`tickSleepMechanics`): cada minuto se acumula cansancio × 3 / 10; cada 180
  puntos se recortan 10 minutos de vigilia **(comprobado)**.
- **Con sueño y sin acostarse** **(comprobado)**:
  - Felicidad y disciplina bajan: −1/−1 cada 10 minutos (bebé), cada 15 minutos (entrenamiento), o −2/−4 por hora
    (resto).
  - Cada hora sube un contador de horas sin dormir, que lleva a la enfermedad (ver más abajo).
  - Si llega la hora de despertar sin haber dormido: **un error de cuidado**, posible bajada del nivel del tamer y el
    horario se reinicia.
- **Dormir** (`partnerTickSleep`, `handleSleeping`, `sleepRegen`) **(comprobado)**:
  - El reloj salta a la hora de despertar. La vida restante baja las horas dormidas y el contador de evolución sube
    las horas de sueño por defecto.
  - Se quita el hambre y, si tenía ganas de ir al baño, deja una caca. Esa caca **no** cuenta como error de cuidado
    (`handleSleeping`) **(comprobado)**.
  - Si estaba enfermo: felicidad −20, disciplina −10, cansancio +10. Si estaba lesionado: −10, −5, +5.
  - Recuperación según la fracción de sueño completada: HP y MP entre el 70 y el 79 % del máximo, el cansancio baja
    entre el 80 y el 99 %, y el peso baja una décima parte del peso por defecto.
  - La almohada (un objeto) mejora la recuperación un 20 %; dormir en una zona que le gusta, un 20 % más, y en una
    que no, un 20 % menos.
  - Durante la primera hora tras despertar no puede morir ni empeorar de enfermedad (`setImmortalHour`).
  - Al despertar se prueban las evoluciones especiales (ver la [ficha de evolución](evolution.md)).
- No se encontró una regla aparte para "dormir fuera de la cama"; la zona es lo único que cuenta
  **(suposición: puede estar en scripts)**.

### Baño y caca

- **Temporizador** (`tickPoopingMechanic`): cada especie tiene su intervalo en `RAISE_DATA`. Baja 1 cada 10 minutos y
  1 por cada comida **(comprobado)**.
- **Aviso:** al llegar a 0, el partner tiene ganas y hay un margen de (disciplina + 20) × 1200 / 100 fotogramas, es
  decir, entre 12 y 72 minutos **(comprobado)**.
- **En el suelo** (`tickWildPoop`, `handleWildPoop`, `createPoopPile`) **(comprobado)**:
  - **Un error de cuidado**, felicidad −10, disciplina −5, baja de peso y posible bajada del nivel del tamer.
  - La barra de virus sube 1. Con 16, el partner se convierte en Sukamon (ver la
    [ficha de evolución](evolution.md)).
  - Se guarda la caca en el mapa: hasta 16 por mapa y 100 en total.
- **En el baño** (`handleToilet`) **(comprobado)**: felicidad +2, disciplina +2 y una pequeña baja de peso según el
  tamaño de la caca. En la versión americana el siguiente aviso tarda el doble. El orinal portátil (un objeto) vacía
  sin dar felicidad ni disciplina, y el siguiente aviso tarda el intervalo normal, sin doblar (`handlePortaPotty` en
  `item.c`) **(comprobado)**.
- **Comer caca:** dos especies concretas comen las cacas cercanas, ganan HP, MP y peso según el tamaño, y tienen una
  pequeña probabilidad de curarse (`handleEatingPoop`) **(comprobado)**.

### Enfermedad y heridas

- **Causas de enfermedad** (`tickSicknessMechanics`) **(comprobado)**:
  - Con 10 o más horas sin dormir acumuladas, cada hora hay una tirada con probabilidad
    (intentos − 10) × 5 + (cansancio − 50) %. Ver rarezas.
  - Más de 10 horas seguidas en una zona que no le gusta.
  - Una herida sin curar durante 12 horas se convierte en enfermedad.
  - Algunas comidas tienen probabilidad de enfermar (`handleItemSickness` en `item.c`).
- **Al enfermar:** felicidad −20 **(comprobado)**.
- **Mientras está enfermo:** cada hora felicidad −10, disciplina −5 y cansancio +5 **(comprobado)**. En batalla rinde
  menos (ver la [ficha de batalla](battle.md)).
- **Sin tratar:** a las 12 horas de enfermedad pierde **una vida** (empieza con 3) y se lanza un evento de guion
  **(comprobado)**. Qué muestra ese evento es **(suposición)**.
- **Heridas:** salen de los combates ganados en los que recibió daño, con probabilidad = cansancio − % de HP
  restante (`handleBattleInjury` en `battle_ui.c`). La tirada va en la pantalla de resultados, antes de las cuentas
  del tamer tras el combate (felicidad, cansancio, tiempo) **(comprobado)**.
- **Curas** (`handleStatusItems` en `item.c`) **(comprobado)**: la venda cura la herida siempre y la enfermedad un
  20 % de las veces; la medicina cura las dos. Con el partner desmayado (HP 0) no hacen nada
  (`handleMedicineHealing`) **(comprobado)**. No hay lógica de médico en el código C **(suposición: estará en
  scripts)**.
- **Sin vidas:** si la enfermedad sin tratar quita la última vida, la vida restante pasa a 0 y el partner muere sin
  heredar stats **(comprobado)**.

### Felicidad y disciplina

- **Rangos** (`tickConditionBoundaries`): felicidad de −100 a 100; disciplina, cansancio de 0 a 100; peso de 1 a 99
  **(comprobado)**.
- **Regañar** (`handlePraiseScold`): disciplina +2, felicidad −10. Justo después de un rechazo o de una caca en el
  suelo, disciplina +8 sin perder felicidad **(comprobado)**. Esa bonificación se gasta: la siguiente vez que se le
  ofrece un objeto se reinicia y lo acepta (`partnerWillRefuseItem`) **(comprobado)**.
- **Elogiar:** disciplina −5, felicidad + (disciplina / 10 + 2) **(comprobado)**.
- **Batallas** (`startBattle` en `tamer.c`): ganar, felicidad +2; huir, felicidad −10 y disciplina −6; perder,
  felicidad −30 y disciplina −20 **(comprobado)**. Ganar y huir suman además el cansancio de después del combate
  (+5, y huir +2 más); perder no lo suma, pero quita una vida (`handlePostBattleTiredness`) **(comprobado)**.
- **Zona:** cada hora, +1 de felicidad en una zona que le gusta y −1 en una que no (`tickTirednessMechanics`)
  **(comprobado)**.
- **Enfado** (`tickUnhappinessMechanics`) **(comprobado)**:
  - Sin otra necesidad y con felicidad negativa, cada 10 minutos hay una probabilidad de (|felicidad| − disciplina)
    % de que se enfade.
  - Mientras está enfadado, la felicidad sube sola poco a poco, pero **todos sus stats bajan un 2 % cada 15
    minutos**, hasta que la felicidad vuelve a 0 o se le regaña.
- **Vida:** con felicidad menor que 80, cada 4 horas pierde horas de vida extra: 1 si está entre 31 y 79, y hasta 4
  cuanto más baja (fórmula: (felicidad − 80) / −50 + 1, con división entera) (`tickConditions`) **(comprobado)**.
- **Disciplina en batalla:** reduce la desobediencia (ver la [ficha de batalla](battle.md)).
- **Nivel del tamer** (`clock.c`): cada año de juego (30 días), con felicidad 100 puede subir; con felicidad negativa
  puede bajar **(comprobado)**.

### Cansancio

- **Sube:** entrenar (unos 8 a 10 por sesión), pelear (+5), estar enfermo (+5 por hora) y estar en una zona que no
  le gusta (+1 por hora) **(comprobado)**.
- **Efectos** **(comprobado)**:
  - Con 80 o más, aparece el aviso de cansado y pierde 2 de felicidad cada 5 minutos.
  - Con 70 o más tras entrenar, felicidad −3 y disciplina −3.
  - Con 100, el entrenamiento solo da 1 punto.
  - Con cansancio alto el hambre llega antes y suben las probabilidades de enfermar y de lesionarse.
- **Baja:** al dormir, en una zona que le gusta (−2 por hora) y con algunas comidas **(comprobado)**.

### Vida y edad

- **Vida restante:** empieza en 360 horas (15 días) y baja 1 por hora (`initializePartner`, `clock.c`)
  **(comprobado)**.
- También baja: las horas dormidas, cada combate (adelanta 20 minutos), cada entrenamiento (adelanta 1 hora), la
  infelicidad (ver arriba) y algunos objetos (−24 horas) **(comprobado)**.
- Sube: al evolucionar a definitivo sin objeto (+96 horas) y con algunas comidas **(comprobado)**.
- **Edad:** días vividos; sube a medianoche, también si se llega a ella saltando horas o entrenando
  **(comprobado)**.
- **Descansar o saltar horas** (`skipHours`, lo usan los guiones): suma las horas al contador de evolución, las
  resta de la vida, adelanta comida, baño y ventana de hambre, y baja la energía **una sola vez** por el gasto de la
  especie, sin mover el reloj **(comprobado)**.

### Muerte y renacer

- **Muerte natural** (`tickDeathCondition`): con la vida restante a 0, el tamer libre y fuera de la hora protegida tras
  dormir, se lanza el evento de muerte **(comprobado)**.
- **Vidas:** perder un combate o dejar 12 horas una enfermedad sin tratar quita una vida; con 0 vidas, la vida
  restante pasa a 0 y muere (`script_interp.c`) **(comprobado)**.
- **Renacer** (`reincarnatePartner` en `evolution.c`, `initializeReincarnatedPartner`, `setReincarnateStats`;
  el huevo se elige en `dooa.c`) **(comprobado)**:
  - Sale uno de los cuatro bebés, al azar.
  - Stats = los fijos del bebé + **un % de los stats del partner muerto igual al nivel del tamer**, pero solo si la
    muerte fue natural. Si no, no hereda nada.
  - Disciplina y felicidad a 50, vida a 360, edad a 0, vidas a 3, errores y batallas a 0.
  - Las técnicas equipadas se reinician. La lista de técnicas aprendidas no se borra, así que parece que se
    conservan **(suposición)**.

### Entrenamiento

Es parte del cuidado porque gasta tiempo, cansa y baja la felicidad (`src/trn/`) **(comprobado)**:

- **Coste por sesión** (`TRN_advanceTrainingTime`): 1 hora de juego (con todo lo que pasa en una hora), cansancio,
  energía y felicidad.
- **Ganancia** (`TRN_calculateTrainingMultiplier`): una base por gimnasio, multiplicada por:
  - aptitud de la especie para ese stat: ×0,8 a ×1,1 (`trainingType` en `RAISE_DATA`);
  - nivel: ×0,9 (bebé y entrenamiento), ×1,0 (novato), ×1,1 (campeón), ×1,2 (definitivo);
  - gimnasio mejorado: ×1,2;
  - 10 o más sesiones de ese stat entrenadas como bebé o en entrenamiento (en el primer gimnasio; los `TRN_setup*`
    las cuentan con nivel menor que 3): ×1,2 **(comprobado)**;
  - el manual de entrenamiento (un objeto): ×1,1.
- **Ruleta de bonus** (`trn_slots.c`): multiplica el resultado entre ×0,5 y ×10 **(comprobado)**.
- **Comidas de entrenamiento:** activan durante un tiempo un bonus para uno o varios stats (`setTrainingBoost` en
  `item.c`) **(comprobado)**. Toda comida llama a `setTrainingBoost`, así que una sin bonus borra el que hubiera
  (ver rarezas) **(comprobado)**.
- **Inteligencia:** su sesión pide el multiplicador del MP (modo 1), no el de inteligencia
  (`TRN_awardBrainTrainingGains`) **(comprobado)**. Con las tablas del original las dos aptitudes coinciden, así que
  no se nota.
- **Tope:** una ganancia de 1000 o más se queda en 999; en el original solo se limita la primera ganancia de cada
  sesión (`TRN_award*`) **(comprobado)**.
- Cada sesión suma 1 al contador de evolución **(comprobado)**.

### Comida y objetos de cuidado

- Los efectos de los objetos no están en una tabla: los decide el código, según el rango del identificador del objeto
  (`ITEM_FUNCTIONS`) **(comprobado)**. Rangos: curas de HP y MP, curas de estado, discos de mejora temporal, chips
  (stats permanentes a cambio de vida), objetos de baño, comida y objetos de evolución.
- Cada comida puede dar energía, quitar cansancio, cambiar felicidad, disciplina, peso o vida, subir stats de forma
  permanente, curar HP o MP, activar un bonus de entrenamiento o tener probabilidad de enfermar. Todo en un `switch`
  dentro de `handleFood` (`item.c`) **(comprobado)**. Cada comida baja además 1 el temporizador del baño.

## Datos

Tablas del ejecutable principal. `dwx` las extraerá a YAML desde el disco del usuario; aquí solo se describe su forma.

| Tabla | Entrada | Entradas | Campos |
|---|---|---|---|
| `RAISE_DATA` | 28 bytes | 66 | Por especie: hasta 8 horas de comida, tope, umbral y gasto de energía, intervalo de baño, tamaño de la caca, comida favorita, patrón de sueño, zona favorita, aptitud de entrenamiento, peso por defecto y desplazamientos de cámara. |
| `SLEEP_PATTERN` | 6 bytes | 8 | Patrones de sueño (horas despierto y dormido). |
| `ITEM_PARA` | 32 bytes | 128 | Por objeto: nombre (no en la versión europea, que usa `ITEM_NAMES`), precio, valor de mérito, orden, color, si se puede tirar y un campo sin identificar. |
| `ITEM_FUNCTIONS` | 4 bytes | 128 | Función que aplica cada objeto. Los valores de cada efecto están en el código, no en datos. |
| `HEALING_VALUES` | — | 8 bytes en total | Cuánto cura cada objeto de HP o MP. |
| `TOILET_DATA` | — | — | Posiciones de los baños por mapa (`toilet_data.c`). |
| `REINCARNATE_BABY_TYPE` | 2 bytes | 4 | El bebé de cada huevo. |

El estado del partner está en `PartnerPara` (`include/dw/params.h`, 0x84 bytes) **(comprobado)**:

| Campo | Qué guarda |
|---|---|
| `condition` | Banderas: sueño, cansado, hambre, baño, enfado, lesión, enfermedad |
| Horas de dormir y despertar, horas despierto y dormido por defecto, vigilia de hoy | Horario de sueño |
| `sicknessCounter`, `missedSleepHours`, `sicknessTries`, `sicknessTimer`, `injuryTimer`, `timesBeingSick` | Enfermedad y heridas |
| `poopLevel`, `poopingTimer`, `virusBar` | Baño y suciedad |
| `tiredness`, `subTiredness`, `tirednessSleepTimer`, `tirednessHungerTimer` | Cansancio |
| `discipline`, `happiness` | Disciplina y felicidad |
| `energyLevel`, `foodLevel`, `starvationTimer`, `nextHungerHour`, `emptyStomachTimer`, `refusedFavFood`, `weight` | Hambre y peso |
| `remainingLifetime`, `age` | Vida y edad |
| `careMistakes`, `battles`, `evoTimer` | Contadores de evolución |
| Bonus de entrenamiento (bandera, valor, tiempo), contadores de mejora | Entrenamiento |
| Copia de stats | Para volver de Sukamon |

Las vidas no están aquí, sino en la entidad del partner. Las estructuras de `RAISE_DATA` están en
`include/dw/partner.h`.

## Rarezas

- **Enfermedad casi segura.** La probabilidad de enfermar se guarda en un byte sin signo; un valor negativo se
  convierte en uno grande y la tirada siempre acierta **(comprobado)**. Por eso, con 10 horas sin dormir acumuladas,
  enfermar es prácticamente seguro **(suposición sobre el efecto en juego)**.
- **Penalización que nunca salta.** Al dormir, el hambre se quita antes de comprobarla para la baja de peso, así que
  esa baja nunca ocurre **(comprobado)**.
- **Sin variación en el bebé.** El horario del bebé llama a un aleatorio con límite 1, que siempre da 0
  **(comprobado)**.
- **Contador muerto.** `subTiredness` se lee y se reinicia, pero nada lo sube **(comprobado: ninguna función del
  decomp lo escribe salvo para ponerlo a 0)**.
- **Medianoche en combate.** Si un combate cruza la medianoche, no suma edad ni evolución **(comprobado)**.
- **Años distintos.** El entrenamiento trata un año como 31 días y el reloj como 30 **(comprobado)**.
- **Sueño fijo para la evolución.** Dormir suma las horas de sueño por defecto al contador de evolución, aunque
  durmiera menos **(comprobado)**.
- **Baño en la versión americana.** Solo ahí el baño dobla el tiempo hasta el siguiente aviso **(comprobado)**.
- **Cansancio por MP roto.** Ver la [ficha de batalla](battle.md). `handlePostBattleTiredness` divide antes de
  multiplicar: solo un combate que gastó todo el MP suma los 10 de cansancio **(comprobado)**.
- **La comida borra el bonus.** Cualquier comida reinicia el bonus de entrenamiento, aunque no dé ninguno
  (`handleFood`) **(comprobado)**.
- **Error de cuidado doble al saltar horas.** `skipHours`, el combate y el entrenamiento bajan la ventana de hambre
  y cuentan el error si se agota, pero no la cierran; luego `tickHungerMechanics` la cierra y lo cuenta otra vez
  **(comprobado)**.
- **Intentos de enfermedad al saltar horas.** Con el partner enfermo, saltar horas suma intentos de la tirada de
  enfermedad **(comprobado)**.
- **Infelicidad al saltar horas.** `skipHours` comprueba `(HORA + horas % 4) == 0` en vez de cruzar un múltiplo de 4:
  solo resta vida por infelicidad a medianoche con grupos de 4 horas **(comprobado)**.

## Dónde está en el decomp

| Qué | Dónde |
|---|---|
| Bucle de cuidado | `src/main/partner_impl.c`: `tickConditions`, `tickConditionBoundaries` |
| Hambre y comida | `src/main/partner_impl.c`: `setFoodTimer`, `tickHungerMechanics`, `partnerHandleFoodFeed`, `partnerWillRefuseItem`; `src/main/item.c`: `handleFood`, `addEnergy` |
| Sueño | `src/main/partner_impl.c`: `setSleepTimes`, `tickSleepMechanics`, `handleSleeping`, `sleepRegen`; `src/main/partner.c`: `partnerTickSleep` |
| Baño | `src/main/partner_impl.c`: `tickPoopingMechanic`, `createPoopPile`, `handleToilet`, `handleEatingPoop`; `src/main/partner.c`: `tickWildPoop`; `src/main/toilet_data.c` |
| Enfermedad y heridas | `src/main/partner_impl.c`: `tickSicknessMechanics`; `src/main/battle_ui.c`: `handleBattleInjury`; `src/main/item.c`: `handleStatusItems` |
| Felicidad y disciplina | `src/main/partner_impl.c`: `handlePraiseScold`, `tickUnhappinessMechanics`, `tickTirednessMechanics`; `src/main/tamer.c`: `startBattle` |
| Reloj, vida y edad | `src/main/clock.c`: `tickGameClock`; `src/main/partner_impl.c`: `skipHours` |
| Muerte y renacer | `src/main/partner_impl.c`: `tickDeathCondition`, `initializeReincarnatedPartner`; `src/main/evolution.c`: `reincarnatePartner`; `src/dooa/dooa.c` |
| Entrenamiento | `src/trn/trn_reward.c`: `TRN_advanceTrainingTime`, `TRN_calculateTrainingMultiplier`; `src/trn/trn_slots.c`; `src/trn2/` |
| Estructuras | `include/dw/params.h` (`PartnerPara`), `include/dw/partner.h` (`RaiseData`), `include/dw/item.h` |
