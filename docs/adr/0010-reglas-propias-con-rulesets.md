# 0010. Reglas propias, organizadas en rulesets

- **Estado:** Aceptada
- **Fecha:** 2026-10-18

## Contexto

El objetivo es reinventar el juego, no reproducirlo. Había que decidir entre dos opciones:

- **A.** Reproducir el comportamiento del original (fórmulas, tiempos, IA, generador de aleatorios y bugs), verificado
  con trazas y tests diferenciales.
- **B.** Usar el original solo como inspiración.

## Decisión

**Opción B.** El original es un punto de partida, no un contrato. Además:

- **Las reglas del juego son datos, agrupados en rulesets.** Un ruleset reúne las fórmulas y los valores: evolución,
  vida, cuidado, daño, economía. Vive en un paquete de contenido, en YAML y Lua.
- **El primer ruleset parte de los valores del original**, sacados de las tablas del disco y de las fichas de
  referencia. No se verifica que sea fiel: solo sirve como punto de partida del balance.
- **El ruleset del mod** es la reinvención, y cada modo de juego puede traer el suyo.
- **Cada sistema tiene una ficha de referencia** en [`docs/reference/`](../reference/README.md) que resume cómo
  funcionaba el original, para que diseño decida a conciencia qué conservar y qué cambiar.
- **La especificación es el GDD.** Cada sistema sigue el [ciclo de diseño](../conventions/design-cycle.md).

## Consecuencias

- Libertad de diseño desde el primer sistema y bastante menos trabajo: no hay trazas, ni tests diferenciales, ni una
  fase dedicada a reconstruir el original.
- El juego original no existe dentro del motor: quien quiera jugarlo usa su disco.
- Hacen falta **pilares de diseño** escritos y una persona responsable de diseño con la última palabra.
- El balance es trabajo propio: hacen falta tablas editables, un panel de depuración para ajustar valores en vivo,
  simulaciones (por ejemplo, mil crianzas para ver qué evoluciones salen) y pruebas de juego frecuentes.
- Probar una idea de diseño es editar datos, no código.

## Alternativas descartadas

- **A. Comportamiento fiel:** mucho trabajo dedicado a replicar cosas que se van a cambiar, y ata los tiempos y las
  fórmulas al original.
- **B sin rulesets:** las reglas quedarían fijas en C++, y cada cambio de diseño o cada modo de juego nuevo exigiría
  recompilar.
