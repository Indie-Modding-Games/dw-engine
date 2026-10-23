# Auditoría de los valores del original: reglas o tablas

> Fecha: 2026-10-21. Paquete `p4-ruleset-audit`, fase A (solo clasificación: no cambia ningún valor ni código).
> Completa la [política legal de assets](assets.md).

## Para qué sirve este documento

El motor se escribe mirando el juego original (su *decomp*), y la política del proyecto es que **en git no hay nada
del original**: ni código, ni gráficos, ni textos, ni tablas de datos. Pero para que el juego se juegue igual hay que
saber *cómo funciona*: cuántas horas vive un Digimon, cada cuánto tiene hambre, cómo se calcula el daño. Este
documento revisa uno por uno los números que el repositorio tiene hoy sacados del original y decide, para cada uno, si
puede quedarse en git o si tiene que salir del disco del jugador (ejecutable y overlays).

Hay dos clases:

- **(a) Regla o mecánica.** Un número suelto, o una estructura pequeña, que dice *cómo funciona* el juego: un tope,
  un umbral, un divisor de una fórmula, cada cuántos fotogramas pasa algo, una probabilidad, el tamaño de un bloque.
  Es la "física" del juego, lo que cualquiera describiría en una guía. **Se queda en git**, en el ruleset, con su cita
  al decomp.
  *Ejemplo:* `life.yaml: lifetime_hours: 360`, el partner vive 360 horas de juego. Es una regla; un mod puede
  cambiarla.
- **(b) Tabla por entidad.** Datos con una entrada por Digimon, por objeto, por técnica o por mapa: los stats de cada
  especie, qué especie evoluciona en cuál, qué hace cada objeto. Es el *contenido* del juego, igual que sus gráficos.
  **Sale del disco del jugador (ejecutable y overlays) con `dwx`** al paquete base, y el ruleset solo dice qué tabla usar.
  *Ejemplo:* `life.yaml: rebirth.babies`, los stats iniciales de cada uno de los cuatro bebés al renacer. Son datos
  de cuatro especies concretas, no una regla.
- **(?) Dudoso.** Un valor que podría ir a cualquiera de las dos. Lleva una recomendación y su motivo. Todos los de esta
  auditoría ya están decididos (ver «Decisiones del coordinador»): en las tablas aparecen con su clase final.

**Decisión de la distinción (a)/(b):** «Decidido por el coordinador (2026-10-21)». Motivo: las
reglas son necesarias para reimplementar el juego y son descripciones de su funcionamiento, no copias de su contenido;
las tablas por entidad son contenido del juego, y el modelo "trae tus propios datos" ([ADR 0011](../adr/0011-politica-legal-de-assets.md))
exige que salgan del disco del usuario.

Los nombres de funciones y ficheros del decomp en los comentarios están permitidos: dicen dónde comprobar una regla.
Las tablas copiadas no.

### Criterios que aplica esta auditoría

Estos criterios son la interpretación de la decisión anterior para los casos que no dice; están aceptados tal como están
escritos: «Decidido por el coordinador (2026-10-21)».

1. **Se clasifica por el significado, no por la sintaxis.** La rama `p4-ids-refs` cambia números de objeto y técnica
   por ids (`item: <número>` pasa a `item: <id del objeto>`, `map: <número>` a `map: <id del mapa>`). Eso no cambia la
   clase: una lista de efectos por objeto es (b) se escriba con números o con ids. Donde `p4-ids-refs` cambia la
   forma, la línea lo dice.
2. **Una referencia no es una tabla.** Regla: «una sola referencia a una entidad es (a); una lista de dos o más
   entidades es (b)». «Decidido por el coordinador (2026-10-21)». Un id suelto dice *a quién* se aplica una regla (la
   forma a la que cae un novato sin candidato, la técnica de contraataque, el objeto que atrae enemigos): nombra el
   sujeto de una mecánica y no copia datos de esa entidad. Una lista de entidades, o una que **asigna valores** a
   varias (stats, efectos, destinos de evolución), es (b).
3. **Una categoría no es una entidad.** Las tablas indexadas por especialidad (fuego, hielo...), tipo (dato, vacuna,
   virus), nivel (bebé... definitivo), categoría de objeto o tramo de inteligencia son mecánicas: (a). Excepción
   (D3, decidida): van a (b) las que en el ejecutable son **una tabla de datos con símbolo propio y nueve valores o más**
   (`TYPE_FACTORS`, `FLEE_CHANCES`, `SLEEP_PATTERN`, `ITEM_SORT_ORDER`), porque extraerlas cuesta poco (`dwx` ya lee
   tablas por dirección) y son lo más parecido a una tabla copiada.
4. **Los valores del motor** (inventados, medidos o desviaciones aprobadas) son (a): no vienen de ninguna tabla.

## Cómo se lee la tabla

Una línea por valor: la clave dentro del fichero, el valor o un resumen (las tablas se resumen, nunca se copian
aquí), la clase y el motivo. `quirks.*` son banderas de rarezas: todas son (a), porque dicen si se reproduce un
comportamiento, no guardan datos.

---

## `content/packs/game/rules/original/`

### `ruleset.yaml`

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `id` | `original` | a | Metadatos del proyecto. |
| `name` | `Original` | a | Metadatos del proyecto. |
| `version` | `0.1.0` | a | Metadatos del proyecto. |
| `description` | `Las reglas del juego original, con sus rarezas, como punto de partida y de comparación.` | a | Metadatos del proyecto. |
| `tables` | 8 papeles → `data/*` | a | Referencias a las tablas del paquete base: el mecanismo que usa la clase (b). La fase B añade papeles. |
| `sections` | 12 secciones | a | Estructura del ruleset. |

### `clock.yaml`

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `frames_per_second` | 20 | a | Temporizador o duración. |
| `frames_per_minute` | 20 | a | Temporizador o duración. |
| `days_per_year` | 30 | a | Calendario. |
| `year_limit` | 100 | a | Calendario. |
| `start.year` | 0 | a | Fecha de arranque de una partida nueva. |
| `start.day` | 0 | a | Fecha de arranque de una partida nueva. |
| `start.hour` | 8 | a | Hora de arranque de una partida nueva. |
| `start.minute` | 0 | a | Hora de arranque de una partida nueva. |

### `life.yaml`

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `lifetime_hours` | 360 | a | Temporizador o duración. |
| `lives` | 3 | a | Regla de vida. |
| `unhappy_lifetime.every_hours` | 4 | a | Temporizador o duración. |
| `unhappy_lifetime.happiness_below` | 80 | a | Umbral. |
| `unhappy_lifetime.happiness_divisor` | 50 | a | Divisor de una fórmula. |
| `unhappy_lifetime.extra_hours` | 1 | a | Término de la fórmula de vida. |
| `rebirth.baby_choice` | `original` | a | Nombre de estrategia. |
| `rebirth.babies` | tabla de 4 entradas por especie (id + 6 stats) | **b** | Stats fijos por especie. En el ejecutable: `REINCARNATE_BABY_TYPE` (dato de 8 bytes; dooa.c) y los argumentos inmediatos de las cuatro llamadas a `setReincarnateStats` en `initializeReincarnatedPartner` (partner_impl.c). Destino: `data/rebirth`. |
| `rebirth.inherit_percent_per_tamer_level` | 1 | a | Factor de la fórmula de herencia. |
| `scenes.lost_life.*` | 35, 95, 135, 145, 185 fotogramas; sombras 160 y 255 | a | Temporizador o duración; nivel de fundido. |
| `scenes.wake_map.story_map`, `early_map`, `exit` | 205, 218, 1 | a | Referencia a mapas por su índice. |
| `scenes.wake_map.story_triggers`, `story_variable`, `story_variable_at_least` | [220, 214], 1, 50 | a | Condición de la historia (umbral). |
| `scenes.death.sound_bank`, `sparkle_sound` | 19, 7 | a | Referencia a un banco de sonidos y a un sonido. |
| `scenes.death.*_frame`, `burst`, `sink`, `vanish`, `rise`, `glow`, `hatch` | fotogramas 10-439 con su sonido | a | Temporizador o duración. |
| `scenes.death.clips.*` | 1, 44, 12, 33, 28, 11 | a | Referencia a animaciones por su id. |

### `new_game.yaml`

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `partner` | una especie (referencia) | a | Referencia (criterio 2): una sola entidad, sujeto de una regla. |
| `stats` | 6 stats de reserva | a | Valores inventados de respaldo (no son del original); los reales los extrae `dwx` del guion 193 a `data/new_game_stats`. |
| `tamer_level` | 0 | a | Valor inicial. |

### `new_game_scene.yaml`

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `lines` | 1238 (una tabla de MAPHEAD) | a | Referencia a los textos del paquete base; las frases no se copian. |
| `partners` | 2 especies (referencias) | a | Referencias (criterio 2): la especie que elige cada respuesta. |
| `fade_in_frames`, `fade_out_frames`, `frame_ticks` | 20, 40, 3 | a | Tiempos de la escena (`fadeFromBlack`, `fadeToBlack`, `VSync(3)`). |
| `camera_distance`, `camera_projection`, `camera_centre_line` | 4200, 1000, 185 | a | Parámetros de cámara de la escena. |
| `host` | una especie, su posición, dos clips, un fotograma y dos giros | a | Puesta en escena: una entidad y sus números de animación. |
| `naming` | 6 letras, 5 columnas, 18 filas de letras | a | El alfabeto latino y los dígitos en una rejilla; no son datos del original. |

### `partner_moves.yaml`

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `new_game_slot` | 0 | a | Posición en la lista de técnicas de la especie, no una técnica concreta. |
| `reborn_slot` | 0 | a | Posición en la lista de técnicas de la especie, no una técnica concreta. |

### `calendar.yaml`

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `new_day.day_counters` | 4 variables de la historia | a | Números de variable (referencias): script_common.c: dailyPStatTrigger suma 1 a cada una al empezar el día. |
| `new_day.stopped_value` | 255 | a | Valor centinela de la regla: una variable con él no cuenta. |
| `new_day.daily_marks` | 1 variable y su bit | a | Referencia y máscara: el bit que el día nuevo borra. |

### `field_items.yaml`

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `ground_slots` | 11 | a | Capacidad (DROPPED_ITEMS). |
| `chest_slots` | 8 | a | Capacidad (los cofres de un mapa). |
| `reach_cells` | 1 | a | Distancia de la regla (tamer.c: checkItemPickup, checkChestCollision). |
| `pickup_sound`, `toilet_sound`, `sleep_sound`, `praise_sound`, `scold_sound` | ranura y sonido | a | Referencias a sonidos (ranura, número), no datos. |
| `feed.sound` | ranura y sonido | a | Referencia a un sonido (partner.c: partnerTickFeedItem, playSound(0, 12)). |
| `feed.range_base`, `feed.range_per_distance` | 160, 20 | a | Fórmula de la regla (partner.c: checkEatDistance, distancia × 200 / 10 + 160). |
| `feed.take_distance` | 10 por defecto; 15 o 25 en 13 especies (`ITEM_TAKE_DISTANCE`, 66 bytes) | **b** | Tabla por especie. Como las de D3, se extrae como tabla de datos (`TableLocation`); hasta entonces el ruleset escribe solo los valores distintos del 10. |
| `feed.offer_frame`, `feed.hand_down_frame` | 7, 15 | a | Cuadros de animación de la escena (referencias de tiempo). |
| `feed.clips` | 5, 4, 0, 8, 13 | a | Ids de animación (referencias). |
| `toilet_walk.camera_frames` | 20 | a | Duración (partner.c: tickPartnerToilet, createCameraMovement). |
| `toilet_walk.partner_clip` | 10 | a | Id de animación (referencia). |

### `cards.yaml`

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `max_copies` | 9 | a | Límite de la regla (script_menu.c: showCardTextbox). |
| `listed.first`, `listed.last` | 1, 64 | a | Rango de números de carta (referencias). |
| `pack.draws` | 3 | a | Cantidad de la regla (rollCardPack). |
| `pack.rarity_roll`, `pack.rarity_below` | 100; 4 cotas | a | Probabilidades de rareza del sobre (rollCard): constantes de código, no datos de cartas. |
| `lines.*` | números de línea | a | Referencias a las líneas del texto de MAPHEAD 0x4d3; el texto no se copia. |

### `fishing.yaml`

