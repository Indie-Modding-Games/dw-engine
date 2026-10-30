# inventory

El inventario (`dwe::game::inventory`, capa `game`): la bolsa del jugador, su dinero, el uso de objetos y el comercio de
las tiendas. Lo que hacía el original está en la [ficha de inventario](../../reference/inventory.md).

No dibuja menús ni conoce al partner: depende de `core`, `assets` y `ruleset`. Los números salen de la sección
`inventory` del ruleset (`rules/<id>/inventory.yaml`) y los datos por objeto de las tablas del paquete base
`data/items`, `data/item_effects` y `data/shops`, que extrae `dwx`.

## Uso

```cpp
auto inventory = dwe::game::inventory::Inventory::fromRuleset(ruleset);   // partida nueva: 10 huecos, 0 de dinero
auto items = dwe::assets::loadItemTable(vfs);
auto table = dwe::assets::loadItemEffectTable(vfs);
dwe::game::inventory::ItemEffects effects{*table, inventory->rules().effects, inventory->rules().quirks};

auto item = *items->handleOf("bandage");   // el handle del objeto en esta sesión (docs/modulos/engine/assets.md: ids en la sesión)
inventory->add(item, 1);                    // recoger, premio, cofre: AddResult dice qué entró, qué se perdió y qué no
if (!inventory->hasRoomFor(item)) { ... }   // el objeto se queda en el suelo o en el cofre
auto used = useItem(*inventory, item, UseContext::Field, *items, effects, partnerTarget);
```

- **Estado** (`InventoryState`): los 30 huecos (`std::optional<Stack>` con el handle del objeto y la cantidad),
  cuántos se usan (`slotCount`) y el dinero. `toYaml` y `fromYaml` lo pasan a YAML para la sección de la partida
  guardada, con cada objeto por su id y la versión en `SectionVersion` (2); la sesión los envuelve en un
  `SectionSerializer` de `game/save`. La versión 1 guardaba el número del objeto: `itemNumbersToIds` la migra al id
  del objeto del original con ese número. Un hueco cuyo objeto no está en ninguna tabla montada (el de un mod que ya
  no está, o un número de la versión 1 que no es ningún objeto del original) se queda vacío: `fromYaml` devuelve esos
  montones (`DroppedStack`, id y cantidad) y la sesión los nombra en el registro; el resto de la partida se carga. Los
  ids se escriben entre comillas.
- **Partida nueva**: `bag.new_game_slots`, `bag.new_game_money` y `bag.new_game_items` (`StartingItem`: id del
  objeto y cantidad, de 1 a `max_stack`). El inventario no los pone: la sesión crea la bolsa y añade la lista. En el
  original la lista está vacía: el guion `dg 193` quita todo lo que deja `initializeDefaultSavegame`.
- **Bolsa** (`Inventory`): `add`, `hasRoomFor`, `countOf`, `remove`, `move`, `discard`, `sort`, `setSlotCount`,
  `loseOnDefeat` (devuelve las pilas elegidas, las que cuenta la tienda de reciclaje) y `addMoney`, `spendMoney`.
- **Efectos** (`ItemEffect`): una descripción plana de lo que hace un objeto (curar, revivir, curar estados, proteger,
  medicina, subir atributos en combate o para siempre, cambios de cuidado, nivel de domador, sección de guion, baño,
  evolución). `ItemEffects::resolve` la construye con la tabla y el ruleset.
- **Destino** (`ItemEffectTarget`): lo implementa quien tiene al partner. `accepts` decide si lo acepta (solo en el
  campo: `partnerWillRefuseItem`) y `apply` aplica el efecto con sus topes.
- **Uso** (`useItem`): mira si el objeto está, si su `item_color` deja usarlo ahí y su efecto; en el campo pregunta al
  partner; gasta uno y aplica. Devuelve `UseResult`: el `UseOutcome` (`Used`, `Refused`, `NotUsableHere`, `NotInBag`,
  `Useless`) y `keyItem`.
- **Objetos clave** (`isKeyItem`): el original no tiene una marca propia; son los de `droppable` igual a
  `use.key_item_droppable` (0), que ni se venden ni se tiran. `useItem` y `buy` (`Purchase::keyItem`) lo dicen para que
  la sesión decida el autoguardado; el inventario nunca guarda.
