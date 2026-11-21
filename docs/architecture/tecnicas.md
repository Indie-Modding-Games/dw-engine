# Technique catalogue

## 1. Goal

Collect the techniques that exist in Digimon World as a reference for redesigning DWPF's combat system.

The original arsenal has **56 techniques spread over 7 specialties**. Each technique has parameters such as power, MP cost, range and additional effects.

This collection will serve as a base to analyze, classify and later tune the techniques for the new combat system.

## 2. Technique stats

|Parameter|Description|
|---|---|
|Power|Base power of the technique.|
|MP|Magic-point cost needed to execute it.|
|Range|Range of the technique.|
|Effect|Additional effects, such as stun, poison or confuse.|

The effects and values of the techniques must be verified before using them as a definitive balance base.

## 3. Original arsenal

### 3.1 Fire

|Technique|Power|MP|Range|Effect|
|---|---|---|---|---|
|Fire Tower|155|81|Long|Stun|
|Prominence Beam|444|183|Long|Flat|
|Spitfire|66|30|Long|—|
|Red Inferno|210|171|Wide|—|
|Magma Bomb|279|132|Long|Confuse|
|Heat Laser|84|105|Wide|Flat|
|Infinity Burn|488|264|Wide|Stun|
|Meltdown|400|318|Wide|Stun|

### 3.2 Battle

|Technique|Power|MP|Range|Effect|
|---|---|---|---|---|
|Tremar|178|168|Wide|—|
|Muscle Charge|0|66|Self|ATK +30%|
|War Cry|0|42|Self|ATK +10%, DEF +5%, SPD +10%|
|Sonic Jab|52|18|Short|—|
|Dynamite Kick|193|99|Short|Stun|
|Counter|285|165|Short|Confuse|
|Megaton Punch|320|186|Short|Stun|
|Buster Dive|500|258|Long|Confuse|

### 3.3 Air

|Technique|Power|MP|Range|Effect|
|---|---|---|---|---|
|Thunder Justice|586|330|Long|Stun|
|Spinning Shot|389|150|Long|—|
|Electric Cloud|120|69|Long|Stun|
|Megalo Spark|382|174|Long|Stun|
|Static Elect|85|45|Short|Stun|
|Wind Cutter|178|93|Long|—|
|Confused Storm|225|216|Wide|Confuse|
|Hurricane|366|253|Wide|Confuse|

### 3.4 Earth

|Technique|Power|MP|Range|Effect|
|---|---|---|---|---|
|Poison Powder|117|171|Wide|Poison|
|Bug|500|354|Long|Flat|
|Mass Morph|0|30|Self|DEF +20%, SPD +10%|
|Insect Plague|58|96|Long|Poison|
|Charm Perfume|180|210|Wide|Confuse|
|Poison Claw|62|51|Short|Poison|
|Danger Sting|157|102|Short|Flat|
|Green Trap|310|147|Long|Stun|

### 3.5 Ice

|Technique|Power|MP|Range|Effect|
|---|---|---|---|---|
|Giga Freeze|264|120|Long|Stun|
|Ice Statue|424|186|Long|Stun|
|Winter Blast|120|169|Wide|Stun|
|Ice Needle|126|78|Long|Stun|
|Water Blitz|211|102|Long|—|
|Aqua Magic|0|36|Self|ATK +7%, DEF +7%, SPD +7%|
|Aurora Freeze|430|258|Wide|Flat|
|Tear Drop|60|42|Long|Flat|

### 3.6 Mech

|Technique|Power|MP|Range|Effect|
|---|---|---|---|---|
|Power Crane|226|126|Long|—|
|All Range Beam|573|330|Wide|—|
|Metal Sprinter|150|165|Wide|—|
|Pulse Laser|389|168|Long|—|
|Delete Program|430|219|Long|Flat|
|DG Dimension|722|420|Wide|—|
|Full Potential|0|99|Self|ATK +25%, DEF +15%, SPD +15%|
|Reverse Program|256|297|Long|Flat|

### 3.7 Filth

|Technique|Power|MP|Range|Effect|
|---|---|---|---|---|
|Odor Spray|88|75|Long|Stun|
|Poop Round Toss|122|96|Long|Poison|
|Big Poop Toss|211|192|Long|Confuse|
|Big Random Toss|211|282|Wide|Confuse|
|Poop Random Toss|75|120|Wide|Poison|
|Random Speed Toss|122|216|Wide|Poison|
|Horizontal Kick|53|24|Short|—|
|Ultimate Poop Hell|333|333|Wide|Flat|

---

## 4. Effects present in the arsenal

|Effect|General function|
|---|---|
|Stun|Interferes with or temporarily incapacitates the target.|
|Confuse|Alters the target's behaviour.|
|Poison|Applies damage or progressive deterioration.|
|Flat|Special effect whose behaviour must be verified individually.|
|Buff|Temporary improvement of attributes.|

