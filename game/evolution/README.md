# evolution

La evolución del partner (`dwe::game::evolution`, capa `game`): cuándo cambia de forma, a cuál y qué se lleva al
cambiar (stats, técnicas, contador). Lo que hacía el original está en la
[ficha de evolución](../../docs/reference/evolution.md); cómo se leen las reglas, en
[rulesets](../../docs/architecture/rulesets.md).

No conoce al partner: recibe un `EvolutionInputs` con lo que lee (forma, stats, peso, errores de cuidado, disciplina,
felicidad, cansancio, batallas, técnicas aprendidas, edad, contador, barra de virus, mapa, si el tamer está libre,
copia de la forma virus) y devuelve un `EvolutionResult` que el partner aplica. Solo guarda su propio estado
(`EvolutionState`): los contadores de evolución y la copia de la forma virus son del partner. No incluye otros
módulos de `game`; la sesión hace de pegamento.

## Uso

```cpp
auto evolution = dwe::game::evolution::Evolution::load(vfs, ruleset);   // sección evolution + tablas del manifiesto
dwe::game::evolution::EvolutionState state;                             // va en la partida guardada
dwe::ruleset::Random random{seed};

// Cada avance del reloj, con las horas que ha cruzado (GameClock):
const auto check = evolution->checkNatural(inputs, state, advance.hours, random);
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

- **`checkNatural`** repasa una a una las horas cruzadas (para que adelantar el reloj dé lo mismo que jugarlo): la
  elección normal desde el umbral de cada nivel, la especial de las 360 horas de campeón y la barra de virus. El
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
  en la versión de sección `EvolutionStateVersion` (para las migraciones de `game/save`); la marca no se guarda, como
  en el original.
- **Simulación** (`simulation.hpp`): `simulateRaisings(evolution, generador, n, semilla)` corre `n` crianzas con un
  generador de entradas y devuelve un histograma de destinos, para el panel de ajuste.

## Reglas y estrategias

La sección `evolution` del ruleset
([`rules/original/evolution.yaml`](../../content/packs/game/rules/original/evolution.yaml),
[esquema](../../content/schemas/ruleset_evolution.schema.json)) tiene los números con su cita del decomp: horas de
cada nivel, destinos de los bebés, puntuación, fallback, especiales (como reglas de datos: destino, tirada, formas de
origen y requisitos), virus, objetos fijos, ganancias de stats, técnicas, bebés del renacer y las rarezas en
`quirks:`. `selection` y `scoring` nombran las estrategias (`original` de cada una, en `strategies.hpp`).

Las tablas salen del manifiesto del ruleset (`tables:`): `digimon`, `raise`, `moves`, `evolution` e `item_effects`,
leídas con los lectores de [`assets`](../../engine/assets/README.md#tablas-del-juego). `EvolutionTables` resuelve las
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

## Paridad con el decomp

| Regla | Decomp | Nuestro código / sección | Test | Estado |
|---|---|---|---|---|
| Umbrales 6/24/72/144 h y comprobación con el tamer libre | `partner_impl.c`: `tickConditions` | `Evolution::checkNatural`, `check_hours` | `each level checks from its own hours…`, `no natural evolution while the tamer is busy` | Igual |
| Horas cruzadas al adelantar el reloj | `clock.c`, `partner_impl.c`: `skipHours` | `checkNatural(hoursCrossed)` | `a check over several hours…` | Igual |
| Bebé → entrenamiento fijo | `evolution.c`: `getFreshEvolutionTarget` | `OriginalSelection` (`babyTarget`), `baby_targets` | `a baby becomes its in-training form…` | Igual |
| Bebé sin destino reinicia el contador | `evolution.c`: `getFreshEvolutionTarget` | `quirks.unlisted_baby_restarts_timer` | `quirk unlisted_baby_restarts_timer…` | Igual (el valor sin inicializar no se reproduce: no evoluciona); decidido por el responsable de diseño (2026-10-20): fiel al original |
| Puntos de cuidado, peso, stats y bonus | `evolution.c`: `calculateRequirementScore` | `OriginalScoring`, `requirements` | `a target scores a point each…`, `the battles bonus…`, `the happiness and learned moves bonuses` | Igual |
| Stat favorecido de novato y empate | `evolution.c`: `calculateRequirementScore` | `OriginalScoring`, `quirks.rookie_stat_tie_picks_last` | `a rookie target scores…`, `quirk rookie_stat_tie_picks_last…` | Igual |
| Preferencia por formas no criadas | `evolution.c`: `calculateRequirementScore` | `OriginalScoring` (`preferNew`) | `against a best candidate…` | Igual |
| Entrenamiento: más puntos, primero en empate | `evolution.c`: `getInTrainingEvolutionTarget` | `OriginalSelection` (`bestScored`) | `an in-training partner takes…` | Igual |
| Novato y campeón: mejor media, acumulador | `evolution.c`: `getRookieEvolutionTarget`, `getChampionEvolutionTarget` | `OriginalSelection` (`bestAverage`), `quirks.selection_average_carries_over` | `rookies and champions take…`, `quirk selection_average_carries_over…` | Igual (sin dividir entre 0) |
| Fallback de novato a las 96 h | `evolution.c`: `getRookieEvolutionTarget` | `fallback` | `a rookie with no candidate…`, slow `the known paths` | Igual |
| Especiales al despertar | `evolution.c`: `handleSpecialEvolutions` (modo 2) | `Evolution::wakeUp`, `specials.wake_up` | `waking up: …`, `a special needs its roll…`, `one roll for every rule…` | Igual |
| Especiales al elogiar o regañar; aviso de regaño | `evolution.c`: `handleSpecialEvolutions` (modo 3); `partner_impl.c`: `handlePraiseScold` | `Evolution::praiseOrScold`, `specials.praise_scold` | `praise or scold: …` | Igual |
| Especial de las 360 h que pisa la normal | `partner_impl.c`: `tickConditions` | `praise_scold_on_timer`, `quirks.timer_special_overrides_normal` | `a champion at the timer hours…`, `quirk timer_special_overrides_normal…` | Igual; decidido por el responsable de diseño (2026-10-20): fiel al original |
| Barra de virus y copia al entrar | `partner_impl.c`: `tickConditions` | `virus`, `Evolution::fillVirusBar` (el aviso del partner), `EvolutionResult::virusBackup` (la guarda el partner, con la forma) | `a full virus bar…`, `the partner's full virus bar…` | Igual |
| Vuelta de la forma virus y stats reconstruidos | `partner.c`: `callDigimonRoutine` (2); `evl.c`: `EVL_scaleBaseStats` | `Evolution::returnFromVirus`, `EvolutionInputs::virusBackup`, `EvolutionResult::restoredStats` | `back from the virus form…`, `the stats rebuilt from the virus backup are left to the session to clamp` | Igual: se restauran con `setStats` y la sesión los deja en el tope de cada stat, como `EVL_clampBaseStats`; sin el tope de 999 por ganancia (decidido por el responsable de diseño, 2026-10-20) |
| Evolución por script | `partner.c`: `callDigimonRoutine` (10) | `Evolution::evolveByScript` | `effects_tests.cpp` | Igual |
| Objetos de evolución fijos y de tabla | `item.c`: `handleEvoItems` | `Evolution::useItem`, `items`, `EvolutionTables::itemTarget` (`data/item_effects`, `item_targets`) | `a fixed evolution item…`, `an item of the item table…`, `the evolution items' targets come from the item effects…`, slow `every evolution item of the item effects…` | Igual |
| Marca de objeto contagiosa | `item.c`: `handleEvoItems`; `evl.c`: `EVL_applyEvolution` | `quirks.evolution_item_sticks` | `quirk evolution_item_sticks…` | Igual |
| Ganancias de stats y escala | `evl.c`: `EVL_applyEvolution`, `EVL_scaleBaseStats` | `stat_gains` (`EvolutionEffects`), `EvolutionResult::statChange` | `each stat that reaches the reference…`, `the listed forms…` | Igual |
| Topes de los stats | `evl.c`: `EVL_clampBaseStats` | `Partner::addStatGains` (`care.limits`), no la evolución | `game/partner`: `stat_gain_tests.cpp` | Desviación aprobada: además, cada ganancia queda en 999 como mucho (`stat_gain_max`, 2026-10-20) |
| Peso, cuidado, batallas, contador, vida, nivel del tamer | `evl.c`: `EVL_applyEvolution` | `stat_gains` | `effects_tests.cpp` | Igual |
| Técnica aprendida y ranuras | `evl.c`: `EVL_applyEvolution` | `moves` | `the form learns the weakest move…`, slow `every target of every chart…` | Igual |
| Renacer | `evolution.c`: `reincarnatePartner`; `dooa.c` | `Evolution::rebirth`, `rebirth` | `rebirth turns the partner…` | Igual (la herencia de stats y las vidas son del partner) |
| Rechazo de objetos | `partner_impl.c`: `partnerWillRefuseItem` | — | — | Fuera de alcance (sistema de objetos) |

Los tests lentos (`the user's disc, extracted: …`) cargan la evolución con las tablas extraídas (`DWE_CONTENT`),
pasan todas las comprobaciones por todas las formas con entradas al azar, comprueban caminos conocidos, que cada
destino de cada carta aprueba con entradas que cumplen su requisito, y simulan mil crianzas de cada forma de
entrenamiento.