- **Tiendas** (`shop.hpp`): `shopStock` con los disparadores de la historia (`StockTriggers`), `buyPrice`, `sellPrice`,
  `maxPurchase`, `canAfford`, `buy`, `saleOffers` y `sell`. `Buyer` dice si el partner es un bebé. Los disparadores van
  por el número del objeto en el original, así que un objeto que añade un mod aún no se vende en ninguna tienda.
- **Ids**: la sección del ruleset nombra cada objeto por su id; `checkItemIds` comprueba que están en `data/items`.
- **Apilar** (`StackingRule`): la estrategia que nombra `bag.stacking`; `stackingRules()` registra `original`.

## Rarezas

Cada una es un flag de `quirks` en la sección; `original` las tiene todas a `true` (decidido por el responsable de
diseño, 2026-10-20). Las variantes corregidas irán más adelante en un ruleset de diseño, no en `original`.

| Quirk | Qué hace |
|---|---|
| `stack_overflow_is_lost` | Lo que pasa de 99 al apilar se pierde; con `false`, no entra y se queda donde estaba. |
| `baby_discount_limited_by_full_price` | El máximo que se puede comprar con un bebé se calcula con el precio entero. |
| `omni_disk_ignores_limit` | El disco de los tres atributos no mira el tope de combate. |
| `useless_items_consumed` | Un objeto sin efecto se gasta al usarlo; con `false`, se queda en la bolsa. |

## Decisiones de diseño (2026-10-20)

Del responsable de diseño:

1. **De dónde salen los efectos**: híbrido. Los números de cada objeto vienen de `data/item_effects` (los extrae
   `dwx`) y las constantes de la lógica de cada función, del ruleset (`effects`), con su cita.
2. **Objetos de partida nueva**: una lista del ruleset (`bag.new_game_items`) citada al guion `dg 193`, que pone la
   sesión; en `original` está vacía. Cuando la máquina de guiones de la fase 4 corra `dg 193`, queda como respaldo.
3. **Rarezas**: las 4 a `true` en `original`; las variantes corregidas, en un ruleset de diseño más adelante.
4. **Objeto clave**: `droppable` igual a 0 (8 objetos en el original), sin marca propia.
5. **Pérdida al perder un combate**: con `ruleset::Random` (los números de la partida), no con la tirada del original;
   el efecto es el mismo (Igual).

## Paridad con el original

Estado: **Igual** (mismo comportamiento), **Desviación aprobada** (cambio decidido por diseño) o **Hueco** (falta).

