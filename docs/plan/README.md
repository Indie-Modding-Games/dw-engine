# Plan del proyecto: especificaciones por fase

Esta carpeta explica **qué construye cada fase, por qué y cómo se sabe que está terminada**. Seguimos un enfoque de
desarrollo guiado por especificaciones (*spec-driven development*): antes de programar, cada fase tiene escrita su
especificación con requisitos que se pueden comprobar. El código se acepta cuando cumple esos requisitos, no cuando
"parece que funciona".

El [plan por fases](../roadmap.md) es el resumen con el estado del día. Aquí está el detalle.

## Las fases

| Fase | Especificación | En una frase |
|---|---|---|
| 0 | [Base](fase-0-base.md) | El esqueleto del motor: compila en cualquier PC, abre una ventana, lee el mando y ejecuta Lua |
| 1 | [Tubería de assets](fase-1-tuberia-de-assets.md) | Sacar todo el contenido del disco original a formatos abiertos y prepararlo para el motor |
| 2 | [Prueba vertical](fase-2-prueba-vertical.md) | Un trozo pequeño del juego, jugable de punta a punta: caminar de File City a la casa de Jijimon y volver |
| 3 | [Núcleo jugable](fase-3-nucleo-jugable.md) | El bucle principal del juego: criar, entrenar, combatir y evolucionar, con el diseño nuevo |
| 4 | [Contenido y modos](fase-4-contenido-y-modos.md) | La historia, los personajes, los modos de juego y la API de mods; termina con la primera versión del mod |

```mermaid
flowchart LR
    f0[0. Base] --> f1[1. Tubería de assets] --> f2[2. Prueba vertical] --> f3[3. Núcleo jugable] --> f4[4. Contenido y modos]
    gdd[[Pilares de diseño y GDD]] --> f3
    gdd --> f4
```

Cada fase usa lo que deja la anterior. Dos fases pueden solaparse cuando la siguiente solo necesita lo que ya está
terminado de la anterior. Por ejemplo, la fase 2 empezó con la extracción completa de la fase 1, mientras se integraba
la compresión del cocinado.

## Cómo es una especificación

Todas siguen la misma estructura:

| Sección | Qué responde |
|---|---|
| Objetivo | Qué existe al terminar la fase que no existía antes |
| Por qué | Qué problema resuelve y por qué va en este orden |
| Alcance | Qué entra |
| Fuera de alcance | Qué no entra, aunque lo parezca, y en qué fase va |
| Requisitos | Lista numerada (`R1.3` es el requisito 3 de la fase 1). Cada requisito dice cómo se comprueba |
| Diseño | Módulos que se tocan y las decisiones que aplican (ADR) |
| Criterio de cierre | La prueba concreta que da la fase por terminada |
| Dependencias y riesgos | Qué necesita la fase y qué puede salir mal |
| Estado | Lo que está hecho y lo que falta, con fecha |

## Cómo se trabaja con ellas

1. **Antes de empezar una fase** se escribe o se revisa su especificación. Un cambio de alcance se hace primero aquí,
   en un PR, y luego en el código.
2. **Cada PR dice qué requisitos cumple** (por ejemplo, "R2.4, R2.5"). Si un PR no cumple ningún requisito, o falta
   uno en la especificación o el PR no toca.
3. **Cada requisito tiene su comprobación**: un test automático siempre que se pueda; si no, una prueba manual
   descrita paso a paso.
4. **La fase se cierra** cuando todos los requisitos se cumplen y la prueba del criterio de cierre pasa. Entonces se
   actualiza el estado aquí y en el [plan](../roadmap.md).
5. **Las reglas del juego no se especifican aquí.** Desde la fase 3, cómo funciona cada sistema (cuidado, batalla,
   evolución…) lo define el GDD del repositorio de diseño, según el [ciclo de diseño](../conventions/design-cycle.md).
   Estas especificaciones dicen qué tiene que hacer el motor para que ese diseño se pueda construir.

## Reglas que valen para todas las fases

- **Sin limitaciones de PS1.** Cualquier resolución, IDs por nombre, nada de tablas de tamaño fijo
  ([ADR 0001](../adr/0001-reimplementacion-nativa.md), [ADR 0013](../adr/0013-juego-nativo-de-pc.md)).
- **El decomp es la referencia, no el código.** Se lee para entender formatos y comportamiento; el motor se escribe
  desde cero.
- **El runtime nunca lee formatos de PS1.** Solo `dwx` usa `formats/`.
- **Nada del juego original en los repositorios.** Cada persona extrae su propio disco
  ([política legal](../legal/assets.md)).
- **Calidad mínima de cada commit en `main`:** compila sin warnings en el preset `ci`, clang-tidy sin avisos,
  formato correcto y todos los tests en verde.
