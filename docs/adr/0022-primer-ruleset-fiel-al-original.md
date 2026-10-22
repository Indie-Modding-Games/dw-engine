# 0022. El primer ruleset reproduce el original

- **Estado:** Aceptada
- **Fecha:** 2026-10-20

## Contexto

El [ADR 0010](0010-reglas-propias-con-rulesets.md) decide que las reglas son datos agrupados en rulesets y que el
primer ruleset solo parte de los valores del original, sin comprobar que sea fiel. También pide pilares de diseño y
una persona responsable de diseño antes de diseñar los sistemas.

Al empezar la fase 3 el GDD del repositorio de diseño sigue vacío. Esperarlo bloquearía el núcleo jugable, y diseñar
cada sistema sin él daría criterios distintos en cada uno.

## Decisión

- **El primer ruleset, `original`, reproduce las reglas del juego original tal como están en el decomp y en las
  tablas del disco.** Su especificación son las [fichas de referencia](../reference/index.md); el código cita la
  función del decomp que reproduce.
- **El diseño nuevo llega como otros rulesets** sobre los mismos sistemas. Por eso las reglas que son lógica, y no
  números, van detrás de una estrategia que elige el ruleset.
- **Responsable de diseño: Rivarux**, responsable del proyecto. Aprueba cada decisión de diseño; cualquier cambio
  respecto al original es una de ellas.

Matiza el ADR 0010 sin sustituirlo: el objetivo sigue siendo reinventar el juego, y el original es el primer ruleset,
no el contrato del motor.

## Consecuencias

- La fase 3 empieza sin esperar al GDD y deja un bucle completo que se puede jugar, medir y comparar.
- Cada ruleset nuevo se mide contra el original con las mismas simulaciones.
- Reproducir el original cuesta más que inspirarse en él: hay que leer el decomp sistema a sistema y escribir tests a
  partir de las fichas.
- Las rarezas del original son preguntas de diseño: el responsable de diseño decide cuáles se reproducen.

## Alternativas descartadas

- **Esperar al GDD:** bloquea la fase 3 sin fecha.
- **Diseñar cada sistema sobre la marcha:** decisiones de diseño tomadas por programación, sin criterio común.