| Regla | Decomp | Nuestro código | Test | Estado |
|---|---|---|---|---|
| Apilar en la primera pila, tope 99, el resto se pierde | `item.c: giveItem` | `stacking.cpp: OriginalStackingRule`; `bag.max_stack` | `bag_tests`: "a stack holds 99…" | Igual |
| Pila llena bloquea aunque haya huecos | `item.c: giveItem` | `OriginalStackingRule::addToStack` | `bag_tests`: "a stack holds 99…" | Igual |
| Bolsa llena: el objeto no entra | `item.c: giveItem`, `pickupItem` | `Inventory::add`, `hasRoomFor` | `bag_tests`: "a full bag refuses…" | Igual |
| Quitar deja hueco | `item.c: removeItem` | `Inventory::remove` | `bag_tests`: "a stack that runs out…" | Igual |
| Contar la primera pila | `item.c: getItemCount` | `Inventory::countOf` | `bag_tests`: "items join…" | Igual |
| Tamaños 10, 20 y 30 por guion | `script_interp.c: set_inventory_size`; `item.c: setInventorySize` | `Inventory::setSlotCount`; `bag.slot_counts` | `bag_tests`: "the story grows…" | Igual |
| Partida nueva: 10 huecos y 0 de dinero | guion `dg 193`; `main_menu.c: initializeDefaultSavegame` | constructor de `Inventory`; `bag.new_game_*` | `bag_tests`: "a new game's bag…" | Igual |
| Partida nueva: sin objetos | guion `dg 193` (`script_interp.c: remove_item` de los 29 huecos de `main_menu.c: initializeDefaultSavegame`, sin `give_item`) | `bag.new_game_items: []`; los pone la sesión (`InventoryAdapter::startNewGame`) | `rules_tests`: "the original starts with an empty bag…"; `apps/session` `inventory_adapter_tests` | Igual (cuando la máquina de guiones de la fase 4 corra `dg 193`, la lista queda como respaldo; decidido el 2026-10-20) |
| Mover intercambia huecos | `inventory.c: selectOptionMenu` | `Inventory::move` | `bag_tests`: "moving swaps…" | Igual |
| Tirar: campo, `droppable` 1, pila entera | `inventory.c: selectOptionMenu` | `Inventory::discard`; `use.discard_droppable` | `bag_tests`: "moving swaps…" | Igual |
| Objeto clave: `droppable` 0, ni se vende ni se tira (sin marca propia) | `script_common.c: shopFillSellItemList`; `inventory.c: selectOptionMenu` | `isKeyItem`; `use.key_item_droppable`; `UseResult::keyItem`, `Purchase::keyItem` | `use_tests`: "using an item says…"; `shop_tests`: "buying a key item…" | Igual (8 objetos; decidido el 2026-10-20; el aviso es para el autoguardado, que no existe en el original) |
| Ordenar por categorías y número | `inventory.c: sortItems`, `ITEM_SORT_ORDER`; `utils.c: sortItemsById` | `Inventory::sort`; `sort.orders` | `bag_tests`: "sorting groups…" | Igual |
| Dinero: tope 999 999, suelo 0 | `battle_ui.c`; `script_interp.c` | `addMoney`, `spendMoney`; `money.max` | `bag_tests`: "money stops…" | Igual |
| Derrota: 30 % de los huecos ocupados, redondeado hacia abajo y contado una vez; cada tirada elige un hueco ocupado y se lleva todas las pilas de su objeto; si la bolsa se vacía, no se pierde nada más | `script_common.c: handleItemLoss`; `item.c: removeItem` | `Inventory::loseOnDefeat`; `defeat.lost_percent` | `bag_tests`: "a defeat loses…", "a defeat's pick takes every stack…", "a defeat that empties the bag…", "a defeat loses what the original's rule loses…" | Igual en efecto (decidido el 2026-10-20): tirada con `ruleset::Random`, los números de la partida (`InventoryAdapter::loseOnDefeat`), no la del original. Con un `bag.max_stack` de mod mayor que 99 se quitan todas las pilas del objeto enteras, mientras que el `removeItem(…, 99)` del original dejaría lo que pase de 99 |
| Tienda de reciclaje con lo perdido | `script_common.c: handleItemLoss`, menú 2 | `loseOnDefeat` devuelve solo las pilas elegidas, como las cuenta el original | — | Hueco (fase 4) |
| Uso según `item_color` | `inventory.c: selectOptionMenu` | `use.cpp: usableIn`; `use.*_colors` | `use_tests`: "colour 0…" | Igual |
| Campo: rechazo sin gastar; gastar y aplicar | `partner.c` (estados 4 a 6); `partner_impl.c: partnerWillRefuseItem` | `useItem`, `ItemEffectTarget::accepts` | `use_tests`: "in the field…" | Igual (el rechazo lo decide el partner) |
| Combate: se gasta al lanzar, siempre llega | `main.c: startThrowingItem`, `tickThrownItem` | `useItem` con `UseContext::Battle` | `use_tests`: "in the field…" | Igual (el retraso del lanzamiento es de la batalla) |
| Objetos sin efecto se gastan | `partner.c`; `ITEM_FUNCTIONS` nulos | `useless_items_consumed` | `use_tests`: "an item that does nothing…" | Igual |
| Curas de HP y MP | `item.c: handleHPHealingItem`, `handleMPHealingItem`, `HEALING_VALUES` | `ItemEffects::describe`; `data/item_effects` | `effects_tests`: "healing items…" | Igual |
| Doble cura 1500 | `item.c: handleDoubleFloppy` | `effects.double_floppy` | `effects_tests`: "healing items…" | Igual |
| Estados, protección, medicina | `item.c: handleStatusItems`, `handleMedicineHealing` | `describeStatus`; `effects.status` | `effects_tests`: "status items…" | Igual |
| Revivir | `item.c: handleRestore` | `describeRevive`; `effects.revive` | `effects_tests`: "status items…" | Igual |
| Discos con tope +30 %, 999 | `main.c: handleBuffDisks` | `describeDisk`; `effects.battle_disk` | `effects_tests`: "status items…", "the omni disk…" | Igual |
| Chips: atributos, vida, nivel de domador | `item.c: handleChips` | `describeChip`; `data/item_effects`, `effects.chip` | `effects_tests`: "chips give…" | Igual |
| Piloto automático y orinal | `item.c: handleChips`, `handlePortaPotty` | `effects.chip.script`, `effects.chip.toilet` | `effects_tests`: "chips give…" | Igual |
| Comida, favorita, baño −1 | `item.c: handleFood` | `describeFood`; `data/item_effects`, `effects.food` | `effects_tests`: "food gives…" | Igual |
| Evolución por tabla y fija | `item.c: handleEvoItems`, `EVOLUTION_ITEM_TARGET` | `describeEvolution`; `effects.evolution` | `effects_tests`: "evolution items…" | Igual |
| Partículas de curación: con anillo los objetos grandes; la medicina solo si cura la enfermedad | `item.c` (`addHealingParticleEffect`), `handleMedicineHealing` | `ItemEffect::healingParticles`; `effects.healing_particles`; `InventoryAdapter::takeHealingParticles` | `effects_tests`: "the larger items show…" | Igual |
| Existencias por disparador `0x180 + i` | `script_common.c: shopFillBuyItemList` | `shopStock`, `StockTriggers`; `shop.stock_trigger_base` | `shop_tests`: "the stock is…" | Igual |
| Cuándo se puede comprar | `script_common.c: shopFillBuyItemList` | `shop.cpp: isBuyable` | `shop_tests`: "the stock is…", "with the bag full…" | Igual |
| Precio, descuento de bebé 90 % | `script_common.c: tickItemMenuAmountBox`, `isPartnerBaby` | `buyPrice`, `buy`; `shop.baby_discount_percent` | `shop_tests`: "buying pays…" | Igual |
| Máximo de compra | `script_common.c: createItemMenuAmountBox` | `maxPurchase`; `shop.max_amount` | `shop_tests`: "buying pays…", "the stack's room…" | Igual |
| Venta a mitad de precio, solo lo que se tira | `script_common.c: shopFillSellItemList`, `createItemMenuAmountBox` | `saleOffers`, `sell`; `shop.sell_divisor` | `shop_tests`: "selling pays…" | Igual |
| Lista del intercambio por méritos: los objetos 0 a 127 con mérito, por número; se puede tomar si llegan los méritos y cabe en la bolsa (hueco libre o un montón por debajo del tope) | `shop/script_menu.c: shopFillMeritItemList` | `meritOffers` | `merit_shop_tests`: "the merit list is every item…", "with a full bag…" | Igual |
| Una carta vale su `meritValue`; los méritos no pasan de 9999 | `shop/script_menu.c: createMeritCardTradeDialogue`, `openMeritShop` (estado 8) | `cardMerit`, `addMerit` | `merit_shop_tests`: "a card is worth…" | Igual |
| Objetos del suelo (11 a la vez) | `item.c: DROPPED_ITEMS`, `pickupItem` | `hasRoomFor`, `add` | `bag_tests`: "a full bag refuses…" | Hueco (el suelo es del campo y la batalla) |

## Tests

- Unitarios en `tests/`, con el ruleset `original` del paquete `game` y objetos inventados (`tests/fixture.hpp`):
  ninguna tabla del original se copia.
- `rules_tests` cambia un valor del ruleset (`bag.max_stack`) y cambia la estrategia de apilar por otra registrada en
  el test.
- `content_tests` ("the user's disc, extracted: …") carga con `DWE_CONTENT` todos los objetos, efectos y tiendas del
  disco del usuario y resuelve el efecto de cada objeto: en el US se resuelven los 128.