Las tablas de la pesca (las clases de pez, `FISH_BITE_CHANCE`, `FISH_BAIT_CHANCES`, las zonas, las orillas y los mapas)
son (b) y no están aquí: son `data/fishing`, que `dwx` extrae del overlay FISH del disco.

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `sea_serpent.*` | 20 %, cada 8 y cada 20 fotogramas | a | Probabilidad y frecuencias de la regla (fish.c: FISH_findFishToBite, FISH_tickFishPopulation). |
| `bites.*` | cada 20 fotogramas; 200 y 600 | a | Frecuencia y distancias de la regla (FISH_findFishToBite, FISH_tickFish). |
| `population.*` | 33, 32; 600 a 1200; 30 | a | Topes, vida y pausa de la regla (FISH_tickFishPopulation, FISH_spawnFish, FISH_tickFish). |
| `hook.*` | 150; 20 a 40 y 30; 40 a 60 y 60; 150, 100 | a | Umbral, tiempos y fuerza de la regla (FISH_hookFish). |
| `tension.*` | 5000 y 10000; rangos de ±500 o menos; 1 a 3, 5 | a | Límites y deltas de la fórmula (FISH_beginTension, FISH_updateTension). |
| `junk.*` | desde 31, cada 20, 1000 de 10000, 0 a 99 | a | Frecuencia y probabilidad de la regla (FISH_tickRod); qué objeto sale es (b), de `data/fishing`. |
| `cast.*` | 5, 20; 100, 200 y 400 | a | Fuerza y velocidades del lanzamiento (FISH_tickRod). |
| `shore_by_y.*` | 429, 6 | a | Constantes de código de la prueba de agua (FISH_isFishableWater2). |
| `sounds.*` | ranura 8, sonidos 0 a 10 | a | Referencias a sonidos (playSound). |

### `save.yaml`

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `autosave.on_map_change` | `true` | a | Desviación aprobada (2026-10-20); del motor. |
| `autosave.on_battle_end` | `true` | a | Desviación aprobada (2026-10-20); del motor. |
| `autosave.on_partner_sleep` | `true` | a | Desviación aprobada (2026-10-20); del motor. |
| `autosave.on_new_day` | `true` | a | Desviación aprobada (2026-10-20); del motor. |
| `autosave.on_evolution` | `true` | a | Desviación aprobada (2026-10-20); del motor. |
| `autosave.on_tournament_result` | `true` | a | Desviación aprobada (2026-10-20); del motor. |
| `autosave.on_recruit_join` | `true` | a | Desviación aprobada (2026-10-20); del motor. |
| `autosave.on_key_item` | `true` | a | Desviación aprobada (2026-10-20); del motor. |
| `autosave.on_dangerous_area` | `true` | a | Desviación aprobada (2026-10-20); del motor. |
| `autosave.on_story_progress` | `true` | a | Desviación aprobada (2026-10-20); del motor. |
| `autosave.on_partner_reborn` | `true` | a | Desviación aprobada (2026-10-20); del motor. |
| `autosave.coalesce_seconds` | 2 | a | Valor del motor (desviación aprobada, 2026-10-20). |

### `tamer.yaml`

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `level_min` | 0 | a | Tope (mínimo). |
| `level_max` | 10 | a | Tope. |
| `evolution_level_gain` | 1 | a | Incremento. |

### `story.yaml`

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `ticks_per_frame` | 3 | a | Ritmo: el original avanza 20 fotogramas por segundo (`VSync(3)`), el motor hace tick a 60 Hz. |
| `variable_max` | 255 | a | Tope (una variable es un byte). |
| `frozen_variable`, `frozen_value` | 0, 3 | a | Número de variable y valor de una marca de estado (`scriptPauseGame`). |
| `talk_facing_trigger` | 44 | a | Número de disparador de una regla. |
| `show_name_variable` y sus valores | 6; 0, 1, 2 | a | Número de variable y valores de una regla (`consumeMapChangeShowName`). |

### `dialogue.yaml`

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `open_frames` | 5 | a | Ritmo: fotogramas que tarda en crecer una caja (`ui.c: renderUIBoxAnimated`). |
| `no_cancel_trigger` | 49 | a | Número de disparador que bloquea cancelar una selección. |
| `speech_box.columns`, `speech_box.rows` | 21, 4 | a | Tamaño de la caja de diálogo en caracteres y filas. |
| `sounds.*` | 5 pares ranura/sonido | a | Números de sonido del sistema que suenan al abrir, avanzar, cancelar, mover el cursor y rechazar. |
| `name_colors.*` | 6, 10, 7, 5 | a | Índices de color del nombre de quien habla. |

### `story_values.yaml`

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `stat_limits` | 21 topes | a | Topes de las órdenes de estadísticas (`STATS_LIMIT_ARRAY`); `lives` 255: el original lee fuera de la tabla. |
| `happiness_floor` | -100 | a | Suelo de `reduce_stat` para la felicidad. |
| `date_year_limit`, `date_overflow` | 255; 25, 14, 23:59 | a | Rareza de `secondsToDate`. |

### `item_use.yaml`

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `refusal.never_taken` | 3 rangos de números de objeto | **b** | D5. «Decidido por el coordinador (2026-10-21)»: marca por objeto en `data/item_effects`; `meals` se deriva de los objetos con manejador de comida si coincide. |
| `refusal.discipline_checked_below` | 1 número de objeto (límite de rango) | **b** | D5. «Decidido por el coordinador (2026-10-21)»: marca por objeto en `data/item_effects`. |
| `refusal.fixed_level_evolutions` | 3 objetos con su nivel | **b** | Valor por objeto: el nivel en que acepta cada objeto de evolución fija (comparaciones de `partnerWillRefuseItem`, partner_impl.c). Destino: `data/item_effects` (`accepted_level`). `p4-ids-refs` pasa los objetos a ids. |
| `refusal.refused_level_differences` | [0, 2, -1] | a | Regla sobre la diferencia de niveles. |
| `meals` | 2 rangos de números de objeto | **b** | D5. «Decidido por el coordinador (2026-10-21)»: marca por objeto en `data/item_effects`; `meals` se deriva de los objetos con manejador de comida si coincide. |
| `shop_discount_levels` | [1, 2] | a | Por nivel (criterio 3). El original usa `BABY_IDS` (8 especies; script_common.c: `isPartnerBaby`), que son justo las de nivel 1 y 2: la regla por nivel no copia la lista. |
| `training_boost_bits` | 6 bits, uno por stat | a | Disposición de bits (estructura). |