> The exact description, duration, probability and behaviour of each effect must be researched individually.

---

## 5. New technique candidates — Holy and Dark

> The following techniques come from Digimon World 2 and are proposed as candidates to expand DWPF's Holy and Dark specialties. Their original mechanics serve as reference, while their power, range, behaviour and MP consumption must be adapted to DWPF's combat system.

### Holy

|Technique|Type|Original DW2 AP|Original DW2 MP|Proposed range|DWPF adaptation|Estimated DWPF MP|
|---|---|---|---|---|---|---|
|Heaven's Arrow|Offensive|35|16|Long|High-speed sacred-energy projectile against a single target.|71|
|Fist of Fate|Offensive|27.5|10|Short|Fast-executing focused energy strike, able to punish vulnerable enemies.|8|
|Crimson Flame|Support|—|50|Medium|Revives an allied Digimon and fully restores its HP. Adaptation proposal: restricted recovery with a penalty to prevent abuse.|To define|
|Full HP Cure|Support|—|80|Medium|Fully restores the HP of all allied Digimon.|To define|
|Full Recovery|Support|—|18|Medium|Full HP recovery of a single ally.|To define|
|HP Recovery|Support|—|24|Medium|Restores 150 HP to all allied Digimon. The amount must adapt to the new health system.|To define|
|Invincibility|Defense / Support|—|20|Medium|Grants temporary invulnerability to an ally. DWPF must limit its duration and define which effects it can block.|To define|
|Anti-Confusion|Support|—|12|Short|Removes the confusion status from an allied Digimon. Proposed as a status-cleansing technique.|To define|

### Dark

|Technique|Type|Original DW2 AP|Original DW2 MP|Proposed range|DWPF adaptation|Estimated DWPF MP|
|---|---|---|---|---|---|---|
|Dark Shot|Offensive|40|20|Long|High-impact dark projectile against one enemy.|72|
|Darkness Gear|Offensive|17.5|8|Medium|Fires a dark-energy mechanism at a single target. Could adapt as a fast-executing projectile.|65|
|Darkside Attack|Offensive / Control|32.5|20|Medium|Temporarily prevents the enemy from recovering HP.|To define|
|Demi Dart|Offensive / Debuff|15|8|Long|Damages the enemy and slightly reduces its MP.|To define|
|E-Stun Blast|Offensive / Control|15|8|Medium|Fires a dark discharge that may paralyze the target.|To define|
|Evil Charm|Control|20|12|Medium|Dark-energy attack that may cause confusion.|To define|
|Evil Touch|Debuff|22.5|12|Short|Reduces the target's MP through a contact attack.|To define|
|Evil Wind|Offensive / Control|20|36|Medium|Affects all enemies and reduces their speed by 20%.|To define|

### Additional techniques that could enter evaluation

These techniques do not necessarily belong to Holy or Dark. They are included because their mechanics could serve to expand DWPF's arsenal.

|Technique|DW2 specialty|Original AP|Original MP|Original mechanic|Possible adaptation|
|---|---|---|---|---|---|
|Grisly Wing|Darkness|30|40|Attack against all enemies using bats.|Dark area attack, possibly with distributed damage.|
|Buffalo Breath|Darkness|22.5|10|Counterattack technique that may confuse the enemy.|Counterattack with a chance to apply confusion.|
|Crimson Claw|Darkness|35|18|May transfer confusion, poison or stun from the user to the enemy.|Altered-status transfer technique.|
|Darkness Ray|Darkness|—|10|Modifies the Digimon's techniques, turning them into Darkness attacks.|Temporary elemental conversion system for techniques.|
|Electro-Shocker|Nature|22.5|12|Interrupts the enemy and reduces its attack power by 20%.|Interruption with offensive debuff.|
|Chrono Breaker|Machine|20|14|Delays the enemy's action.|Temporal control of execution or recovery.|
|GigaScissorClaw|Nature|30|40|Interrupts and temporarily disables the enemy's attacks.|Strong interruption or temporary action lock.|
|Banana Slip|None|—|60|Makes counterattacking enemies lose their turn.|Control technique that punishes defensive states or counterattacks.|

### Considerations

- The original AP and MP values belong to Digimon World 2.

- DWPF power values must not be transferred directly from DW2, as both systems use different combat structures.

- Holy and Dark specialties are assigned by the technique's theme and mechanic, not necessarily by its original DW2 specialty.

- Recovery, resurrection, invulnerability and drain techniques require specific balance rules.

- Control techniques must account for interruptions, duration, target resistance and execution windows.

- The candidate catalogue does not represent approved techniques. Its purpose is to serve as selection material for the new DWPF arsenal.
