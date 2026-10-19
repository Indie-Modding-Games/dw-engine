# Fichas de referencia

Una ficha resume cómo funcionaba un sistema del juego original según el decomp. **No es una especificación:** el
juego se reinventa ([ADR 0010](../adr/0010-reglas-propias-con-rulesets.md)). Sirve para que diseño decida a conciencia
qué conservar, qué cambiar y qué tirar, y para sembrar el primer ruleset con valores del original.

Cada ficha es el paso 1 del [ciclo de diseño](../conventions/design-cycle.md).

## Formato de una ficha

Un fichero por sistema (`care.md`, `evolution.md`, `battle.md`…) con estas secciones:

1. **Qué hace**, en términos de juego.
2. **Reglas**: las fórmulas, condiciones y tiempos, explicados en lenguaje claro.
3. **Datos**: qué tablas del disco usa y qué significa cada campo.
4. **Rarezas**: comportamientos extraños o bugs conocidos.
5. **Dónde está en el decomp**: los ficheros y funciones, para quien quiera profundizar.

## Reglas

- Se describe con palabras propias. No se copian fragmentos de código ni tablas de datos del decomp
  (ver [política legal](../legal/assets.md)).
- Cada afirmación dice si está comprobada en el decomp o es una suposición.