### `inventory.yaml`

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `bag.max_slots` | 30 | a | Tope. |
| `bag.slot_counts` | [10, 20, 30] | a | Tamaños posibles de la bolsa. |
| `bag.new_game_slots` | 10 | a | Valor inicial. |
| `bag.new_game_money` | 0 | a | Valor inicial. |
| `bag.new_game_items` | [] | a | Vacía. Llenarla con `DEFAULT_INVENTORY_TYPES/AMOUNTS` sería (b): ese dato saldría del ejecutable. |
| `bag.max_stack` | 99 | a | Tope. |
| `bag.stacking` | `original` | a | Nombre de estrategia. |
| `money.max` | 999999 | a | Tope. |
| `sort.orders` | 3 órdenes de 6 categorías (`ITEM_SORT_ORDER`, 18 bytes) | **b** | D3. «Decidido por el coordinador (2026-10-21)»: se extrae como tabla de datos (`TableLocation`). |
| `use.field_colors` | [0, 2] | a | Por categoría de uso (criterio 3). |
| `use.battle_colors` | [1, 2] | a | Por categoría de uso (criterio 3). |
| `use.discard_droppable` | 1 | a | Valor de una marca de `data/items`. |
| `use.key_item_droppable` | 0 | a | Valor de una marca de `data/items`. |
| `defeat.lost_percent` | 30 | a | Porcentaje o probabilidad. |
| `shop.stock_trigger_base` | 384 | a | Base de un rango de triggers (estructura). |
| `shop.sell_divisor` | 2 | a | Divisor de una fórmula. |
| `shop.baby_discount_percent` | 90 | a | Porcentaje o probabilidad. |
| `shop.max_amount` | 99 | a | Tope. |
| `effects.stat_limit` | 999 | a | Tope. |
| `effects.hp_mp_limit` | 9999 | a | Tope. |
| `effects.conscious_only` | [hp_healing, mp_healing, double_floppy, status, battle_disk] | a | Por manejador (categoría de código), no por objeto. |
| `effects.double_floppy` | HP y MP que cura (un valor de cada) | **b** | D6. «Decidido por el coordinador (2026-10-21)»: `data/item_effects`, junto a `HEALING_VALUES`. |
| `effects.status` | 5 objetos con su efecto | **b** | Efectos por objeto (casos de `handleStatusItems`, item.c). Destino: `data/item_effects` (`battle_status_cure`, `protects`, `injury_cure`, `sickness_cure`). `p4-ids-refs` pasa los objetos a ids. |
| `effects.injury_roll` | 3 | a | Rango de una tirada. |
| `effects.sickness_roll` | 10 | a | Rango de una tirada. |
| `effects.revive` | 2 objetos con su % de HP | **b** | Efectos por objeto (`handleRestore`, item.c). Destino: `data/item_effects` (`revive_hp_percent`, `battle_status_cure`). |
| `effects.battle_disk.limit_percent` | 130 | a | Porcentaje o probabilidad. |
| `effects.battle_disk.limit_cap` | 999 | a | Tope. |
| `effects.battle_disk.disks` | 7 objetos con la subida de 3 stats | **b** | Efectos por objeto (switch de constantes de `handleBuffDisks`, main.c). Destino: `data/item_effects` (`battle_buff`), leído con `ConstantSwitch` como `handleFood`. |
| `effects.chip.script` | 1 objeto → una sección de guion | **b** | Efecto por objeto (`handleChips`, caso 0x16). Destino: `data/item_effects` (`script_section`). |
| `effects.chip.toilet` | un objeto (referencia) | a | Criterio 2, «Decidido por el coordinador (2026-10-21)»: una sola entidad. El efecto "baño" sigue siendo del manejador `handleChips`. |
| `effects.chip.tamer_level.items` | 3 objetos | **b** | D7. «Decidido por el coordinador (2026-10-21)»: una lista de objetos; `chip_effect` por objeto en `data/item_effects`. |
| `effects.chip.tamer_level.chance_percent` | 10 | a | Porcentaje o probabilidad. |
| `effects.chip.tamer_level.amount` | -1 | a | Incremento. |
| `effects.food.favourite_energy_percent` | 140 | a | Porcentaje o probabilidad. |
| `effects.food.favourite_happiness` | 2 | a | Incremento. |
| `effects.food.poop_level_change` | -1 | a | Incremento. |
| `effects.evolution.requires_previous_level` | `true` | a | Regla. |
| `effects.evolution.fixed` | 3 objetos → especie | **b** | Mapa objeto → especie (`handleEvoItems`, item.c, objetos ≥ 0x7d). Destino: `data/item_effects` (`evolves_to`, que ya existe para `EVOLUTION_ITEM_TARGET`). Duplica `evolution.yaml: items.fixed`. |
| `quirks.stack_overflow_is_lost` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.baby_discount_limited_by_full_price` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.omni_disk_ignores_limit` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.useless_items_consumed` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |

### `evolution.yaml`

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `selection` | `original` | a | Nombre de estrategia. |
| `scoring` | `original` | a | Nombre de estrategia. |
| `check_hours.baby` | 6 | a | Temporizador o duración. |
| `check_hours.in_training` | 24 | a | Temporizador o duración. |
| `check_hours.rookie` | 72 | a | Temporizador o duración. |
| `check_hours.champion` | 144 | a | Temporizador o duración. |
| `baby_targets` | 4 especies → especie | **b** | Mapa de evolución especie → especie (switch de `getFreshEvolutionTarget`, evolution.c). Destino: `data/evolution` (`fresh_target` por especie). |
| `requirements.pass_points` | 3 | a | Umbral. |
| `requirements.weight_margin` | 5 | a | Umbral. |
| `requirements.hp_mp_divisor` | 10 | a | Divisor de una fórmula. |
| `requirements.mastered_moves_below` | 64 | a | Límite de la máscara de bits (estructura). |
| `fallback.rookie_hours` | 96 | a | Temporizador o duración. |
| `fallback.rookie_target` | una especie (referencia) | a | Referencia (criterio 2): una sola entidad, sujeto de una regla. |
| `specials.roll_limit` | 100 | a | Rango de una tirada. |
| `specials.wake_up` | 4 reglas: destino, orígenes, tirada, condiciones | **b** | D1. «Decidido por el coordinador (2026-10-21)»: `data/evolution: specials`. |
| `specials.praise_scold` | 3 reglas | **b** | D1. «Decidido por el coordinador (2026-10-21)»: `data/evolution: specials`. |
| `specials.praise_scold_on_timer.level` | 4 | a | Umbral. |
| `specials.praise_scold_on_timer.hours` | 360 | a | Temporizador o duración. |
| `items.fixed` | 3 objetos → especie | **b** | Igual que `inventory.yaml: effects.evolution.fixed` (duplicado: la fase B deja una sola fuente). |
| `items.level_step` | 1 | a | Regla. |
| `stat_gains.gain_divisor` | 10 | a | Divisor de una fórmula. |
| `stat_gains.scale_divisor` | 10 | a | Divisor de una fórmula. |
| `stat_gains.scaled_targets` | 4 especies | **b** | D2. «Decidido por el coordinador (2026-10-21)»: marca por especie en `data/digimon`. |
| `stat_gains.scaled_below_level` | 3 | a | Umbral por nivel. |
| `stat_gains.ultimate_lifetime_hours` | 96 | a | Temporizador o duración. |
| `stat_gains.champion_tamer_level_chance` | 20 | a | Porcentaje o probabilidad. |
| `stat_gains.ultimate_tamer_level_chance` | 100 | a | Porcentaje o probabilidad. |
| `moves.specialty_blocks` | 7 índices, uno por especialidad | **b** | D4. «Decidido por el coordinador (2026-10-21)»: se deriva al cargar de `data/moves` (campo `special`); no se guarda. |
| `moves.block_size` | 9 | a | Tamaño de bloque. |
| `moves.basic_slot_below_level` | 3 | a | Umbral. |
| `moves.finisher_first` | 58 | a | Límite de rango como mecánica; con `p4-ids-refs`, rango sobre índices del original. |
| `moves.finisher_end` | 113 | a | Límite de rango como mecánica; con `p4-ids-refs`, rango sobre índices del original. |
| `rebirth.babies` | 4 especies | **b** | `REINCARNATE_BABY_TYPE` (dooa.c, dato de 8 bytes). Destino: `data/rebirth`. Duplica la lista de `life.yaml`. |
| `scene.sound_bank` | 18 | a | Referencia a un banco de sonidos (`loadMapSounds2`). |
| `scene.wait_frames`, `scene.dim_frames` | 56, 32 | a | Temporizador o duración. |
| `scene.camera_aim_end`, `scene.camera_move_end`, `scene.burst_frame` | 64, 96, 106 | a | Temporizador o duración. |
| `scene.orbit_ease`, `scene.grow` | 164-194, 204-264 | a | Temporizador o duración. |
| `scene.roar_frame`, `scene.swap_frame`, `scene.end_frame` | 214, 284, 354 | a | Temporizador o duración. |
| `scene.burst_sounds`, `scene.roar_sound` | [1, 0], 5 | a | Referencia a sonidos del banco. |
| `scene.camera.*` | 8 enteros (ángulo, paso, distancia, subida, proyección, caída) | a | Término de una fórmula de cámara. |
| `scene.clips.*` | 0, 1, 12, 11 | a | Referencia a animaciones por su id. |
| `quirks.evolution_item_sticks` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.selection_average_carries_over` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.unlisted_baby_restarts_timer` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.rookie_stat_tie_picks_last` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.timer_special_overrides_normal` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |

### `training.yaml`

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `aptitude_tenths` | 6 stats × 5 tipos de aptitud | a | Por categoría: el `training_type` de cada especie ya sale de `data/raise`; son constantes de código de `TRN_calculateTrainingMultiplier`, no una tabla de datos. |
| `level_tenths` | [9, 9, 10, 11, 12] | a | Factores por nivel (criterio 3). |
| `upgrade_bonus` | 6/5 | a | Factor 6/5. |
| `upgrade_flags` | 6 triggers, uno por stat | a | Qué trigger de la historia mejora cada máquina: referencias a flags (criterio 2). |
| `practice_bonus` | 6/5 | a | Factor 6/5. |
| `practice_sessions` | 10 | a | Umbral. |
| `practice_level_below` | 3 | a | Umbral. |
| `manual_bonus` | 11/10 | a | Factor 11/10. |
| `tired_out_tiredness` | 100 | a | Umbral. |
| `tired_out_gain` | 1 | a | Valor fijo de la ganancia. |
| `slot_default_tenths` | 10 | a | Valor por defecto. |
| `overwork_tiredness` | 70 | a | Umbral. |
| `overwork_happiness` | -3 | a | Incremento. |
| `overwork_discipline` | -3 | a | Incremento. |
| `gain_formula` | `original` | a | Nombre de estrategia. |
| `places` | 2 módulos de entrenamiento: temporizadores y 15 cursos | a | D8. «Decidido por el coordinador (2026-10-21)»: `trn` y `trn2` son módulos de código (TRN_REL y TRN2_REL), no mapas; sus temporizadores y cursos son reglas del gimnasio, por stat (categoría). |
| `quirks.brain_training_uses_mp_aptitude` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |

### `care.yaml`

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `limits.happiness_min` | -100 | a | Tope (mínimo). |
| `limits.happiness_max` | 100 | a | Tope. |
| `limits.discipline_max` | 100 | a | Tope. |
| `limits.tiredness_max` | 100 | a | Tope. |
| `limits.weight_min` | 1 | a | Tope (mínimo). |
| `limits.weight_max` | 99 | a | Tope. |
| `limits.hp_mp_max` | 9999 | a | Tope. |
| `limits.stat_max` | 999 | a | Tope. |
| `limits.stat_gain_max` | 999 | a | Desviación aprobada (2026-10-20). |
| `limits.stat_min` | 1 | a | Tope (mínimo). |
| `start.discipline` | 50 | a | Valor inicial. |
| `start.happiness` | 50 | a | Valor inicial. |
| `hunger.baby_interval_hours` | 2 | a | Temporizador o duración. |
| `hunger.in_training_interval_hours` | 3 | a | Temporizador o duración. |
| `hunger.window_minutes` | 90 | a | Temporizador o duración. |
| `hunger.window_step_frames` | 10 | a | Temporizador o duración. |
| `hunger.starving_weight_loss_minutes` | 10 | a | Temporizador o duración. |
| `hunger.tired_hunger_tiredness` | 50 | a | Umbral. |
| `hunger.tired_hunger_frames` | 50 | a | Temporizador o duración. |
| `hunger.tired_hunger_minutes` | 3 | a | Temporizador o duración. |
| `hunger.fed_happiness` | 5 | a | Incremento. |
| `hunger.fed_discipline` | 1 | a | Incremento. |
| `hunger.overfed_happiness` | -3 | a | Incremento. |
| `hunger.overfed_discipline` | -2 | a | Incremento. |
| `hunger.favorite_energy_percent` | 140 | a | Factor. Duplica `inventory.yaml: effects.food`. |
| `hunger.favorite_happiness` | 2 | a | Incremento. Duplica `inventory.yaml: effects.food`. |
| `refusal.discipline_base` | 110 | a | Término de una fórmula. |
| `refusal.discipline_bonus_min` | 10 | a | Término de una fórmula. |
| `refusal.discipline_bonus_range` | 10 | a | Rango de una tirada. |
| `refusal.favorite_refusal_percent` | 20 | a | Porcentaje o probabilidad. |
| `sleep.tiredness_percent_per_minute` | 30 | a | Factor de una fórmula. |
| `sleep.tiredness_threshold` | 180 | a | Umbral. |
| `sleep.awake_cut_minutes` | 10 | a | Temporizador o duración. |
| `sleep.baby_awake_hours` | 3 | a | Temporizador o duración. |
| `sleep.in_training_awake_hours` | 7 | a | Temporizador o duración. |
| `sleep.young_awake_variation_hours` | 1 | a | Temporizador o duración. |
| `sleep.patterns` | 8 horarios (`SLEEP_PATTERN`, 48 bytes) | **b** | D3. «Decidido por el coordinador (2026-10-21)»: se extrae como tabla de datos (`TableLocation`). |
| `sleep.baby_sleepy_penalty.every_frames` | 200 | a | Temporizador o duración. |
| `sleep.baby_sleepy_penalty.happiness` | -1 | a | Incremento. |
| `sleep.baby_sleepy_penalty.discipline` | -1 | a | Incremento. |
| `sleep.in_training_sleepy_penalty.every_frames` | 300 | a | Temporizador o duración. |
| `sleep.in_training_sleepy_penalty.happiness` | -1 | a | Incremento. |
| `sleep.in_training_sleepy_penalty.discipline` | -1 | a | Incremento. |
| `sleep.adult_sleepy_penalty.every_frames` | 1200 | a | Temporizador o duración. |
| `sleep.adult_sleepy_penalty.happiness` | -2 | a | Incremento. |
| `sleep.adult_sleepy_penalty.discipline` | -4 | a | Incremento. |
| `sleep.missed_sleep_tamer_level_percent` | 1 | a | Porcentaje o probabilidad. |
| `sleep.regeneration.hp_mp_percent` | 70 | a | Porcentaje o probabilidad. |
| `sleep.regeneration.hp_mp_percent_range` | 10 | a | Porcentaje o probabilidad. |
| `sleep.regeneration.tiredness_percent` | 80 | a | Porcentaje o probabilidad. |
| `sleep.regeneration.tiredness_percent_range` | 20 | a | Porcentaje o probabilidad. |
| `sleep.regeneration.weight_divisor` | 10 | a | Divisor de una fórmula. |
| `sleep.regeneration.pillow_percent` | 120 | a | Porcentaje o probabilidad. |
| `sleep.regeneration.liked_area_percent` | 120 | a | Porcentaje o probabilidad. |
| `sleep.regeneration.disliked_area_percent` | 80 | a | Porcentaje o probabilidad. |
| `sleep.sick_penalty.happiness` | -20 | a | Incremento. |
| `sleep.sick_penalty.discipline` | -10 | a | Incremento. |
| `sleep.sick_penalty.tiredness` | 10 | a | Incremento. |
| `sleep.injured_penalty.happiness` | -10 | a | Incremento. |
| `sleep.injured_penalty.discipline` | -5 | a | Incremento. |
| `sleep.injured_penalty.tiredness` | 5 | a | Incremento. |
| `sleep.hungry_weight_loss` | 1 | a | Incremento. |
| `sleep.fade.out_frames` | 40 | a | Temporizador o duración. |
| `sleep.fade.in_frames` | 40 | a | Temporizador o duración. |
| `sleep.fade.control_frame` | 35 | a | Temporizador o duración. |
| `toilet.poop_step_minutes` | 10 | a | Temporizador o duración. |
| `toilet.urge_discipline_offset` | 20 | a | Término de una fórmula. |
| `toilet.urge_frames_per_hundred` | 1200 | a | Temporizador o duración. |
| `toilet.toilet_happiness` | 2 | a | Incremento. |
| `toilet.toilet_discipline` | 2 | a | Incremento. |
| `toilet.wild_happiness` | -10 | a | Incremento. |
| `toilet.wild_discipline` | -5 | a | Incremento. |
| `toilet.wild_tamer_level_percent` | 1 | a | Porcentaje o probabilidad. |
| `toilet.weight_loss_roll` | 4 | a | Rango de una tirada. |
| `toilet.weight_loss_divisor` | 4 | a | Divisor de una fórmula. |
| `toilet.virus_bar_limit` | 16 | a | Umbral. |
| `toilet.virus_form` | una especie (referencia) | a | Referencia (criterio 2): una sola entidad, sujeto de una regla. |
| `toilet.poop_eaters` | 2 especies | **b** | D2. «Decidido por el coordinador (2026-10-21)»: marca por especie en `data/digimon`. |
| `toilet.eating` | 2 tramos | a | Tramos por tamaño de caca, no por entidad. |
| `toilet.eating_largest.hp_divisor` | 2 | a | Divisor de una fórmula. |
| `toilet.eating_largest.mp_divisor` | 10 | a | Divisor de una fórmula. |
| `toilet.eating_largest.weight` | 10 | a | Incremento. |
| `toilet.eating_largest.cure_percent` | 20 | a | Porcentaje o probabilidad. |
| `toilet.piles_per_map` | 16 | a | Tope. |
| `toilet.piles_total` | 100 | a | Tope. |
| `sickness.missed_sleep_hours` | 10 | a | Temporizador o duración. |
| `sickness.tries_offset` | 10 | a | Término de una fórmula. |
| `sickness.tries_percent` | 5 | a | Porcentaje o probabilidad. |
| `sickness.tiredness_offset` | 50 | a | Umbral. |
| `sickness.disliked_area_hours` | 10 | a | Temporizador o duración. |
| `sickness.injury_hours` | 12 | a | Temporizador o duración. |
| `sickness.untreated_hours` | 12 | a | Temporizador o duración. |
| `sickness.on_sick_happiness` | -20 | a | Incremento. |
| `sickness.hourly_penalty.happiness` | -10 | a | Incremento. |
| `sickness.hourly_penalty.discipline` | -5 | a | Incremento. |
| `sickness.hourly_penalty.tiredness` | 5 | a | Incremento. |
| `mood.scold_discipline` | 2 | a | Incremento. |
| `mood.scold_happiness` | -10 | a | Incremento. |
| `mood.boosted_scold_discipline` | 8 | a | Incremento. |
| `mood.boosted_scold_happiness` | 0 | a | Incremento. |
| `mood.praise_discipline` | -5 | a | Incremento. |
| `mood.praise_happiness` | 2 | a | Incremento. |
| `mood.praise_discipline_divisor` | 10 | a | Divisor de una fórmula. |
| `mood.sulk_check_frames` | 200 | a | Temporizador o duración. |
| `mood.sulk_recovery_frames` | 15 | a | Temporizador o duración. |
| `mood.sulk_stat_loss_frames` | 300 | a | Temporizador o duración. |
| `mood.sulk_stat_loss_percent` | 2 | a | Porcentaje o probabilidad. |
| `mood.area_effect_frames` | 1200 | a | Temporizador o duración. |
| `mood.liked_area.happiness` | 1 | a | Incremento. |
| `mood.liked_area.tiredness` | -2 | a | Incremento. |
| `mood.disliked_area.happiness` | -1 | a | Incremento. |
| `mood.disliked_area.tiredness` | 1 | a | Incremento. |
| `mood.area_timer_limit_frames` | 28800 | a | Temporizador o duración. |
| `tiredness.tired_threshold` | 80 | a | Umbral. |
| `tiredness.tired_happiness` | -2 | a | Incremento. |
| `tiredness.tired_happiness_frames` | 100 | a | Temporizador o duración. |
| `tiredness.sub_tiredness_threshold` | 60 | a | Umbral. |
| `year.happy_happiness` | 100 | a | Umbral. |
| `year.happy_tamer_level_percent` | 5 | a | Porcentaje o probabilidad. |
| `year.unhappy_tamer_level_percent` | 10 | a | Porcentaje o probabilidad. |
| `strategies.care_mistakes` | `original` | a | Nombre de estrategia. |
| `strategies.sickness_roll` | `original` | a | Nombre de estrategia. |
| `strategies.item_refusal` | `original` | a | Nombre de estrategia. |
| `quirks.sleeping_hungry_keeps_weight` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.sleep_adds_default_hours_to_evolution` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.sickness_chance_wraps` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.young_sleep_without_variation` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.battle_midnight_skips_age` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.toilet_doubles_poop_interval` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.food_clears_training_boost` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.skipped_hunger_window_counts_twice` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.skipped_hours_add_sickness_tries` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.skipped_hours_unhappiness_only_at_midnight` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.wake_searches_from_last_meal` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |

### `battle.yaml`

| Clave | Valor | Clase | Motivo |
|---|---|---|---|
| `strategies.partner_ai` | `original` | a | Nombre de estrategia. |
| `strategies.enemy_ai` | `original` | a | Nombre de estrategia. |
| `strategies.damage` | `original` | a | Nombre de estrategia. |
| `strategies.obedience` | `original` | a | Nombre de estrategia. |
| `limits.max_enemies` | 3 | a | Tope. |
| `limits.pending_damage_cap` | 9999 | a | Tope. |
| `limits.stat_cap` | 999 | a | Tope. |
| `damage_tick` | 4 escalones | a | Escalones de la mecánica de la reserva de daño. |
| `affinity.factors` | matriz 7 × 7 por especialidad (`TYPE_FACTORS`, 49 bytes) | **b** | D3. «Decidido por el coordinador (2026-10-21)»: se extrae como tabla de datos (`TableLocation`). |
| `affinity.empty_slot` | 10 | a | Valor neutro. |
| `affinity.move_weight_bonus` | 5 pesos | a | Constantes de código (`BTL_calculateElementBonus`) por valor de afinidad (categoría). |
| `affinity.target_score` | 5 puntuaciones | a | Constantes de código (`BTL_calculateTargetScore`) por valor de afinidad (categoría). |
| `moves.mp_cost_multiplier` | 3 | a | Factor de una fórmula. |
| `moves.finisher_first_index` | 58 | a | Límite de rango como mecánica; con `p4-ids-refs`, rango sobre índices del original. |
| `moves.finisher_last_index` | 112 | a | Límite de rango como mecánica; con `p4-ids-refs`, rango sobre índices del original. |
| `moves.learnable_below_index` | 58 | a | Límite de rango como mecánica; con `p4-ids-refs`, rango sobre índices del original. |
| `moves.counter` | una técnica (referencia) | a | Referencia (criterio 2): una sola entidad, sujeto de una regla. |
| `moves.flattened_shot` | una técnica (referencia) | a | Referencia (criterio 2): una sola entidad, sujeto de una regla. |
| `moves.speed_reserve_floor` | -155 | a | Tope (mínimo). |
| `moves.shared_cooldown_frames` | 110 | a | Temporizador o duración. |
| `moves.buffs` | 5 técnicas con la subida de 3 stats | **b** | Efecto por técnica (switch de `BTL_applyBuffMove`, battle_main.c). Destino: `data/moves` (`buff`), leído con `ConstantSwitch`. |
| `moves.buff_uses.per_brain` | 100 | a | Umbral. |
| `moves.buff_uses.base` | 1 | a | Término de una fórmula. |
| `moves.buff_uses.tiers` | 2 tramos | a | Tramos de inteligencia (categoría). |
| `moves.learned_together` | 2 parejas de técnicas | **b** | Lista por técnica (`learnMove`, utils2.c). Destino: `data/moves` (`learned_with`). |
| `speed_reserve.start` | 100 | a | Valor inicial. |
| `speed_reserve.cap` | 100 | a | Tope. |
| `speed_reserve.gain_every_frames` | 2 | a | Temporizador o duración. |
| `speed_reserve.gain_per_speed` | 100 | a | Término de una fórmula. |
| `speed_reserve.gain_base` | 1 | a | Término de una fórmula. |
| `pacing.after_attack_frames` | 40 | a | Temporizador o duración. |
| `pacing.no_move_frames` | 80 | a | Temporizador o duración. |
| `pacing.ranged_patience_frames` | 80 | a | Temporizador o duración. |
| `pacing.ranged_band_percent` | 30 | a | Porcentaje o probabilidad. |
| `pacing.flattened_shot_every_frames` | 40 | a | Temporizador o duración. |
| `pacing.flattened_shot_frames` | 30 | a | Temporizador o duración. |
| `pacing.flattened_shot_release_frame` | 2 | a | Temporizador o duración. |
| `spacing.contact_margin` | 200 | a | Distancia. |
| `spacing.standing.near_squared` | 0 | a | Distancia. |
| `spacing.standing.far_squared` | 480000 | a | Distancia. |
| `spacing.walking.near_squared` | 160000 | a | Distancia. |
| `spacing.walking.far_squared` | 320000 | a | Distancia. |
| `spacing.keep_away_margin_squared` | 640000 | a | Distancia. |
| `spacing.keep_away_walking_squared` | 160000 | a | Distancia. |
| `obedience.brain_below` | 301 | a | Umbral. |
| `obedience.roll_brain_divisor` | 2 | a | Divisor de una fórmula. |
| `obedience.roll_period_frames` | 20 | a | Temporizador o duración. |
| `obedience.partner_chance_base` | 70 | a | Porcentaje o probabilidad. |
| `obedience.enemy_chance_brain` | 300 | a | Término de una fórmula. |
| `obedience.enemy_chance_divisor` | 4 | a | Divisor de una fórmula. |
| `obedience.idle_frames` | 100 | a | Temporizador o duración. |
| `obedience.retarget_chances` | [70, 40, 30, 0] | a | Por modo de carga (categoría); 4 bytes de `TARGET_ATTACKER_CHANCE`, menos de 9 valores (criterio 3). |
| `obedience.enemy_follow_attacker_percent` | 71 | a | Porcentaje o probabilidad. |
| `obedience.command_delay.low_discipline_below` | 70 | a | Umbral. |
| `obedience.command_delay.low_base` | 160 | a | Término de una fórmula. |
| `obedience.command_delay.low_divisor` | 10 | a | Divisor de una fórmula. |
| `obedience.command_delay.high_base` | 10 | a | Término de una fórmula. |
| `obedience.command_delay.high_divisor` | 10 | a | Divisor de una fórmula. |
| `obedience.command_delay.high_scale` | 10 | a | Factor de una fórmula. |
| `commands.menus` | 6 tramos → órdenes | a | Por tramo de inteligencia (categoría); `BRAIN_TO_COMMAND_MAP` (5 bytes, battle_setup.c), que es el que modela el ruleset, y código. `STD_BRAIN_TO_COMMAND_MAP` (5 bytes, std_main.c, lo usa el código de STD_REL) no es una copia: es una variante con otros valores, y el ruleset no la modela. |
| `partner_ai.status_target_brain` | 300 | a | Umbral. |
| `partner_ai.switch_margin` | 40 | a | Umbral. |
| `partner_ai.scored_target_brain` | 150 | a | Umbral. |
| `partner_ai.power_weights` | [50, 20, 5, 0] | a | Pesos por puesto (`BTL_YOUR_CALL_POWER_PRIO`, 4 bytes): categoría, menos de 9 valores. |
| `partner_ai.mp_weights` | [50, 20, 10, 0] | a | Pesos por puesto (`BTL_YOUR_CALL_MP_PRIO`, 4 bytes): categoría, menos de 9 valores. |
| `partner_ai.reach_weights` | [10, 5, 0, 0] | a | Pesos por puesto (`BTL_YOUR_CALL_WIDE_PRIO`, 4 bytes): categoría, menos de 9 valores. |
| `partner_ai.even_weight` | 20 | a | Peso de la IA. |
| `partner_ai.empty_slot_weight` | 5 | a | Peso de la IA. |
| `partner_ai.area_brain` | 200 | a | Umbral. |
| `partner_ai.area_min_enemies` | 2 | a | Umbral. |
| `partner_ai.area_weight_per_enemy` | [10, 15, 15] | a | Pesos por modo (categoría). |
| `partner_ai.low_hp_percent` | 31 | a | Porcentaje o probabilidad. |
| `partner_ai.buff_priority_brain_divisor` | 10 | a | Divisor de una fórmula. |
| `partner_ai.buff_priority_base` | 5 | a | Término de una fórmula. |
| `partner_ai.buff_priority_decay_frames` | 20 | a | Temporizador o duración. |
| `partner_ai.confused_air_percent` | 70 | a | Porcentaje o probabilidad. |
| `partner_ai.confused_swing_percent` | 5 | a | Porcentaje o probabilidad. |
| `partner_ai.confused_turn_roll` | 10 | a | Rango de una tirada. |
| `partner_ai.confused_turn_from` | 8 | a | Umbral. |
| `enemy_ai.area_weight` | 20 | a | Peso de la IA. |
| `hit.always_hit_below_level` | 3 | a | Umbral. |
| `hit.accuracy_divisor` | 2 | a | Divisor de una fórmula. |
| `hit.attacker_speed_divisor` | 10 | a | Divisor de una fórmula. |
| `hit.speed_scale` | 999 | a | Factor de una fórmula. |
| `hit.standing_evasion_percent` | 120 | a | Porcentaje o probabilidad. |
| `hit.block_frames` | 3 | a | Temporizador o duración. |
| `hit.blocked_melee_min_percent` | 10 | a | Porcentaje o probabilidad. |
| `hit.blocked_melee_max_percent` | 30 | a | Porcentaje o probabilidad. |
| `hit.counter_min_mp` | 165 | a | Umbral. |
| `hit.big_hit_percent` | 20 | a | Porcentaje o probabilidad. |
| `damage.affinity_divisor` | 30 | a | Divisor de una fórmula. |
| `damage.difference_cap` | 500 | a | Tope. |
| `damage.power_scale` | 500 | a | Factor de una fórmula. |
| `damage.variance_min` | 90 | a | Rango de una tirada. |
| `damage.variance_max` | 110 | a | Rango de una tirada. |
| `damage.counter_defense_percent` | 30 | a | Porcentaje o probabilidad. |
| `damage.finisher_charge_divisor` | 40 | a | Divisor de una fórmula. |
| `damage.enemy_finisher_min` | 100 | a | Rango de una tirada. |
| `damage.enemy_finisher_max` | 200 | a | Rango de una tirada. |
| `damage.min` | 1 | a | Tope (mínimo). |
| `damage.max` | 9999 | a | Tope. |
| `status.poison_every_frames` | 100 | a | Temporizador o duración. |
| `status.poison_min_percent` | 1 | a | Porcentaje o probabilidad. |
| `status.poison_max_percent` | 3 | a | Porcentaje o probabilidad. |
| `status.confusion_min_frames` | 200 | a | Temporizador o duración. |
| `status.confusion_max_frames` | 300 | a | Temporizador o duración. |
| `status.stun_min_frames` | 200 | a | Temporizador o duración. |
| `status.stun_max_frames` | 240 | a | Temporizador o duración. |
| `status.flatten_transition_frames` | 65 | a | Temporizador o duración. |
| `status.flatten_min_frames` | 224 | a | Temporizador o duración. |
| `status.flatten_max_frames` | 304 | a | Temporizador o duración. |
| `finisher.goal_base` | 3000 | a | Término de una fórmula. |
| `finisher.per_frame` | 1 | a | Temporizador o duración. |
| `finisher.per_attack` | 2 | a | Término de una fórmula. |
| `finisher.per_block` | 3 | a | Término de una fórmula. |
| `finisher.per_blocked_attack` | 4 | a | Término de una fórmula. |
| `finisher.progress_divisor` | 50 | a | Divisor de una fórmula. |
| `finisher.aura_frames` | 80 | a | Temporizador o duración. |
| `finisher.charge_per_press` | 2 | a | Término de una fórmula. |
| `finisher.charge_cap` | 80 | a | Tope. |
| `condition.stat_penalty_percent` | 20 | a | Porcentaje o probabilidad. |
| `condition.mp_cost_extra_percent` | 50 | a | Porcentaje o probabilidad. |
| `condition.mp_discounts` | 4 tramos | a | Tramos de inteligencia (categoría). |
| `ending.flee_frames` | 41 | a | Temporizador o duración. |
| `ending.flee_ignores_hits_from` | 19 | a | Umbral. |
| `ending.revive_frames` | 171 | a | Temporizador o duración. |
| `ending.revive_countdown_shown_at` | 20 | a | Umbral. |
| `item_throw.release_frame` | 7 | a | Temporizador o duración. |
| `item_throw.flight_frames` | 24 | a | Temporizador o duración. |
| `hud.enemy_hp_bar_frames` | 21 | a | Temporizador o duración. |
| `consequences.victory.happiness` | 2 | a | Incremento. |
| `consequences.victory.discipline` | 0 | a | Incremento. |
| `consequences.victory.tiredness` | 0 | a | Incremento. |
| `consequences.victory.battles` | 1 | a | Incremento de una mecánica. |
| `consequences.flee.happiness` | -10 | a | Incremento. |
| `consequences.flee.discipline` | -6 | a | Incremento. |
| `consequences.flee.tiredness` | 2 | a | Incremento. |
| `consequences.flee.battles` | 0 | a | Incremento de una mecánica. |
| `consequences.defeat.happiness` | -30 | a | Incremento. |
| `consequences.defeat.discipline` | -20 | a | Incremento. |
| `consequences.defeat.tiredness` | 0 | a | Incremento. |
| `consequences.defeat.battles` | 0 | a | Incremento de una mecánica. |
| `consequences.after_battle_tiredness` | 5 | a | Incremento. |
| `consequences.after_battle_food` | -15 | a | Incremento. |
| `consequences.mp_tiredness_below_percent` | 80 | a | Porcentaje o probabilidad. |
| `consequences.mp_tiredness_scale` | 10 | a | Factor de una fórmula. |
| `consequences.defeat_lives` | 1 | a | Incremento. |
| `consequences.defeat_restore` | HP 9999, MP 9999 | a | Respaldo del efecto del guion de derrota. |
| `consequences.elapsed_minutes` | 20 | a | Temporizador o duración. |
| `rewards.enemy_factors` | [10, 12, 16] | a | Por número de enemigos (`STAT_GAIN_ENEMY_FACTORS`): categoría, menos de 9 valores. |
| `rewards.partner_scale` | 10 | a | Factor de una fórmula. |
| `rewards.fallback.hp_lost_percent` | 100 | a | Porcentaje o probabilidad. |
| `rewards.fallback.attacks` | 10 | a | Incremento. |
| `rewards.fallback.big_hits` | 10 | a | Incremento. |
| `rewards.fallback.blocks` | 10 | a | Incremento. |
| `rewards.fallback.speed_hp_lost_percent` | 50 | a | Porcentaje o probabilidad. |
| `rewards.fallback.brain_attacks` | 5 | a | Umbral. |
| `rewards.fallback.brain_big_hits` | 5 | a | Umbral. |
| `rewards.no_drop_maps` | lista de mapas (hoy con una entrada) | **b** | Criterio 2, «Decidido por el coordinador (2026-10-21)»: es una lista de mapas, que crece por mapa; pasa a una marca por mapa. Destino: marca por mapa en `data/maps` (`no_drops`), leída de `battleStatsGainsAndDrops` (battle_ui.c, ejecutable principal). |
| `rewards.tracked_moves` | 12 | a | Tamaño. |
| `joining.chances` | matriz 3 × 3 por tipo (`FLEE_CHANCES`, 12 bytes) | **b** | D3. «Decidido por el coordinador (2026-10-21)»: se extrae como tabla de datos (`TableLocation`). |
| `joining.untyped_chance` | 70 | a | Porcentaje o probabilidad. |
| `joining.bell_item` | un objeto (referencia) | a | Referencia (criterio 2): una sola entidad, sujeto de una regla. |
| `joining.bell_roll` | -50 | a | Rango de una tirada. |
| `joining.repel_item` | un objeto (referencia) | a | Referencia (criterio 2): una sola entidad, sujeto de una regla. |
| `joining.repel_roll` | 20 | a | Rango de una tirada. |
| `timing.attack_frames` | 30 | a | Valor del motor (aproximado, a falta de medirlo), no del original. |
| `timing.hit_frame` | 12 | a | Valor del motor (aproximado, a falta de medirlo), no del original. |
| `timing.hit_reaction_frames` | 20 | a | Valor del motor (aproximado, a falta de medirlo), no del original. |
| `timing.block_animation_frames` | 15 | a | Valor del motor (aproximado, a falta de medirlo), no del original. |
| `timing.faint_frames` | 40 | a | Valor del motor (aproximado, a falta de medirlo), no del original. |
| `timing.walk_speed` | 22 | a | Valor del motor (aproximado, a falta de medirlo), no del original. |
| `timing.projectile_speed` | 60 | a | Valor del motor (aproximado, a falta de medirlo), no del original. |
| `timing.arena_radius` | 2400 | a | Valor del motor (aproximado, a falta de medirlo), no del original. |
| `quirks.broken_mp_tiredness` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.command_delay_discarded` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.sickness_always_hits` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.smart_menu_drops_attack_and_moderate` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.counter_needs_mp` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.low_hp_weights_loop_bug` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.target_score_uses_own_stats` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.weakest_reports_last_score` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.confusion_waits_for_partner_attack` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.scripted_enemies_free_mp` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.injury_counts_any_hit` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.scripted_battle_everyone_joins` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |
| `quirks.mp_check_and_payment_round_differently` | `true` | a | Bandera de rareza: dice si se reproduce un comportamiento. |

