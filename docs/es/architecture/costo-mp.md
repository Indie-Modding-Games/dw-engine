# Sistema de costes de técnicas — MP Cost Formula v0.2

## 1. Objetivo

Establecer un sistema de costes de MP para las técnicas de combate que tenga en cuenta su potencia, efectos secundarios, alcance y área de impacto.

El objetivo es evitar que las técnicas más versátiles sean automáticamente las mejores opciones, promoviendo la toma de decisiones entre potencia, eficiencia y posicionamiento.

Una técnica no debe tener un coste basado únicamente en su daño. Su valor también depende de los efectos que produce, la distancia desde la que puede ejecutarse y la cantidad de objetivos que puede alcanzar.

---

## 2. Fórmula general

El coste de una técnica se calcula mediante la suma de sus componentes:

```text
MP_final = ⌈ (D × 0.30) + Σᵢ (Eᵢ × Pᵢ × Tᵢ) + R + A ⌉
```

Donde:

|Variable|Descripción|
|---|---|
|D|Daño base de la técnica|
|E|Coste base de cada efecto adicional|
|P|Probabilidad de aplicar el efecto|
|T|Multiplicador de duración|
|R|Modificador de distancia|
|A|Modificador de área de impacto|
|⌈ ⌉|Redondeo hacia arriba al entero más cercano|

Los componentes adicionales se suman al coste de daño base.

---

## 3. Coste por daño

El componente de daño representa el coste energético necesario para ejecutar la técnica.

```text
MP_daño = D × 0.30
```

|Daño base|Coste de MP|
|---|---|
|50|15|
|100|30|
|150|45|
|200|60|
|300|90|
|400|120|
|500|150|
|700|210|

El daño base constituye el componente principal del coste de las técnicas ofensivas.

---

## 4. Coste de efectos adicionales

Los efectos adicionales aumentan el coste de la técnica dependiendo de su impacto, probabilidad de aplicación y duración.

### 4.1 Costes base

|Efecto|Coste base|
|---|---|
|Stun|50|
|Confusión|40|
|Veneno|35|
|Slow|35|
|Knockdown|45|
|Desplazamiento|25|
|Interrupción ligera|25|
|Interrupción fuerte|50|
|Reducción de ATK|40|
|Reducción de DEF|40|
|Reducción de SPD|40|
|Bloqueo de técnicas / silencio|60|

### 4.2 Cálculo

Cada efecto utiliza la siguiente fórmula:

```text
MP_efecto = E × P × T
```

Donde:

- **E:** coste base del efecto.

- **P:** probabilidad de aplicación expresada entre 0 y 1.

- **T:** multiplicador de duración.


Cuando una técnica aplica múltiples efectos, sus costes se suman.

---

## 5. Multiplicador de duración

La duración de los efectos influye directamente en su coste.

|Duración|Multiplicador|
|---|---|
|Instantánea|×1.00|
|Corta|×1.00|
|Media|×1.25|
|Larga|×1.50|

Los efectos que permanezcan activos durante períodos prolongados deberán tener un coste mayor que aquellos de aplicación instantánea.

---

## 6. Coste por distancia

El alcance de una técnica representa una ventaja táctica. Las técnicas que pueden ejecutarse desde una distancia considerable reducen la exposición del Digimon al combate directo.

Por este motivo, las técnicas de distancia media y larga tienen un coste adicional significativamente mayor.

|Distancia|Coste adicional|
|---|---|
|Personal|0 MP|
|Corta|0 MP|
|Media|30 MP|
|Larga|60 MP|

El coste de distancia se aplica independientemente del daño y de los efectos de la técnica.

Este sistema busca que las técnicas de largo alcance sean una ventaja táctica, pero no una opción gratuita frente a las técnicas de corto alcance.

---

## 7. Coste por área de impacto (AoE)

Las técnicas que pueden afectar a múltiples objetivos tienen un coste adicional debido a su mayor potencial durante los combates grupales.

|Área|Coste adicional|
|---|---|
|Objetivo único|0 MP|
|AoE pequeña|20 MP|
|AoE media|40 MP|
|AoE grande|60 MP|

El área de impacto representa el potencial de la técnica para afectar simultáneamente a varios enemigos.

---

## 8. Costes de soporte

Las técnicas de soporte utilizan valores base según la magnitud del efecto.

### 8.1 Buffs

|Efecto|Coste|
|---|---|
|Mejora ligera de un atributo|25|
|Mejora moderada de un atributo|40|
|Mejora fuerte de un atributo|60|
|Mejora ligera de varios atributos|45|
|Mejora moderada de varios atributos|70|
|Mejora fuerte de varios atributos|100|

Los buffs que posean duración utilizan el multiplicador de duración correspondiente.

### 8.2 Curación y protección

|Efecto|Coste|
|---|---|
|Curación pequeña|25|
|Curación moderada|45|
|Curación grande|70|
|Regeneración temporal|30|
|Recuperación de estado|25|
|Escudo pequeño|30|
|Escudo moderado|50|
|Escudo fuerte|80|

Cuando una técnica combina varios efectos de soporte, sus costes se suman.

---

## 9. Ejemplos de cálculo

### Ejemplo 1: ataque de daño directo

- Daño: 100

- Distancia: Media

- Objetivos: Individual

- Efectos adicionales: Ninguno


Cálculo:

```text
MP = 100 × 0.30 + 30 = 60
```

**Coste final: 60 MP**

---

### Ejemplo 2: ataque con Stun

- Daño: 200

- Stun: 25%

- Distancia: Larga

- Objetivos: Individual


Cálculo:

```text
MP = 60 + (50 × 0.25) + 60 = 132.5
```

**Coste final: 133 MP**

---

### Ejemplo 3: ataque de área con veneno

- Daño: 150

- Veneno: 50%

- Distancia: Larga

- Área: Grande

- Duración: Media


Cálculo:

```text
MP = 45 + (35 × 0.50 × 1.25) + 60 + 60 = 186.875
```

**Coste final: 187 MP**

---

### Ejemplo 4: técnica de soporte

- Buff moderado de ATK

- Buff moderado de SPD

- Duración: Media


Cálculo:

```text
MP = (40 + 40) × 1.25 = 100
```

**Coste final: 100 MP**

---

## 10. Principios de balance

- El daño constituye la base del coste de una técnica ofensiva.

- Los efectos adicionales incrementan el coste según su impacto, probabilidad y duración.

- Las técnicas de largo alcance deben tener un coste superior debido a su ventaja táctica.

- Las técnicas de área deben ser considerablemente más costosas que las técnicas individuales.

- Los buffs, debuffs y efectos de control deben reflejar su impacto real en combate.

- Las técnicas que combinen múltiples ventajas deberán asumir el coste acumulado de dichas ventajas.

- El coste de MP debe favorecer la diversidad de técnicas y evitar que una única técnica sea óptima para todas las situaciones.

- La distancia, el área y los efectos no deben ser gratuitos simplemente por estar incluidos dentro de una técnica.

---

## 11. Estado del sistema

**Versión:** MP Cost Formula v0.2

Este sistema constituye una base inicial de diseño. Los valores deberán ajustarse mediante pruebas de combate, considerando:

- MP máximo de cada Digimon.

- Regeneración de MP durante el combate.

- Duración promedio de los enfrentamientos.

- Frecuencia de uso de técnicas.

- Eficiencia de las técnicas de área.

- Rendimiento de técnicas de largo alcance.

- Impacto de efectos de control.

- Balance entre combate individual y multijugador.


La fórmula busca establecer una estructura consistente para la creación de técnicas, pero sus valores no se consideran definitivos hasta completar pruebas de balance.
