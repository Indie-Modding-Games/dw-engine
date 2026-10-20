# Ficha de referencia: inventario

Cómo funcionaban la bolsa, el dinero, el uso de objetos y las tiendas en el juego original, según el decomp. Las
constantes de esta ficha siembran la sección `inventory` del ruleset `original`
(`content/packs/game/rules/original/inventory.yaml`); los datos por objeto los extrae `dwx` del disco del usuario.

## Qué hace

El jugador lleva una bolsa de objetos y un monedero. Los objetos se recogen del suelo, se ganan en combate, se compran
en tiendas o los da la historia. Desde el menú se usan (casi todos se le dan al partner), se mueven, se ordenan o se
tiran. En combate se lanzan al partner. Las tiendas venden lo que la historia ha desbloqueado y compran lo que se
puede tirar.

## Reglas

### La bolsa

- **Tamaño:** caben hasta 30 huecos, pero solo se usan los primeros N. La historia fija N con una orden de guion
  (`set_inventory_size`): la partida nueva la deja en 10 y otros guiones la suben a 20 y a 30 **(comprobado)**.
- **Apilar** (`giveItem`): un objeto que entra va a la **primera** pila de su tipo. Si esa pila no está llena, suma y
  lo que pasa de 99 se pierde; si ya tiene 99, no entra **aunque haya huecos libres**. Si no hay pila, ocupa el primer
  hueco libre; con la bolsa llena no entra **(comprobado)**.
- **Quitar** (`removeItem`): resta de cada pila de ese tipo; una pila que llega a 0 deja el hueco vacío, sin mover
  las demás **(comprobado)**.
- **Contar** (`getItemCount`): la cantidad de la primera pila del tipo **(comprobado)**.
- **Mover:** intercambia dos huecos **(comprobado)**.
- **Tirar:** solo en el campo y solo los objetos con `droppable` 1; se tira la pila entera **(comprobado)**.
- **Objetos clave:** el original no tiene una marca de objeto importante. Los de `droppable` 0 (8 en la versión
  americana: 115 a 120, 123 y 124) ni se venden (`shopFillSellItemList`) ni se tiran (`selectOptionMenu`); los de
  `item_color` 255 (123 y 124) además se pintan en otro color (`renderInventoryTop`) **(comprobado)**.
- **Ordenar** (`sortItems`): hay tres órdenes. Cada uno es un orden de las seis categorías de `sorting_value`; dentro
  de cada categoría, por número de objeto. Los objetos quedan al principio y los huecos al final **(comprobado)**.
- **Partida nueva:** 10 huecos y 0 de dinero. El estado de arranque (`initializeInventory`) trae objetos de prueba y
  50 000; el guion de la partida nueva los quita y resta los 50 000 **(comprobado)**. Con qué objetos se empieza lo
  decide ese guion (fase 4).

### El dinero

- No pasa de 999 999 al sumar (premios de combate, guiones y ventas) y no baja de 0 al restar con el guion
  **(comprobado)**.

### Usar un objeto

- **Dónde:** según `item_color`: 0 solo en el campo, 1 solo en combate, 2 en los dos, 255 nunca **(comprobado)**.
- **En el campo** (estados 4 a 6 de `partner.c`): el jugador da el objeto al partner, que puede rechazarlo
  (`partnerWillRefuseItem`, ver la [ficha de cuidado](care.md)). Si lo rechaza no se gasta. Si lo acepta se aplica el
  efecto, se gasta uno y se cuenta como comida (`partnerHandleFoodFeed`) **(comprobado)**.
- **En combate** (`startThrowingItem`): se gasta uno al lanzarlo y el efecto llega al caer; no se puede rechazar
  **(comprobado)**.
- **Efectos:** `ITEM_FUNCTIONS` da la función de cada objeto. No hay más tabla que esa, `HEALING_VALUES` (lo que curan
  las curas de HP y MP) y `EVOLUTION_ITEM_TARGET` (a qué evoluciona cada objeto de evolución); el resto de valores son
  constantes en el código **(comprobado)**:
  - **Curas de HP y MP:** suben los HP o MP actuales sin pasar del máximo. No hacen nada con el partner a 0 HP.
  - **Doble cura:** 1500 de HP y de MP, también solo con HP.
  - **Estados** (`handleStatusItems`, solo con HP): uno cura los estados de combate; otro los cura en combate y además
    hace la doble cura; otro protege al partner en ese combate; la venda cura la herida siempre y la enfermedad el
    20 % de las veces (tiradas de 3 y de 10); la medicina cura las dos siempre.
  - **Revivir** (`handleRestore`): uno devuelve la mitad de los HP máximos y otro todos y cura los estados en combate.
    En combate quitan la cuenta atrás de la derrota.
  - **Discos** (`handleBuffDisks`, solo con HP): suben ataque, defensa o velocidad 20 o 50 para el combate, con tope
    del valor al empezar el combate más un 30 %, como mucho 999. El disco de los tres atributos suma 20 a cada uno.
  - **Chips** (`handleChips`): suben para siempre ataque, defensa, inteligencia, velocidad (+50), HP o MP máximos
    (+500) o pares (+100, o +1000 de HP y MP) a cambio de 24 horas de vida; esos tres además tienen un 10 % de bajar un
    nivel de domador. Topes: 999 para los atributos y 9999 para HP y MP. El piloto automático llama a una sección del
    guion si está en pausa; el orinal portátil vacía las ganas del partner.
  - **Comida** (`handleFood`): energía, cansancio, felicidad, disciplina, peso, vida, HP y MP máximos y actuales,
    atributos, un bonus de entrenamiento y una probabilidad de enfermar. La comida favorita da energía × 1,4 y +2 de
    felicidad. Toda comida baja 1 el contador del baño.
  - **Evolución** (`handleEvoItems`): evoluciona al destino de la tabla si el partner está un nivel por debajo; tres
    objetos llevan a destinos fijos sin mirar el nivel.