---

## Otras constantes del original fuera de los rulesets

Búsqueda en `engine/`, `game/`, `apps/`, `formats/`, `content/` (fuera de `rules/original/`) y `tests/` de comentarios
que citan el decomp, de tablas de constantes y de asserts de tests que repiten valores del ruleset `original`. Un
assert que repite un valor (b) es (b); uno que solo mira un tamaño o que algo existe es (a). La mayoría de las citas en `game/` y `apps/session` explican
comportamiento y no guardan valores; esas no se listan. Una línea por constante o grupo de constantes del mismo tipo.

**Cómo se buscaron los asserts de tests.** Para cada tabla (b) se sacaron del ruleset todas sus referencias (ids de
especie, técnica, objeto y mapa, números de objeto y rangos) y se buscaron en todos los ficheros de `*/tests/`, en
`tests/` y en `apps/sim/scenarios/`: como texto, como las constantes con nombre de los fixtures que valen esos
números de objeto (`Bun`, `Charm`, `BigCrest`…) y por los nombres de los campos que las leen (`babyTargets`,
`scaledTargets`, `eatsPoop`, `sleepyHour`, `sortOrders`, `noDropMaps`, `scriptedWarps`…). Cada resultado se revisó a
mano: es (b) si el test pasa solo porque el ruleset `original` trae ese valor o esa entidad en la lista (también si
depende de que una entidad **no** esté); es (a) si el valor lo pone el propio test, si sale de una tabla inventada del
fixture, o si solo mira tamaños o que algo existe. Los fixtures que repiten una lista (b) cuentan como asserts.
Además se revisaron los tests de fórmulas que corren con `originalRules()` por si su resultado esperado depende de
una celda de una matriz (`TYPE_FACTORS`, `FLEE_CHANCES`): dependen `battle_tests.cpp:250-260` (daño) y
`battle_tests.cpp:448-457` (unirse al combate); los que solo cuentan resultados de combates enteros con stats
desiguales (`runBattles`) no.

