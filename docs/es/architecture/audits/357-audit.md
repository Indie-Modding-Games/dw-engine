# Auditoría de #357: estado compartido de interacciones y objetos

Versión corta; el detalle está en [la auditoría en inglés](../../../architecture/audits/357-audit.md).

| Criterio | Antes | Después |
|---|---|---|
| La autoridad registra las interacciones | Hecho | Hecho |
| Los cambios llegan a quien corresponde | Hecho | Hecho |
| Una misma interacción no se procesa dos veces | Hecho | Hecho |
| Los objetos son coherentes entre clientes | Hecho | Hecho |
| Se separan las interacciones locales de las compartidas | Parcial | Hecho: la variable 247 (argumento de rutinas, pareja de la 248) pasa a ser de cada jugador |
| Se conserva el juego individual | Hecho | Hecho |

Variables por jugador de #421 y #423 revisadas en los 222 scripts: el trigger 101 y la variable 117 solo los usa el
trabajo de la tienda; la 248 ningún script la lee (es argumento de rutinas); la 110 es una tirada de trabajo, y en la
Mansión de juguetes (scripts 142 a 144) cada juego decide la puerta buena con su propia tirada. Nada se rompe.
