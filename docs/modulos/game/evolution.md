# evolution

La evolución del partner (`dwe::game::evolution`, capa `game`): cuándo cambia de forma, a cuál y qué se lleva al
cambiar (stats, técnicas, contador). Lo que hacía el original está en la
[ficha de evolución](../../reference/evolution.md); cómo se leen las reglas, en
[rulesets](../../architecture/rulesets.md).

No conoce al partner: recibe un `EvolutionInputs` con lo que lee (forma, stats, peso, errores de cuidado, disciplina,
felicidad, cansancio, batallas, técnicas aprendidas, edad, contador, mapa, si el tamer está libre,
forma virus, copia de la forma virus) y devuelve un `EvolutionResult` que el partner aplica. Solo guarda su propio estado
(`EvolutionState`): los contadores de evolución y la copia de la forma virus son del partner. No incluye otros
módulos de `game`; la sesión hace de pegamento.

## Uso

```cpp
auto evolution = dwe::game::evolution::Evolution::load(vfs, ruleset);   // sección evolution + tablas del manifiesto
dwe::game::evolution::EvolutionState state;                             // va en la partida guardada
dwe::ruleset::Random random{seed};

// Cada avance del reloj, con los fotogramas que ha corrido (GameClock):
const dwe::game::evolution::CheckSpan span{.frames = advance.frames, .frameInHour = frameInHour,
                                           .framesPerHour = framesPerHour};
const auto check = evolution->checkNatural(inputs, state, span, random);
partner.evolutionTimer = check.evolutionTimer;
if (check.evolution) {
    applyToPartner(*check.evolution);                                   // la sesión
    dwe::game::evolution::recordEvolution(state, *check.evolution);
}

// Eventos:
evolution->wakeUp(inputs, state, random);                               // al despertar
evolution->praiseOrScold(inputs, state, {.scolded = true, .disciplineBefore = 0, .happinessBefore = -100}, random);
evolution->fillVirusBar(inputs, state);                                 // el partner avisó de la barra llena
evolution->useItem(inputs, state, "giga_hand");                         // id del objeto en data/items
evolution->returnFromVirus(inputs, state);                              // rutina de script 2 (lee inputs.virusBackup)
evolution->evolveByScript(inputs, state, target);                       // rutina de script 10
evolution->rebirth(inputs, random);                                     // al morir
```

- **`checkNatural`** mira una vez por fotograma del reloj de juego, como `tickConditions`: repasa cada valor del
  contador por el que pasó el tramo (`CheckSpan`; para que adelantar el reloj dé lo mismo que jugarlo), con la
  elección normal desde el umbral de cada nivel, y la especial de las 360 horas de campeón tira una vez por cada
  fotograma de esa hora hasta que sale una forma. `CheckSpan::hourly(n)` es una comprobación por hora (simulación,
  tests). La barra de virus no: su
  límite y su forma son de la sección `care` (`toilet.virus_bar_limit`, `toilet.virus_form`), el partner avisa con
  `VirusBarFull` y `fillVirusBar` lleva a la forma de `EvolutionInputs::virusForm`. El
  contador que devuelve ya incluye las horas posteriores a la evolución. El partner sigue siendo quien sube el
  contador.
- **`EvolutionResult`**: forma destino (la que se carga), motivo (`Normal`, `Special`, `Item`, `Virus`, `Script`,
  `Rebirth`), cambio de stats o stats restaurados (ver abajo), peso, errores de cuidado y batallas, contador, horas
  de vida extra, probabilidad de subir el nivel del tamer, técnica aprendida (índice en `data/moves`), ranuras
  equipadas (posiciones en la lista de la forma), si la barra de virus vuelve a 0, la copia que hay que guardar al
  entrar en la forma virus y si queda la marca de objeto.
- **Stats**: la evolución no topa nada. `statChange` es lo que cambia cada stat (negativo si una escala lo baja) y la
  sesión lo suma con `Partner::addStatGains`, que deja cada ganancia en 999 como mucho y cada stat en su tope
  (`care.limits`). Al salir de la forma virus, en cambio, `restoredStats` trae los stats reconstruidos desde la
  copia, sin topar; la sesión los pone con `Partner::setStats` y después los deja en el tope de cada stat
  (`care.limits`: 9999 HP y MP, 999 el resto), como `EVL_clampBaseStats`, pero sin el tope de 999 por ganancia
  (decidido por el responsable de diseño, 2026-10-20); `statChange` queda a 0.