**Criterio para las direcciones y los tamaños** (D9, «Decidido por el coordinador (2026-10-21)»): las direcciones de tablas y funciones, los tamaños de
registro, los opcodes y las posiciones de los registros son **conocimiento del formato** que hace falta para leer el
ejecutable del usuario; no contienen sus datos. Son (a), como `ExecutableLayout`, que `dwx` ya usa por versión.

### Cómo localiza `dwx` las tablas por versión

La versión sale del SHA-1 del ejecutable (`formats/src/disc/known_releases.cpp`: us, eu, jp, jp_rev1). Con ella,
`formats::exe::findExecutableLayout` (`formats/src/exe/executable_layout.cpp`) da la dirección y el número de entradas
de cada tabla (`TableLocation`), y `apps/dwx/src/game_tables.cpp: readGameTables` las lee. Los valores que están en el
código y no en una tabla de datos se leen ejecutando un switch de constantes (`ConstantSwitch`,
`formats/include/dwe/formats/exe/constant_switch.hpp`: `handleFood`, `handleChips`). Los overlays tienen su propio
layout (`formats/src/exe/overlay.cpp`, `overlay_tables.cpp`). Solo la versión americana está localizada; las otras
fallan con un error claro hasta que alguien rellene sus direcciones desde `config/<versión>/symbols.txt` del decomp.

