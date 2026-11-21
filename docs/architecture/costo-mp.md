# Technique cost system — MP Cost Formula v0.2

## 1. Goal

Establish an MP cost system for combat techniques that accounts for their power, side effects, range and area of impact.

The goal is to prevent the most versatile techniques from automatically being the best options, encouraging decisions between power, efficiency and positioning.

A technique's cost must not be based on damage alone. Its value also depends on the effects it produces, the distance from which it can be executed and the number of targets it can reach.

---

## 2. General formula

A technique's cost is calculated by summing its components:

```text
MP_final = ⌈ (D × 0.30) + Σᵢ (Eᵢ × Pᵢ × Tᵢ) + R + A ⌉
```

Where:

|Variable|Description|
|---|---|
|D|Base damage of the technique|
|E|Base cost of each additional effect|
|P|Probability of applying the effect|
|T|Duration multiplier|
|R|Distance modifier|
|A|Area of impact modifier|
|⌈ ⌉|Rounding up to the nearest integer|

Additional components are added on top of the base damage cost.

---

## 3. Damage cost

The damage component represents the energy cost required to execute the technique.

```text
MP_damage = D × 0.30
```

|Base damage|MP cost|
|---|---|
|50|15|
|100|30|
|150|45|
|200|60|
|300|90|
|400|120|
|500|150|
|700|210|

Base damage is the main cost component of offensive techniques.

---

## 4. Additional effect cost

Additional effects raise the technique's cost depending on their impact, application probability and duration.

### 4.1 Base costs

|Effect|Base cost|
|---|---|
|Stun|50|
|Confusion|40|
|Poison|35|
|Slow|35|
|Knockdown|45|
|Displacement|25|
|Light interruption|25|
|Strong interruption|50|
|ATK reduction|40|
|DEF reduction|40|
|SPD reduction|40|
|Technique lock / silence|60|

### 4.2 Calculation

Each effect uses the following formula:

```text
MP_effect = E × P × T
```

Where:

- **E:** base cost of the effect.

- **P:** application probability expressed between 0 and 1.

- **T:** duration multiplier.


When a technique applies multiple effects, their costs are added together.

---

## 5. Duration multiplier

Effect duration directly influences its cost.

|Duration|Multiplier|
|---|---|
|Instant|×1.00|
|Short|×1.00|
|Medium|×1.25|
|Long|×1.50|

Effects that stay active for long periods must cost more than instant-application ones.

---

## 6. Distance cost

A technique's range is a tactical advantage. Techniques that can be executed from considerable distance reduce the Digimon's exposure to direct combat.

For this reason, medium- and long-range techniques carry a significantly higher additional cost.

|Distance|Additional cost|
|---|---|
|Personal|0 MP|
|Short|0 MP|
|Medium|30 MP|
|Long|60 MP|

The distance cost applies independently of the technique's damage and effects.

This system aims for long-range techniques to be a tactical advantage, but not a free option over short-range ones.

---

## 7. Area of impact (AoE) cost

Techniques that can affect multiple targets carry an additional cost due to their greater potential in group fights.

|Area|Additional cost|
|---|---|
|Single target|0 MP|
|Small AoE|20 MP|
|Medium AoE|40 MP|
|Large AoE|60 MP|

The area of impact represents the technique's potential to affect several enemies simultaneously.

---

## 8. Support costs

Support techniques use base values according to the magnitude of the effect.

### 8.1 Buffs

|Effect|Cost|
|---|---|
|Light boost of one attribute|25|
|Moderate boost of one attribute|40|
|Strong boost of one attribute|60|
|Light boost of several attributes|45|
|Moderate boost of several attributes|70|
|Strong boost of several attributes|100|

Buffs with a duration use the corresponding duration multiplier.

### 8.2 Healing and protection

|Effect|Cost|
|---|---|
|Small heal|25|
|Moderate heal|45|
|Large heal|70|
|Temporary regeneration|30|
|Status recovery|25|
|Small shield|30|
|Moderate shield|50|
|Strong shield|80|

When a technique combines several support effects, their costs are added.

---

## 9. Calculation examples

### Example 1: direct damage attack

- Damage: 100

- Distance: Medium

- Targets: Single

- Additional effects: None


Calculation:

```text
MP = 100 × 0.30 + 30 = 60
```

**Final cost: 60 MP**

---

### Example 2: attack with Stun

- Damage: 200

- Stun: 25%

- Distance: Long

- Targets: Single


Calculation:

```text
MP = 60 + (50 × 0.25) + 60 = 132.5
```

**Final cost: 133 MP**

---

### Example 3: area attack with poison

- Damage: 150

- Poison: 50%

- Distance: Long

- Area: Large

- Duration: Medium


Calculation:

```text
MP = 45 + (35 × 0.50 × 1.25) + 60 + 60 = 186.875
```

**Final cost: 187 MP**

---

### Example 4: support technique

- Moderate ATK buff

- Moderate SPD buff

- Duration: Medium


Calculation:

```text
MP = (40 + 40) × 1.25 = 100
```

**Final cost: 100 MP**

---

## 10. Balance principles

- Damage is the base of an offensive technique's cost.

- Additional effects raise the cost according to their impact, probability and duration.

- Long-range techniques must cost more due to their tactical advantage.

- Area techniques must cost considerably more than single-target ones.

- Buffs, debuffs and control effects must reflect their real impact in combat.

- Techniques combining multiple advantages must absorb the accumulated cost of those advantages.

- MP cost should favour technique diversity and prevent one technique from being optimal in every situation.

- Distance, area and effects must not be free simply for being included in a technique.

---

## 11. System status

**Version:** MP Cost Formula v0.2

This system is an initial design base. Values must be tuned through combat testing, considering:

- Each Digimon's maximum MP.

- MP regeneration during combat.

- Average encounter duration.

- Technique usage frequency.

- Efficiency of area techniques.

- Performance of long-range techniques.

- Impact of control effects.

- Balance between solo and multiplayer combat.


The formula aims to establish a consistent structure for creating techniques, but its values are not considered final until balance testing is complete.