- **Copia de la forma virus**: al entrar en la forma virus, `EvolutionResult::virusBackup` trae la forma y los stats
  anteriores. La sesión guarda las dos en `PartnerState::virusBackup` (el original guarda la forma en la variable de
  guion `PSTAT_SUKAMON_BACKUP_DIGIMON`; decidido por el responsable de diseño, 2026-10-20: va con el partner), y las
  devuelve en `EvolutionInputs::virusBackup`, que leen `returnFromVirus` y la reconstrucción de stats.
- **`EvolutionState`**: formas ya criadas y la marca de objeto usado. `toYaml`/`fromYaml` guardan las formas por id,
  en la versión de sección `EvolutionStateVersion` (para las migraciones de `packages/game/save`); la marca no se guarda, como
  en el original.
- **Requisitos** (`report.hpp`): `report(inputs, state, destino)` da el `RequirementReport` de un destino con
  requisito: lo que pide cada uno de los cuatro grupos (errores de cuidado, peso, stats, extra) y cómo está el partner,
  con los puntos que suma la misma `OriginalScoring` que decide la evolución (`score` sale de él; sin la preferencia
  por formas no criadas, que depende del mejor candidato). `outlook(inputs, state)` da el `EvolutionOutlook` del
  partner para el gráfico de evolución: contador, horas de la revisión de su nivel, un informe por destino de su carta,
  la forma que elegiría la próxima revisión con el partner tal como está (`next`, la misma `select` de
  `checkNatural`) y el fallback del novato. Lo usa el gráfico del menú de pausa
  ([game.md](../apps/game.md#requisitos-de-evolución)); los tests de `report_tests.cpp` comprueban que `next` es lo que
  `checkNatural` elige al llegar la hora.
- **Simulación** (`simulation.hpp`): `simulateRaisings(evolution, generador, n, semilla)` corre `n` crianzas con un
  generador de entradas y devuelve un histograma de destinos, para el panel de ajuste.

## Reglas y estrategias

La sección `evolution` del ruleset
([`rules/original/evolution.yaml`](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/packs/game/rules/original/evolution.yaml),
[esquema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_evolution.schema.json)) tiene los números con su cita del decomp: horas de
cada nivel, destinos de los bebés, puntuación, fallback, especiales (como reglas de datos: destino, tirada, formas de
origen y requisitos), virus, objetos fijos, ganancias de stats, técnicas, bebés del renacer y las rarezas en
`quirks:`. `selection` y `scoring` nombran las estrategias (`original` de cada una, en `strategies.hpp`).

Las tablas salen del manifiesto del ruleset (`tables:`): `digimon`, `raise`, `moves`, `evolution` e `item_effects`,
leídas con los lectores de [`assets`](../engine/assets.md#técnicas-y-evolución). `EvolutionTables` resuelve las
referencias a índices; un destino de carta que es un número crudo fuera de la tabla se ignora.

Los destinos de los objetos de evolución (`EVOLUTION_ITEM_TARGET`) salen de `evolves_to` de `data/item_effects`, que
`dwx` extrae del ejecutable, para los objetos con `handler: evolution`. La lista opcional `item_targets` de
`data/evolution` (no la escribe `dwx`) sustituye esos destinos objeto a objeto, y es la única fuente si el ruleset no
nombra `item_effects`. El ruleset `original` la nombra, así que el contenido extraído antes de que `dwx` la escribiera
hay que extraerlo de nuevo.

| Rareza (`quirks:`) | Con `true` (original) |
|---|---|
| `evolution_item_sticks` | Tras usar un objeto de evolución, ninguna evolución da stats ni vida extra |
| `selection_average_carries_over` | La media de un candidato que no mejora se arrastra al siguiente |
| `unlisted_baby_restarts_timer` | Un bebé sin destino vuelve a empezar su contador (decidido por el responsable de diseño (2026-10-20): fiel al original) |
| `rookie_stat_tie_picks_last` | En un empate de stats de novato cuenta el último |
| `timer_special_overrides_normal` | A las 360 horas de campeón, una especial fallida anula la elección normal (decidido por el responsable de diseño (2026-10-20): fiel al original) |

## Contenido oculto

Mejora opcional, apagada por defecto: el ruleset `oculto` («Original + contenido oculto», `extends: original`) da un
camino natural a tres formas que el disco trae completas pero que el original solo da con objetos: `species_k`
(campeón), `species_m` y `species_o` (ultras), y a `species_q` (ultra), que el disco solo trae como jefe. Se elige al empezar una partida nueva
([pantalla de reglas](../apps/game.md#contenido-oculto)) y queda en la partida guardada; el ruleset `original` sigue
igual. Los objetos que las dan (`noble_mane`, `giga_hand`, `metalbanana`) siguen funcionando con los dos.

Todo está en `rules/oculto/species_extra.yaml` (la sección de [parches por
especie](../../architecture/rulesets.md#parches-por-especie)), con números de diseño nuestros, no datos del juego:

| Forma | Desde | Requisito (3 de 4 grupos, como el original) | Puerta |
|---|---|---|---|
| `species_k` | `species_ak`, `species_l` | ataque 150, defensa 100, velocidad 120; 0–2 errores; peso 25 ± 5; 30 técnicas o venir de `species_l` | `species_aj` vive en la ciudad (disparador 248) |
| `species_m` | `species_bb`, `species_r`, `species_ay`, `species_n` | HP 4000, MP 4000, ataque 500, defensa 400, velocidad 300, cerebro 300; 0–5 errores; peso 60 ± 5; 60 batallas | Copa Metálica (16) ganada alguna vez |
| `species_o` | `species_p`, `species_ar` | ataque 500, velocidad 500, cerebro 400; 0 errores; peso 20 ± 5; 100 batallas o venir de `species_p` | `species_s` vencido y en la ciudad (disparador 242) |
| `species_q` | `species_r`, `species_bk` | MP 5000, velocidad 400, cerebro 500; 0–5 errores; peso 30 ± 5; disciplina 80 | `species_q` rescatado en el laboratorio de la Mansión de Grey Lord (disparador 129, fin del guion 64) y de noche (20 a 6) |

- **Puertas.** `SpeciesGate` (disparadores de la historia, hora del día, copa ganada) se mira antes de puntuar: si
  no se cumple, el destino no puntúa (`OriginalScoring` da 0 puntos y `RequirementReport::gateMet` es `false`), así
  que la revisión no lo elige y el panel de requisitos del árbol lo dice en la primera fila («Condición especial»).
  `EvolutionInputs` trae `trigger` (lector de disparadores), `hourOfDay` y `cupWon`; sin lector, una puerta de
  disparadores u hora está cerrada.
- **Ranuras.** Cada destino nuevo toma la primera ranura libre de `to` **después** del último destino del origen, así
  que en un empate gana el destino del original (el plan decía la ranura 0 para `species_ak`; se cambió para no tocar la
  prioridad de los destinos de siempre).
- **Copas.** La puerta `cup` es el número de la copa en `data/tournaments`. `EvolutionAdapter::cupReader` la lee de
  `TournamentAdapter::cupWon` (el disparador 15 + copa que pone la copa ganada y que se guarda con la partida); sin
  torneos (`setCupSource` sin llamar, como en los tests del adaptador solo) `cupWon` va vacío y la puerta está
  abierta. Los premios van en `extra_prizes` del mismo fichero, que los torneos suman a los de `tournament.yaml`:
  Noble Mane (126) en la Copa Animal (20, grado Z), Giga Hand (125) en la Copa Metálica (16, grado M) y Metalbanana
  (127) en la Copa Basura (17, grado T).
- **`species_q` de compañero.** No tiene filas en el disco, así que el parche las diseña: ganancias
  6000/7000/600/500/700/700, crianza copiada de `species_r` con la carne de comida favorita y el remate de `species_r`
  (`death_claw`) en su primera ranura vacía (la 5). `effect_type: devi` da a sus técnicas los puntos de lanzamiento
  de `species_r` (`BattleAdapter` lo pone en `effects.original_types`). Su modelo es de enemigo y no tiene los clips de
  cuidado ni de gimnasio: `clips` dice cuál juega en lugar de cada uno (anda mal → anda, contento → victoria,
  comer → técnica 0, dormir y estados → quieto de mal humor, cansado → cansancio del combate, gimnasios → guardia y
  bloqueo; el remate, clip 51, juega la técnica 0). `FieldScene::loadActor` los añade al cargar al compañero con
  `assets::addClipStandIns`, que copia el clip que existe con el id que falta y nunca pisa uno propio; así la carga
  del compañero sigue negándose a un modelo al que le falte un clip (`MissingClips::Refuse`).
- **Criadas.** Las especies desde 0x3f no caben en los disparadores de criado del original (200 + especie hasta
  0x3e): `StoryAdapter` las guarda en su propia lista, que va en la sección de evolución de la partida y que una
  partida nueva vacía. El árbol las da por descubiertas con ella; la medalla de las 61 sigue contando solo las del
  original.
- **Horas.** `Evolution::checkHoursFor(especie)` da las horas de revisión de una forma (las de su nivel, o las del
  paso Mega para una ultra cuando el ruleset lo trae).

Tests: `packages/game/evolution/tests/hidden_tests.cpp` (puerta de disparadores y hora, puerta de copa, paso Mega),
`clip stand-ins fill only the clips a model lacks, with copies of ones it has` (`packages/engine/assets/tests/model_data_tests.cpp`),
`the user's disc, extracted: the hidden content's paths and gates` y `the user's disc, extracted: the Mega step's
paths` (`extracted_tests.cpp`, con `DWE_CONTENT`: los
destinos tras los del original y cada camino elegido por la revisión),
`the game pack's hidden content rulesets follow their schemas and read` (`packages/engine/ruleset/tests/species_extra_tests.cpp`)
y `extra_prizes: the hidden content's species patches give each new form's item`
(`packages/game/tournament/tests/cup_extras_tests.cpp`), `a species past the raised triggers is kept apart, and a new game clears it`
(`packages/session/tests/evolution_adapter_tests.cpp`). Rutas: `route-hidden-content.yaml`,
`route-hidden-content-off.yaml` y `route-mega-step.yaml` ([prueba vertical](../../guides/prueba-vertical.md)).

### Paso Ultra → Mega

Otra opción de partida nueva, apagada por defecto y solo con el contenido oculto: el ruleset `oculto_mega`
(`extends: oculto`) añade la sección `mega_step` (`rules/oculto_mega/mega_step.yaml`). Un ultra vuelve a revisar a
las 120 horas con la misma puntuación; llegar a Mega reinicia el contador, da 72 horas de vida y sube el nivel de
criador (`tamer_level_chance: 100`). Un Mega es el último paso. En las demás reglas (entrenamiento, torneos,
prosperidad) un Mega sigue contando como nivel 5.

| Forma | Desde | Requisito | Puerta |
|---|---|---|---|
| `species_o` (Mega) | `species_s` (en vez de `species_p` y `species_ar`) | el de antes: ataque 500, velocidad 500, cerebro 400; 0 errores; peso 20 ± 5; 100 batallas o venir de `species_s` | la de antes (disparador 242) |
| `species_t` (Mega) | `species_u`, `species_m` | HP 8000, MP 6000, ataque 700, defensa 600, velocidad 400, cerebro 400; 150 batallas | Mt. Infinity terminado (disparador 50, que el guion 184 pone tras el combate final; suposición) |

- **`species_t` de compañero.** Filas diseñadas: ganancias 9999/9999/999/900/700/800 y crianza de `species_u` con
  siete comidas en vez de seis y peso 70. Lucha como tipo 3 (virus; el jefe es 0, sin tipo: `partner_type`) y su
  Infinity Cannon pega 240 con 1 iframe en vez de 777 con 38 (`partner_moves`). `BattleAdapter` añade esa copia al
  catálogo de técnicas (`MoveCatalog::addVariant`: mismo id, después de la tabla) y `Combatant::moveVariants` la usa
  solo para el compañero; el jefe y cualquier otro luchador siguen con la de la tabla. Tiene los mismos sustitutos de
  clips que `species_q` y en el campo se dibuja a 0,6 (`field_scale`: radio 400 frente a 220 de `species_u`).
- **Sin la opción** nada cambia: un ultra no vuelve a revisar y `species_t` no se alcanza.

Tests: `a partner's move variant replaces the table's move for it alone` (`packages/game/battle/tests/battle_tests.cpp`),
`a ruleset's species patches give the partner its own type and move values`
(`packages/session/tests/battle_adapter_tests.cpp`) y los de «Contenido oculto».

Las etapas canónicas (Supremo/Mega para `species_v`, `species_bd`, `species_o` y `species_t`) son solo de la
UI: las reglas siguen viendo nivel 5 ([game.md](../apps/game.md#etapas-canónicas)).

## Paridad con el decomp

| Regla | Decomp | Nuestro código / sección | Test | Estado |
|---|---|---|---|---|
| Umbrales 6/24/72/144 h y comprobación con el tamer libre | `partner_impl.c`: `tickConditions` | `Evolution::checkNatural`, `check_hours` | `each level checks from its own hours…`, `no natural evolution while the tamer is busy` | Igual |
| Una comprobación por fotograma del reloj, también al adelantarlo | `clock.c`: `tickGameClock`; `partner_impl.c`: `tickConditions` | `checkNatural(CheckSpan)` | `a check over several hours…`, `a span checks each counter value…`, `at the timer hours the special rolls on every checked frame…` | Igual |
| Bebé → entrenamiento fijo | `evolution.c`: `getFreshEvolutionTarget` | `OriginalSelection` (`babyTarget`), `baby_targets` | `a baby becomes its in-training form…` | Igual |
| Bebé sin destino reinicia el contador | `evolution.c`: `getFreshEvolutionTarget` | `quirks.unlisted_baby_restarts_timer` | `quirk unlisted_baby_restarts_timer…` | Igual (el valor sin inicializar no se reproduce: no evoluciona); decidido por el responsable de diseño (2026-10-20): fiel al original |
| Puntos de cuidado, peso, stats y bonus | `evolution.c`: `calculateRequirementScore` | `OriginalScoring`, `requirements` | `a target scores a point each…`, `the battles bonus…`, `the happiness and learned moves bonuses` | Igual |
| Solo cuentan las técnicas aprendidas con índice menor que 64 | `evolution.c`: `getNumMasteredMoves` (dos palabras de 32 bits) | `requirements.mastered_moves_below`, `countMasteredMoves` | `the moves requirement counts only the learned moves below…` | Igual |
| Stat favorecido de novato y empate | `evolution.c`: `calculateRequirementScore` | `OriginalScoring`, `quirks.rookie_stat_tie_picks_last` | `a rookie target scores…`, `quirk rookie_stat_tie_picks_last…` | Igual |
| Preferencia por formas no criadas | `evolution.c`: `calculateRequirementScore` | `OriginalScoring` (`preferNew`) | `against a best candidate…` | Igual |
| Entrenamiento: más puntos, primero en empate | `evolution.c`: `getInTrainingEvolutionTarget` | `OriginalSelection` (`bestScored`) | `an in-training partner takes…` | Igual |
| Novato y campeón: mejor media, acumulador | `evolution.c`: `getRookieEvolutionTarget`, `getChampionEvolutionTarget` | `OriginalSelection` (`bestAverage`), `quirks.selection_average_carries_over` | `rookies and champions take…`, `quirk selection_average_carries_over…` | Igual (sin dividir entre 0) |
| Fallback de novato a las 96 h | `evolution.c`: `getRookieEvolutionTarget` | `fallback` | `a rookie with no candidate…`, slow `the known paths` | Igual |
| Especiales al despertar | `evolution.c`: `handleSpecialEvolutions` (modo 2) | `Evolution::wakeUp`, `specials.wake_up` | `waking up: …`, `a special needs its roll…`, `one roll for every rule…` | Igual |
| Especiales al elogiar o regañar; aviso de regaño | `evolution.c`: `handleSpecialEvolutions` (modo 3); `partner_impl.c`: `handlePraiseScold` | `Evolution::praiseOrScold`, `specials.praise_scold` | `praise or scold: …` | Igual |
| Especial de las 360 h que pisa la normal | `partner_impl.c`: `tickConditions` | `praise_scold_on_timer`, `quirks.timer_special_overrides_normal` | `a champion at the timer hours…`, `quirk timer_special_overrides_normal…` | Igual; decidido por el responsable de diseño (2026-10-20): fiel al original |
| Barra de virus y copia al entrar | `partner_impl.c`: `tickConditions` | `care.toilet.virus_bar_limit` y `virus_form` (dueña: `care`), `Evolution::fillVirusBar` con `EvolutionInputs::virusForm` (el aviso del partner), `EvolutionResult::virusBackup` (la guarda el partner, con la forma) | `the partner's full virus bar…` | Igual |
| Vuelta de la forma virus y stats reconstruidos | `partner.c`: `callDigimonRoutine` (2); `evl.c`: `EVL_scaleBaseStats` | `Evolution::returnFromVirus`, `EvolutionInputs::virusBackup`, `EvolutionResult::restoredStats` | `back from the virus form…`, `the stats rebuilt from the virus backup are left to the session to clamp` | Igual: se restauran con `setStats` y la sesión los deja en el tope de cada stat, como `EVL_clampBaseStats`; sin el tope de 999 por ganancia (decidido por el responsable de diseño, 2026-10-20) |
| Evolución por script | `partner.c`: `callDigimonRoutine` (10) | `Evolution::evolveByScript` | `effects_tests.cpp` | Igual |
| Objetos de evolución fijos y de tabla | `item.c`: `handleEvoItems` | `Evolution::useItem`, `items`, `EvolutionTables::itemTarget` (`data/item_effects`, `item_targets`) | `a fixed evolution item…`, `an item of the item table…`, `the evolution items' targets come from the item effects…`, slow `every item of EVOLUTION_ITEM_TARGET evolves a form…` | Igual |
| Marca de objeto contagiosa | `item.c`: `handleEvoItems`; `evl.c`: `EVL_applyEvolution` | `quirks.evolution_item_sticks` | `quirk evolution_item_sticks…` | Igual |
| Ganancias de stats y escala | `evl.c`: `EVL_applyEvolution`, `EVL_scaleBaseStats` | `stat_gains` (`EvolutionEffects`), `EvolutionResult::statChange` | `each stat that reaches the reference…`, `the listed forms…` | Igual |
| Topes de los stats | `evl.c`: `EVL_clampBaseStats` | `Partner::addStatGains` (`care.limits`), no la evolución | `packages/game/partner`: `stat_gain_tests.cpp` | Desviación aprobada: además, cada ganancia queda en 999 como mucho (`stat_gain_max`, 2026-10-20) |
| Peso, cuidado, batallas, contador, vida, nivel del tamer | `evl.c`: `EVL_applyEvolution` | `stat_gains` | `effects_tests.cpp` | Igual |
| Técnica aprendida y ranuras | `evl.c`: `EVL_applyEvolution`; `utils2.c`: `learnMove` | `moves`; la sesión las da a `BattleAdapter::evolveMoves` | `the form learns the weakest move…`, slow `every target of every chart…`, `an evolution teaches the new form's move…` | Igual |
| Renacer | `evolution.c`: `reincarnatePartner`; `dooa.c` | `Evolution::rebirth`, `rebirth` | `rebirth turns the partner…` | Igual (la herencia de stats y las vidas son del partner) |
| Rechazo de objetos | `partner_impl.c`: `partnerWillRefuseItem` | — | — | Fuera de alcance (sistema de objetos) |

Los tests lentos (`the user's disc, extracted: …`) cargan la evolución con las tablas extraídas (`DWE_CONTENT`),
pasan todas las comprobaciones por todas las formas con entradas al azar, comprueban caminos conocidos, que cada
destino de cada carta aprueba con entradas que cumplen su requisito, y simulan mil crianzas de cada forma de
entrenamiento.