| Dónde | Qué | Clase | Motivo |
|---|---|---|---|
| `formats/src/exe/executable_layout.cpp` | direcciones y número de entradas de unas 37 tablas, 9 manejadores de objetos y los índices del primer objeto de cada tabla de efectos (US) | a | Formato (D9). |
| `formats/src/exe/executable_layout.cpp: UsFoodValues, UsChipValues` | dónde están los valores locales de `handleFood` y `handleChips` (18 y 7 registros o huecos de pila) | a | Formato (D9): dice dónde leer, no qué vale. |
| `formats/src/exe/tables.cpp` | tamaño de cada registro (DigimonPara, Move, Item, EvoRequirements...) | a | Formato. |
| `formats/src/exe/overlay.cpp`, `overlay_tables.cpp` | cabeceras, direcciones de carga, rangos de `SHOP_REL.BIN`, tablas de los overlays (US) | a | Formato (D9). |
| `formats/src/disc/file_table.cpp`, `formats/src/video/movie.cpp`, `formats/src/text/font.cpp`, `formats/src/image/tim_packs.cpp`, `formats/src/audio/vhb.cpp` | direcciones, capacidades y pasos de tablas de ficheros, vídeos, fuente, TIM y sonido | a | Formato. |
| `formats/src/effect/efe_script.cpp`, `efe_subops.cpp`, `efe_file.cpp`, `efe_analysis.cpp` | 18 opcodes y 0x61 subórdenes de efectos con sus argumentos; disposición de VRAM | a | Formato del lenguaje de efectos. |
| `formats/src/script/disassembly.cpp` | 105 opcodes de guion con sus operandos | a | Formato del lenguaje de guiones. |
| `formats/src/script/new_game_stats.cpp` | el guion 193 y 4 opcodes | a | Formato: dónde leer los stats iniciales (los valores salen del disco). |
| `formats/src/text/game_text.cpp` | bytes de control del texto | a | Formato. |
| `formats/src/map/map_file.cpp` | topes de un mapa (35 tiles, 188 instancias, 8 NPC) y la ruta `MAP<id/15+1>` | a | Formato. |
| `formats/src/text/string_manifest.cpp` | US: 25 bloques de cadenas y unas 24 listas de direcciones de mensajes concretos | a | D9. «Decidido por el coordinador (2026-10-21)»: localizadores por mensaje (formato). |
| `formats/src/text/charset.cpp: NonKanjiRows` | tabla cp932 | a | Estándar de codificación, no del juego. |
| `formats/include/dwe/formats/exe/item_effects.hpp` y sus copias en `engine/assets`, `game/inventory` | nombres de 10 manejadores, 18 valores de comida y 7 de chip | a | Nombres propios de campos (esquema). |
| `apps/dwx/src/audio_extractors.cpp` | huecos VAB, nota base, tipos con música de enemigo, pistas que suenan una vez | a | Formato del sonido. |
| `apps/dwx/src/model_extractors.cpp: KnownScenes, RuntimeTextures` | unos 30 modelos → TIM, mapa o rutina donde los muestra el original | a | D9. «Decidido por el coordinador (2026-10-21)»: localizadores por fichero (formato). |
| `apps/dwx/src/model_extractors.cpp`, `map_extractors.cpp`, `image_extractors.cpp`, `padding_extractor.cpp` | filas de CLUT, sectores por modelo, TIM apilados, rangos de relleno de `MMM.DAT` | a | Formato. |
| `engine/assets/include/dwe/assets/map_images.hpp`, `map_document.hpp` | páginas y filas de VRAM, orden del mapa, colisión 100 × 100, 3 luces, 10 warps | a | Formato. |
| `engine/ui/src/minimap.cpp`, `game/field/src/field.cpp` | triggers de warp 110 a 119 | a | Rango de mecánica. |
| `engine/render/include/dwe/render/map_camera.hpp`, `map_layers.hpp`, `map_renderer.hpp` | 320 × 240, plano cercano 100, paso de OT 16, 20 fps, fundido de 25 | a | Mecánica de presentación. |
| `engine/ruleset/include/dwe/ruleset/game_clock.hpp: ClockRules` | 20 fps, 20 fotogramas por minuto, 30 días, 100 años, 8:00 | a | Repite `clock.yaml` como valores por defecto. |
| `game/field/include/dwe/game/field/rules.hpp:27-29` | radio y velocidades de andar y correr por defecto | **b** | D11. «Decidido por el coordinador (2026-10-21)»: valores de una especie; la cabecera entra en el paso de D11 de la migración. |
| `game/field/include/dwe/game/field/rules.hpp` (el resto) | distancias en radios, fundidos, horas del día, luces por defecto | a | Repite la parte (a) de `field/rules.yaml`. |
| `game/battle` (`combatant.hpp`, `state.hpp`, `moves.hpp`, `rules.hpp`, `battle.hpp`, `movement.cpp`, `targeting.cpp`, `original_partner_ai.cpp`, `fighter_actions.cpp`, `ai_ticks.cpp`) | 3 + 1 ranuras, 16 técnicas por especie, animación 0x2E, numeración de órdenes, bits de técnica, 7 especialidades, centinelas | a | Estructura y mecánica. |
| `game/partner/src/poop_piles.cpp`, `game/field/src/field.cpp` | bandas de orientación | a | Mecánica. |
| `game/evolution/include/dwe/game/evolution/tables.hpp` | centinela de requisito y bits 0x01/0x10 | a | Formato. |
| `apps/screens/src/partner_status_view.cpp`, `bag_view.cpp`; `apps/game/src/field_scene.cpp` | topes de barra 999 y 9999, color 255, luz ambiente 0,5 | a | Mecánica de presentación. |
| `apps/session/src/battle_adapter.cpp`, `evolution_adapter.cpp` | 0x2E/0xFF al renacer; topes 9999/999 | a | Estructura; los topes vienen de `care.limits`. |
| `apps/sim/scenarios/care_scenario.cpp: MealEnergy, MealWeight` | la energía y el peso que da la carne del original | **b** | Efecto de un objeto concreto copiado en C++ (`handleFood`). Destino: leerlo de `data/item_effects` o usar valores inventados. |
| `apps/sim/tests/stand-in/*.yaml`, `tests/mods/example_mod/data/*.yaml` | especies y técnicas inventadas, con ids e índices del original | a | Inventados. «Decidido por el coordinador (2026-10-21)»: una prueba con disco comprueba que ningún valor de un fixture coincide con la tabla real de su entidad. |
| `content/packs/game/field/rules.yaml: partner.radius, walk_speed, run_speed`; `player.radius, walk_speed, run_speed` | radio y velocidades del partner inicial y del jugador | **b** | D11. «Decidido por el coordinador (2026-10-21)»: radio de `data/digimon`, velocidades del movimiento raíz del modelo. |
| `content/packs/game/field/rules.yaml` (el resto) | distancias en radios, fundidos, horas del día, luces | a | Mecánica. |
| `content/packs/game/field/warps.yaml: start` | mapa y salida de inicio | a | Referencia (criterio 2). |
| ~~`content/packs/game/field/warps.yaml: scripted_warps`~~ | 1 warp de guion (mapa, trigger, destino, flag) | **b** | D10. «Decidido por el coordinador (2026-10-21)». **Borrado** en p4-scripted-warps (2026-10-21): lo sustituye `field/script_warps`, que extrae `dwx` del disco del usuario. |
| `content/packs/game/models/partner.yaml`, `tamer.yaml` | ids de animación 0 a 4 | a | Estructura. |
| `content/packs/game/save/settings.yaml: manual_slots` | 15 | a | Tamaño. |
| `content/packs/game/ui/battle/logic.lua`, `ui/partner_menu/logic.lua` | 11 órdenes como máximo, 30 huecos | a | Tamaños; repiten el ruleset. |
| `content/packs/es-translation/text/es/*.yaml` | traducción propia, por bloque e índice del original, con los SHA-1 del texto fuente | — | Fuera de esta auditoría: texto derivado. «Decidido por el coordinador (2026-10-21)»: pasa a un repositorio de contenido aparte (paquete `p4-translation-split`). No cuenta en el recuento. |
| Tests que repiten direcciones y recuentos US (`formats/tests/exe_tables_tests.cpp`, `file_table_tests.cpp`, `overlay_tables_tests.cpp`, `game/evolution/tests/extracted_tests.cpp`) | direcciones, recuentos (44 objetos desde 0x47) | a | Formato (D9). |
| `content/schemas/*.json` | descripciones que citan funciones del decomp | a | Sin valores. |
| `game/partner/tests/rules_tests.cpp:51-52, 60-63` | horas de cuatro horarios de `SLEEP_PATTERN` | **b** | Repite valores (b) (D3). |
| `game/partner/tests/rules_tests.cpp:65` | stats del primer bebé al renacer | **b** | Repite valores (b). |
| `game/partner/tests/rules_tests.cpp:50, 58` | tamaños (8 horarios, 4 bebés) | a | Solo tamaños. |
| `game/evolution/tests/rules_tests.cpp:42` | el destino de un bebé | **b** | Repite valores (b). |
| `game/evolution/tests/rules_tests.cpp:64` | los 7 bloques por especialidad | **b** | Repite valores (b) (D4). |
| `game/evolution/tests/rules_tests.cpp:41, 54-55, 60, 65` | tamaños de las listas | a | Solo tamaños. |
| `game/evolution/tests/specials_tests.cpp:53-142` | destinos y orígenes de las evoluciones especiales | **b** | Repite valores (b) (D1). |
| `game/evolution/tests/effects_tests.cpp:141` | las cuatro especies del renacer | **b** | Repite valores (b). |
| `game/battle/tests/rules_tests.cpp:17` | una celda de `TYPE_FACTORS` | **b** | Repite valores (b) (D3). |
| `game/battle/tests/rules_tests.cpp:99-100` | que una técnica tiene subida y otra una pareja | a | Solo existencia y tamaño. |
| `game/battle/tests/battle_tests.cpp:413` | una pareja de técnicas que se aprenden juntas | **b** | Repite valores (b). |
| `game/inventory/tests/rules_tests.cpp:55` | tamaño de `sortOrders` | a | Solo tamaño. |
| `game/inventory/tests/effects_tests.cpp:32-33, 42, 44, 48, 54, 84` | disquete doble, curas, revivir, disco y sección de guion | **b** | Repite valores (b) (D6 y efectos por objeto). |
| `game/partner/tests/sleep_tests.cpp:20-21, 31` | `SleepyHour` y `WakeHour` (un horario de `SLEEP_PATTERN`) y sus horas despierto; se comprueban en :29-30, :59, :85, :88 y :91, y el resto del fichero juega hasta `SleepyHour` | **b** | Repite valores (b) (D3). |
| `game/partner/tests/sleep_tests.cpp:41-45` | las horas de sueño de un bebé y de un entrenamiento, que salen de sus horarios | **b** | Repite valores (b) (D3). |
| `game/partner/tests/hunger_tests.cpp:172-185` | la hora de despertar de un horario, de la que depende la comida siguiente | **b** | Repite valores (b) (D3). |
| `game/partner/tests/life_tests.cpp:180` | el primer bebé de la lista del renacer | **b** | Repite valores (b). |
| `apps/session/tests/session_tests.cpp:373-374` | los cuatro bebés del renacer | **b** | Repite valores (b). |
| `game/partner/tests/partner_fixture.hpp:129`, `apps/session/tests/session_fixture.hpp:103`, `apps/session/tests/example_mod_tests.cpp:110` | fixtures que crean las especies de los cuatro bebés del renacer, en su orden | **b** | Repite valores (b). |
| `game/partner/tests/toilet_tests.cpp:152-153, 167` | que una especie come caca y otra no | **b** | Repite valores (b) (D2). |
| `game/evolution/tests/selection_tests.cpp:62-67, 108-116` | el destino de un bebé, y un bebé que se quita de `baby_targets` | **b** | Repite valores (b). |
| `game/evolution/tests/state_tests.cpp:150-160` | que un bebé tiene destino de entrenamiento | **b** | Repite valores (b). |
| `game/evolution/tests/effects_tests.cpp:58-66` | los cambios de stats de dos especies de `scaled_targets` | **b** | Repite valores (b) (D2). |
| `apps/session/tests/evolution_adapter_tests.cpp:91-99` | el destino de un bebé (`baby_targets`) | **b** | Repite valores (b). |
| `apps/session/tests/evolution_adapter_tests.cpp:201-203, 231-243` | el destino fijo de un objeto de evolución (`items.fixed`) | **b** | Repite valores (b). |
| `game/inventory/tests/effects_tests.cpp:77-83, 86` | un chip de la lista de nivel de tamer, y uno que queda fuera de las listas de chips | **b** | Repite valores (b) (D7). |
| `game/inventory/tests/effects_tests.cpp:111-114` | el destino fijo de un objeto de evolución | **b** | Repite valores (b). |
| `game/inventory/tests/use_tests.cpp:91, 100-101` | un chip que queda fuera de las listas de chips y no hace nada | **b** | Repite valores (b) (D7). |
| `game/inventory/tests/bag_tests.cpp:114-129` | dos órdenes de `ITEM_SORT_ORDER`, en comentarios y en el orden esperado | **b** | Repite valores (b) (D3). |
| `apps/session/tests/inventory_adapter_tests.cpp:162-171` | un objeto de `refusal.never_taken` | **b** | Repite valores (b) (D5). |
| `apps/session/tests/inventory_adapter_tests.cpp:173-190` | el nivel de aceptación de un objeto de evolución fija (`refusal.fixed_level_evolutions`) | **b** | Repite valores (b). |
| `game/battle/tests/battle_tests.cpp:239-245, 349-354` | una técnica de `moves.buffs` y su porcentaje de subida | **b** | Repite valores (b). |
| `game/battle/tests/battle_tests.cpp:433-436` | el mapa de `rewards.no_drop_maps` | **b** | Repite valores (b). |
| `game/battle/tests/battle_tests.cpp:250-260` | el rango de daño esperado, que sale de una celda de la diagonal de `TYPE_FACTORS` | **b** | Repite valores (b) (D3). |
| `game/battle/tests/battle_tests.cpp:448-457` | cuántos enemigos se unen, que sale de dos celdas de `FLEE_CHANCES` | **b** | Repite valores (b) (D3). |
| `game/field/tests/rules_tests.cpp:150-152` | el warp de guion: cuántos hay, su trigger y su salida | **b** | Repite valores (b) (D10). |