### Tiendas

- **Existencias:** no hay tabla. El objeto `i` se vende mientras está activo el disparador `0x180 + i`
  (`shopFillBuyItemList`) **(comprobado)**.
- **Se puede comprar** si el precio entero cabe en el dinero y, o hay hueco libre y ninguna pila del objeto está llena,
  o no hay hueco y hay una pila del objeto sin llenar **(comprobado)**.
- **Comprar:** el precio es `value`; con un bebé de partner, × 90 / 100. Se compran como mucho 99 de una vez, no más
  de lo que falta para 99 en la bolsa y no más de lo que alcanza el dinero **al precio entero** (`createItemMenuAmountBox`)
  **(comprobado)**.
- **Vender:** solo lo que se puede tirar (`droppable` distinto de 0), a `value / 2`; el dinero no pasa de 999 999
  **(comprobado)**.
- **Otras tiendas** (fase 4): el intercambio por méritos (`merit_value`) y la tienda de reciclaje, que vende lo que se
  perdió al caer en combate, son menús que abre el guion.

### Derrota

- Al perder (`handleItemLoss`) se van `huecos ocupados × 30 / 100` huecos al azar, redondeando hacia abajo, cada uno
  con su pila entera. Lo perdido que se puede reciclar pasa a la tienda de reciclaje, hasta 99 de cada uno
  **(comprobado)**.

### Objetos del suelo y cofres

- Los objetos que suelta un enemigo caen al suelo (hasta 11 a la vez; los demás se pierden). Al recogerlos o abrir un
  cofre se intenta meter uno en la bolsa; si no entra, se queda donde estaba **(comprobado)**.

## Datos

| Tabla | Tamaño del registro | Entradas (US) | Qué guarda |
|---|---|---|---|
| `ITEM_PARA` | 32 bytes | 128 | Por objeto: nombre, precio (`value`), valor de mérito, categoría de orden (`sorting_value`), dónde se usa (`item_color`), si se puede tirar (`droppable`) y un campo sin identificar. `dwx` lo extrae a `data/items`. |
| `ITEM_FUNCTIONS` | 4 bytes | 128 | La función de cada objeto. |
| `HEALING_VALUES` | 2 bytes | 4 | HP o MP que cura cada cura. |
| `EVOLUTION_ITEM_TARGET` | 1 byte | 44 | Destino de cada objeto de evolución. |
| `SHOP_MAPS`, `SHOPKEEP_DIGIMON_TYPES` | | 8 y 11 | Mapas con tienda y tenderos (`data/shops`). |

`dwx` escribe en `data/item_effects` la función de cada objeto, lo que cura, a qué evoluciona y los valores de cada caso
de comida y de chip, que lee del código sin copiarlo ([tablas](../formats/tablas.md#efectos-de-objetos)).

## Rarezas

- **Lo que no cabe en la pila se pierde** (`giveItem`), y la tienda ya lo cobró. Quirk `stack_overflow_is_lost`.
- **Una pila llena bloquea el objeto aunque haya huecos libres** (`giveItem` solo mira la primera pila).
- **El tope de compra de un bebé usa el precio entero** (`createItemMenuAmountBox`, `shopFillBuyItemList`): con el
  descuento cabría alguno más. Quirk `baby_discount_limited_by_full_price`.
- **El disco de los tres atributos no mira el tope** (`handleBuffDisks`). Quirk `omni_disk_ignores_limit`.
- **Objetos sin efecto que se gastan:** las llaves, cañas y similares (función nula) y cinco chips cuyo caso no hace
  nada se pueden dar al partner y se gastan sin efecto. Quirk `useless_items_consumed`.
- **La comida no mira la vida a 0** (suma `remainingLifetime` directamente) y los chips sí (`modifyLifetime`).

## Dónde está en el decomp

| Tema | Ficheros y funciones |
|---|---|
| Bolsa | `src/main/item.c`: `giveItem`, `removeItem`, `getItemCount`, `setInventorySize`, `initializeInventory`, `pickupItem` |
| Menú | `src/main/inventory.c`: `selectOptionMenu`, `sortItems`; `src/main/utils.c`: `sortItemsById` |
| Efectos | `src/main/item.c`: `ITEM_FUNCTIONS`, `handleHPHealingItem`, `handleMPHealingItem`, `handleDoubleFloppy`, `handleStatusItems`, `handleMedicineHealing`, `handleRestore`, `handleChips`, `handlePortaPotty`, `handleFood`, `handleEvoItems`; `src/main/main.c`: `handleBuffDisks`, `startThrowingItem` |
| Uso en el campo | `src/main/partner.c` (estados 4 a 8 de dar un objeto); `src/main/partner_impl.c`: `partnerWillRefuseItem` |
| Tiendas | `src/main/script_common.c`: `shopFillBuyItemList`, `shopFillSellItemList`, `createItemMenuAmountBox`, `tickItemMenuAmountBox`, `isPartnerBaby` |
| Derrota | `src/main/script_common.c`: `handleItemLoss` |
| Dinero | `src/main/battle_ui.c`; `src/main/script_interp.c` (`ADD_MONEY`, `REDUCE_MONEY`, `set_inventory_size`) |