---

## Resumen

### Recuento por clase

Tras las decisiones del coordinador no queda ningún (?). Antes de decidir eran 21 (16 en los rulesets y 5 fuera). La
fila de la traducción no cuenta.

| Ámbito | (a) regla | (b) tabla | Líneas |
|---|---|---|---|
| Rulesets (`rules/original/*.yaml`) | 439 | 27 | 466 |
| Otras constantes del original (código, contenido y tests) | 38 | 36 | 74 |
| **Total** | **477** | **63** | **540** |

Casi todo es (a): los rulesets se escribieron ya con la idea de que las tablas por entidad son del paquete base
(`tables:` en `ruleset.yaml`). Las (b) que quedan son tablas pequeñas que en el original no están en una tabla de
datos sino **dentro del código** (argumentos de llamadas, casos de un switch, comparaciones), y por eso se copiaron al
ruleset. Las decisiones D1 a D11 suman a (b) tablas pequeñas de categorías con símbolo propio, conjuntos de
especies u objetos con una conducta y los valores de campo de una especie.

### Tablas (b) y su destino

Ninguna se copia aquí. "Fichero" dice de qué fichero del disco del usuario sale (el ejecutable principal o un
overlay); "Cómo", qué mecanismo de lectura necesita `dwx` (ver la fase B). Las funciones y símbolos se nombran solo como
localizadores.

| Valor | Fichero | Dónde | Cómo | Destino en el paquete base |
|---|---|---|---|---|
| `life.yaml: rebirth.babies` (stats) | ejecutable principal | argumentos de `setReincarnateStats` en `initializeReincarnatedPartner` (partner_impl.c) | constantes de código | `data/rebirth`: stats por bebé |
| `life.yaml: rebirth.babies` (especies) y `evolution.yaml: rebirth.babies` | ejecutable principal (datos pequeños; lo declara dooa.c y lo lee el código de DOOA_REL) | `REINCARNATE_BABY_TYPE` (8 bytes) | tabla de datos | `data/rebirth`: especie por bebé |
| `evolution.yaml: baby_targets` | ejecutable principal | switch de `getFreshEvolutionTarget` (evolution.c) | switch de constantes | `data/evolution`: `fresh_targets` |
| `evolution.yaml: items.fixed` y `inventory.yaml: effects.evolution.fixed` | ejecutable principal | `handleEvoItems` (item.c), objetos ≥ 0x7d | constantes de código | `data/item_effects`: `evolves_to` |
| `item_use.yaml: refusal.fixed_level_evolutions` | ejecutable principal | `partnerWillRefuseItem` (partner_impl.c) | constantes de código | `data/item_effects`: `accepted_level` |
| `inventory.yaml: effects.status` | ejecutable principal | casos de `handleStatusItems` (item.c) | switch de constantes | `data/item_effects`: `battle_status_cure`, `protects`, `injury_cure`, `sickness_cure` |
| `inventory.yaml: effects.revive` | ejecutable principal | `handleRestore` (item.c) | constantes de código | `data/item_effects`: `revive_hp_percent`, `battle_status_cure` |
| `inventory.yaml: effects.battle_disk.disks` | ejecutable principal | switch de `handleBuffDisks` (main.c) | switch de constantes | `data/item_effects`: `battle_buff` |
| `inventory.yaml: effects.chip.script` | ejecutable principal | `handleChips`, caso 0x16 (item.c) | constantes de código | `data/item_effects`: `script_section` |
| `battle.yaml: moves.buffs` | BTL_REL, con copias en STD_REL y VS_REL | switch de `BTL_applyBuffMove`, `STD_applyBuffMove`, `VS_applyBuffMove` (battle_main.c y sus gemelos) | switch de constantes, con el layout de overlays | `data/moves`: `buff`; `dwx` comprueba que las tres copias dan lo mismo |
| `battle.yaml: moves.learned_together` | ejecutable principal | `learnMove` (utils2.c) | constantes de código | `data/moves`: `learned_with` |
| `battle.yaml: rewards.no_drop_maps` (criterio 2) | ejecutable principal | `battleStatsGainsAndDrops` (battle_ui.c) | constantes de código | `data/maps`: `no_drops` |
| `apps/sim/scenarios/care_scenario.cpp: MealEnergy, MealWeight` | ejecutable principal | `handleFood` (ya extraído) | ya existe | leer `data/item_effects`, o valores inventados |
| `evolution.yaml: specials.*` (D1) | ejecutable principal | `handleSpecialEvolutions` (evolution.c) | constantes de código | `data/evolution`: `specials` |
| `evolution.yaml: stat_gains.scaled_targets` (D2) | EVL_REL | `EVL_applyEvolution` (evl.c) | constantes de código, con el layout de overlays | `data/digimon`: `evolution_scales_stats` |
| `care.yaml: toilet.poop_eaters` (D2) | ejecutable principal | `tickPoopDetection` (partner_impl.c) | constantes de código | `data/digimon`: `eats_poop` |
| `battle.yaml: affinity.factors`, `joining.chances` (D3) | ejecutable principal | `TYPE_FACTORS`, `FLEE_CHANCES` | tabla de datos | `data/battle`: `affinity`, `join_chances` |
| `care.yaml: sleep.patterns` (D3) | ejecutable principal | `SLEEP_PATTERN` | tabla de datos | `data/raise`: `sleep_patterns` |
| `inventory.yaml: sort.orders` (D3) | ejecutable principal | `ITEM_SORT_ORDER` | tabla de datos | `data/items`: `sort_orders` |
| `evolution.yaml: moves.specialty_blocks` (D4) | — | — | se deriva al cargar | de `data/moves` (`special`); no se guarda |
| `item_use.yaml: refusal.never_taken`, `refusal.discipline_checked_below`, `meals` (D5) | ejecutable principal | `partnerWillRefuseItem`, `partnerHandleFoodFeed` (partner_impl.c) | constantes de código, o derivado | `data/item_effects`: `refusal`, `meal` (`meal` se deriva de los objetos con manejador de comida si coincide) |
| `inventory.yaml: effects.double_floppy` (D6) | ejecutable principal | `handleDoubleFloppy` (item.c) | constantes de código | `data/item_effects`, junto a `HEALING_VALUES` |
| `inventory.yaml: effects.chip.tamer_level.items` (D7) | ejecutable principal | `handleChips` (item.c) | switch de constantes | `data/item_effects`: `chip_effect` |
| ~~`field/warps.yaml: scripted_warps`~~ (D10) | el guion del mapa | — | `field/script_warps` (dwx) | borrado en p4-scripted-warps |
| `field/rules.yaml` y `game/field/include/dwe/game/field/rules.hpp:27-29`: radio y velocidades (D11) | ejecutable principal y los modelos | `DIGIMON_DATA.radius`; movimiento raíz de las animaciones | ya extraído | `data/digimon` y los modelos |
| Asserts de tests que repiten valores (b) (ver «Otras constantes») | — | — | — | valores inventados en los paquetes de sustitución, o pruebas con disco |

Los símbolos y funciones de estas tablas se nombran solo para localizar los datos en el disco del usuario: ni el
código del decomp ni los valores de las tablas están en este documento ni en git.

## Decisiones del coordinador

Las tomó el coordinador, en quien el usuario delegó estas decisiones. Cada una: «Decidido por el coordinador (2026-10-21)».

- **Criterios de interpretación** (los cuatro de arriba): aceptados tal como están escritos.
- **Una sola entidad o una lista** (regla del criterio 2): «una sola referencia a una entidad es (a); una lista de
  dos o más entidades es (b)». Por eso `effects.chip.toilet` (un objeto) es (a), los chips de nivel del domador
  (tres objetos) son (b), `rewards.no_drop_maps` pasa a (b) como marca por mapa en `data/maps`, y `bell_item` y
  `repel_item` siguen en (a).
- **D1. Evoluciones especiales** (`evolution.yaml: specials.wake_up`, `specials.praise_scold`; 7 reglas en
  `handleSpecialEvolutions`): **(b)**, a `data/evolution: specials`. Motivo: son un mapa de evolución especie →
  especie. El ruleset se queda con `roll_limit`, el momento de cada modo y la estrategia.
- **D2. Conjuntos de especies con una conducta** (`stat_gains.scaled_targets`, `toilet.poop_eaters`): **(b)**, marcas
  por especie en `data/digimon` (`evolution_scales_stats`, `eats_poop`). Motivo: asignan una conducta a varias
  especies, y una especie nueva de un mod declara la marca en su fila.
- **D3. Tablas de categorías con símbolo propio** (`TYPE_FACTORS`, `FLEE_CHANCES`, `SLEEP_PATTERN`,
  `ITEM_SORT_ORDER`): **se extraen** a `data/battle` (`affinity`, `join_chances`), `data/raise: sleep_patterns` y
  `data/items: sort_orders`. No se quedan como (a). Motivo: legalmente es lo más limpio y cuesta una `TableLocation`
  cada una.
- **D4. Bloques de técnicas por especialidad** (`moves.specialty_blocks`): **se derivan al cargar** de `data/moves`
  (el campo `special` de cada técnica aprendible). Motivo: así no se guardan en ningún sitio.
- **D5. Rangos de objetos** (`refusal.never_taken`, `refusal.discipline_checked_below`, `meals`): **(b)**, marcas por
  objeto en `data/item_effects` (`refusal: never | discipline`, `meal`). Antes se comprueba si `meals` coincide con
  los objetos cuyo manejador es la comida; si coincide, se deriva y no se lee nada. Motivo: son propiedades por
  objeto.
- **D6. Disquete doble** (`effects.double_floppy`): **(b)**, en `data/item_effects` junto a `HEALING_VALUES`. Motivo:
  es la cantidad que cura un objeto.
- **D7. Chips de nivel del domador** (`effects.chip.tamer_level.items`, tres objetos): **(b)**, `chip_effect` por
  objeto en `data/item_effects`; la probabilidad y la cantidad siguen en el ruleset (a). Motivo: es una lista de
  objetos. El chip del baño (`effects.chip.toilet`) es una sola referencia y queda en (a) por el criterio 2.
- **D8. Cursos de entrenamiento** (`training.yaml: places[].courses`): **(a)**. Motivo: `trn` y `trn2` son módulos de
  código (TRN_REL y TRN2_REL), no mapas; un curso es la regla de una máquina del gimnasio, por stat (categoría), no
  por especie, objeto, técnica ni mapa.
- **D9. Localizadores** (direcciones, tamaños, opcodes, las listas por mensaje de `string_manifest.cpp`,
  `KnownScenes`): **(a)**. Motivo: dicen dónde está cada dato en el disco del usuario, no lo que vale; es el estatus de
  `ExecutableLayout`.
- **D10. Warps de guion** (`field/warps.yaml: scripted_warps`): **(b)**. **Borrado** en p4-scripted-warps
  (2026-10-21): los warps de los scripts los extrae `dwx` a `field/script_warps` con sus condiciones.
- **D11. Radio y velocidades de campo** (`field/rules.yaml`, partner inicial y jugador): **(b)**. El radio sale de
  `data/digimon` y las velocidades, del movimiento raíz del modelo. Motivo: son valores de una especie, y así un
  partner nuevo anda a su velocidad.
- **Fixtures de prueba** (`apps/sim/tests/stand-in/`, `tests/mods/example_mod/`): aceptados como inventados, con una
  prueba con disco que comprueba que ningún valor de un fixture coincide con la tabla real de su entidad.
- **Traducción al español** (`content/packs/es-translation/`): pasa a un repositorio de contenido aparte (paquete
  `p4-translation-split`).
- **Plan de la fase B y orden de la migración**: aceptados. La fase B empieza solo cuando `p4-ids-refs` llegue a
  `main`.

## Plan de la fase B

Objetivo: que ninguna tabla (b) esté en git, sin cambiar el juego.

### 1. Lectura en `dwx`, por tabla

Tres mecanismos, todos por versión. Las direcciones salen de `config/<versión>/symbols*.txt` del decomp; los valores,
del disco del usuario. Lo que está en el ejecutable principal se localiza con `ExecutableLayout`
(`formats/src/exe/executable_layout.cpp`); lo que está en un overlay (EVL_REL, BTL_REL, STD_REL, VS_REL), con el layout
de overlays de `formats/src/exe/overlay_tables.cpp` (`findOverlayTablesLayout`), que gana las entradas de EVL_REL y
BTL_REL. Una versión sin localizar falla con un error claro, como hoy.

1. **Tabla de datos** (`TableLocation`, ya existe): `REINCARNATE_BABY_TYPE` y (D3) `TYPE_FACTORS`,
   `FLEE_CHANCES`, `SLEEP_PATTERN` e `ITEM_SORT_ORDER`.
2. **Switch de constantes** (`ConstantSwitch`, ya existe para `handleFood` y `handleChips`): `handleBuffDisks`,
   `BTL_applyBuffMove` (y sus copias `STD_applyBuffMove` y `VS_applyBuffMove`), `getFreshEvolutionTarget`,
   `handleStatusItems`. Cada uno con su `ValueLocation` por versión. Las tres copias de la subida de apoyo se leen y
   `dwx` falla si no dan lo mismo.
3. **Constante de código** (nuevo, `CodeConstant`): la dirección de una instrucción y qué operando lleva el valor
   (`addiu`, `ori`, `slti`...). `dwx` decodifica la instrucción, comprueba que es la esperada y lee el inmediato; si
   no lo es, falla y no adivina. Lo usan:
   - los argumentos de `setReincarnateStats`, los destinos fijos de `handleEvoItems`, el nivel de
     `partnerWillRefuseItem`, `handleRestore`, el caso 0x16 de `handleChips`, `learnMove` y
     `battleStatsGainsAndDrops`;
   - las decisiones D1, D2, D5, D6 y D7.

   Vive en `formats/exe` junto a `constant_switch`, con su página en `docs/modulos/` y sus tests.

### 2. Formato en el paquete base

Documentos YAML en `data/`, cada uno con su esquema en `content/schemas/` y referencias por id (como hace hoy
`tablesToYaml`), nunca por número:

- `data/rebirth` (nuevo): `babies: [{ digimon, hp, mp, offense, defense, speed, brain }]`.
- `data/evolution`: añade `fresh_targets: { <bebé>: <destino> }` y `specials` (D1).
- `data/item_effects`: añade por objeto `evolves_to` (también para los fijos), `accepted_level`, `battle_status_cure`,
  `protects`, `injury_cure`, `sickness_cure`, `revive_hp_percent`, `battle_buff: { offense, defense, speed, omni }`,
  `script_section` y las marcas de D5, D6 y D7.
- `data/moves`: añade por técnica `buff: { offense, defense, speed }` y `learned_with`.
- `data/maps`: añade la marca `no_drops` por mapa.
- D3: `data/battle` (`affinity`, `join_chances`), `data/raise: sleep_patterns`, `data/items: sort_orders`.

Cada campo nuevo es opcional en el esquema (cambio aditivo): dw1fe y las herramientas que leen `data/` siguen
funcionando ([herramientas externas](../architecture/herramientas-externas.md)).

### 3. Cómo lo usan los rulesets

- `ruleset.yaml: tables` añade los papeles nuevos (`rebirth: data/rebirth` y `battle: data/battle`).
- Cada sección pierde sus claves (b); el sistema lee esos datos de la tabla de su papel, como ya hace `evolution` con
  `data/evolution`. Las reglas que quedan siguen con su cita al decomp.
- La duplicación se resuelve a la vez: los destinos fijos de objetos y la lista de bebés quedan en un solo sitio.
- Un ruleset de un mod trae sus propias tablas por papel (`extends` sustituye tablas enteras), y una especie u objeto
  nuevo declara sus campos en su fila, sin tocar el ruleset.

### 4. Tests con datos inventados

- `formats/exe`: un ejecutable de prueba (`makeExecutableFixture`) con valores inventados en las direcciones del layout
  y, para los switches y las constantes de código, instrucciones MIPS escritas a mano con inmediatos inventados. Casos
  de error: versión sin localizar, instrucción inesperada, switch que hace algo más que asignar.
- `apps/dwx`: el documento generado valida contra su esquema y los ids son los de las tablas.
- `game/*` y `apps/sim`: los paquetes de sustitución (`apps/sim/tests/stand-in/`) ganan las tablas nuevas, con
  valores inventados que no coinciden con los reales.
- Prueba con disco (nombre `the user's disc, extracted: ...`, etiqueta `slow`) por tabla: la tabla extraída tiene las
  entradas y los ids esperados y el ruleset carga sin error.
- Los asserts de tests que repiten valores (b) (ver «Otras constantes») pasan a valores inventados de los paquetes de
  sustitución o a pruebas con disco que comparan con la tabla extraída; los que solo miran tamaños se quedan.
- Prueba con disco de los fixtures: ningún valor de un fixture (`apps/sim/tests/stand-in/`, `tests/mods/example_mod/`)
  coincide con el de la tabla real de su entidad.

### 5. Criterio de aceptación: el juego no cambia

«Decidido por el coordinador (2026-10-21)»: mover las tablas por entidad a la extracción **no
puede cambiar el juego**. Una prueba con disco (`DWE_DISC` / `DWE_CONTENT`) ejecuta el ruleset `original` antes y
después de la migración sobre los datos reales y exige el mismo comportamiento.

Cómo se construye:

1. **Escenarios fijos y deterministas** en `apps/sim` (ya garantiza mismo resultado con la misma semilla y cualquier
   número de hilos), con un modo `--trace` que escribe una línea por evento (evolución, renacer, uso de objeto, golpe,
   técnica aprendida) con sus valores:
   - cada bebé criado con planes de cuidado fijos hasta su primera, segunda y tercera evolución (destinos de
     `fresh_targets`, de las especiales y de los objetos);
   - una muerte y un renacer por cada semilla de una lista fija (bebé elegido y stats heredados);
   - cada objeto con efecto usado sobre un partner de stats fijos, en campo y en combate (curas, revivir, discos,
     chips, evolución, rechazos);
   - combates fijos que usan cada técnica de apoyo y cada pareja de afinidades;
   - el escenario `loop` de la fase 3 completo.

   Cada entrada (b) tiene al menos un escenario que la ejerce:

   | Entrada (b) | Escenario |
   |---|---|
   | `life.rebirth.babies` (stats y especies), `evolution.rebirth.babies` | muerte y renacer con cada semilla de la lista |
   | `evolution.baby_targets` | cada bebé criado hasta su primera evolución |
   | `evolution.items.fixed`, `inventory.effects.evolution.fixed` | cada objeto de evolución fija sobre un partner de cada nivel |
   | `item_use.refusal.fixed_level_evolutions` | los mismos objetos con el partner un nivel por debajo y por encima |
   | `item_use.refusal.never_taken`, `refusal.discipline_checked_below` | cada objeto dado con disciplina 0 y 100 y una tirada fija |
   | `item_use.meals` | cada objeto dado con hambre y sin ella |
   | `inventory.effects.status` | cada objeto de estado en campo y en combate, partner enfermo y lesionado |
   | `inventory.effects.revive` | cada objeto de revivir durante la cuenta atrás de la derrota |
   | `inventory.effects.battle_disk.disks` | cada disco usado dos veces en un combate (subida y tope) |
   | `inventory.effects.chip.script` | el chip de guion con el guion en pausa |
   | `inventory.effects.chip.tamer_level.items` | cada chip de la lista con una tirada fija |
   | `inventory.effects.double_floppy` | el disquete doble en campo y en combate |
   | `inventory.sort.orders` | la bolsa llena ordenada con los tres órdenes |
   | `evolution.specials.wake_up` | cada regla, al despertar, con su forma de origen y una tirada fija |
   | `evolution.specials.praise_scold` | cada regla tras elogiar o regañar, y la del contador |
   | `evolution.stat_gains.scaled_targets` | una evolución a cada destino que escala y a uno que no |
   | `evolution.moves.specialty_blocks` | una evolución por especialidad: la técnica que aprende |
   | `care.sleep.patterns` | un día entero con cada horario de sueño |
   | `care.toilet.poop_eaters` | cada especie de la lista junto a una caca, y una que no está |
   | `battle.affinity.factors` | un combate por pareja de especialidades |
   | `battle.joining.chances` | el inicio de combate por pareja de tipos con una tirada fija |
   | `battle.moves.buffs` | cada técnica de apoyo en combate normal, de torneo y de versus (BTL, STD y VS) |
   | `battle.moves.learned_together` | aprender la primera técnica de cada pareja |
   | `battle.rewards.no_drop_maps` | ganar un combate en cada mapa de la lista y en uno que no está |
   | `apps/sim` (carne del escenario de cuidado) | el escenario `care` |
   | `field/rules.yaml` y `rules.hpp` (radio y velocidades) | el partner sigue al jugador por un recorrido fijo |
   | `field/script_warps` (sustituye a `scripted_warps`) | la puerta de `room08` con el disparador 203 puesto y quitado (`game/field/tests/field_tests.cpp`) |

   **Cobertura:** el modo `--trace` anota cada entrada (b) que se lee. La prueba falla si una entrada migrada no se lee
   al menos una vez durante los escenarios, así que una tabla nueva sin escenario no pasa.
2. **Grabar antes, comparar después.** En el commit anterior a la migración, `scripts/dev.sh parity-record` corre los
   escenarios con la extracción actual y guarda la traza fuera de git (`_work/parity/<sha>.trace`; la traza contiene
   valores del juego y nunca se versiona). Tras cada commit de la migración, `parity-check` corre lo mismo y compara
   línea a línea; la primera diferencia hace fallar la prueba con el escenario, la semilla y el evento.
3. **Comparación en el mismo proceso** al migrar cada tabla, en dos commits. En el primero, el sistema aprende a leer
   la tabla extraída cuando su sección no trae la clave, y la prueba con disco escribe en el build una copia del
   ruleset `original` sin las claves (b) de esa tabla; corre los escenarios con los dos rulesets (el de git, con los
   valores en el YAML, y la copia, que lee la tabla extraída) y exige trazas idénticas. El segundo commit borra las
   claves del YAML. Así la paridad se comprueba sin depender de una traza grabada a mano.
4. La prueba forma parte de `check-full` (que ya recibe `DWE_DISC` y `DWE_CONTENT`), y la reextracción se hace en un
   directorio nuevo (`_work/content-f5-<tag>`), como piden las reglas del paquete.

### 6. Orden de la migración

Después de que `p4-ids-refs` llegue a `main` (toca las mismas claves). Cada familia de tablas va en sus dos commits
(punto 3 del criterio de aceptación), con su extractor, esquema, tests, cambio del ruleset y prueba de paridad en
verde. Los asserts de tests que repiten valores (b) de esa familia (ver «Otras constantes») cambian **en esos mismos
commits**, como tarde en el segundo, porque el segundo borra las claves del YAML que leen. Orden:

1. Grabar la traza de paridad y añadir el modo `--trace` y los escenarios (sin cambiar datos).
2. `CodeConstant` en `formats/exe`, con sus tests de instrucciones inventadas.
3. `data/rebirth` (tabla `REINCARNATE_BABY_TYPE` + constantes de `setReincarnateStats`): `life.yaml` y `evolution.yaml`.
4. `data/evolution: fresh_targets` (`getFreshEvolutionTarget`).
5. `data/item_effects`: estados, revivir, discos, chip de guion, destinos fijos y nivel de aceptación.
6. `data/moves`: subidas de apoyo y técnicas que se aprenden juntas.
7. `apps/sim`: la carne del escenario de cuidado sale de `data/item_effects`.
8. `data/maps: no_drops` (criterio 2).
9. Las decisiones D1 a D11, de la más barata a la más cara: D4 (derivar), D3 (tablas de datos), D6, D7, D5 (primero
   comprobar si `meals` se deriva), D2, D11 (también `game/field/include/dwe/game/field/rules.hpp:27-29`), D1; D10
   cuando llegue R4.1.
10. Repaso final de los asserts de tests: ninguno repite ya un valor (b); los que queden pasan a valores inventados o a
    pruebas con disco.
11. La prueba de los fixtures.
