# Fase 3: Núcleo jugable

## Objetivo

El bucle principal del juego funciona con las reglas del original: criar al partner, cuidarlo, entrenarlo, combatir y
evolucionar. Con guardado, inventario y herramientas para ajustar el balance mientras se juega. Todo está montado
sobre rulesets, para que el diseño nuevo llegue después como otro ruleset sin reescribir los sistemas.

## Por qué

Es el corazón del juego y lo que más va a cambiar respecto al original. Va después de la prueba vertical porque
necesita el mundo funcionando (campo, render, interfaz). Va antes que la historia porque la historia se apoya en estos
sistemas.

Se empieza por las reglas del original porque el GDD del repositorio de diseño aún está vacío. Así el motor tiene un
bucle completo que se puede jugar y medir, y cada decisión del diseño nuevo se toma comparándola con algo que ya
funciona (decisión del responsable de diseño, 2026-10-20).

## Condición para empezar

- **El primer ruleset es el original.** Reproduce las reglas del juego original tal como están en el decomp y en las
  tablas del disco que extrae `dwx`. No hace falta el GDD para empezar.
- **El diseño nuevo llega como rulesets** que aprueba el responsable de diseño. Cada uno se monta encima de los
  sistemas de esta fase, como otro ruleset, sin reescribirlos.
- **Responsable de diseño: Rivarux**, responsable del proyecto. Es quien tiene la última palabra que pide el
  [ADR 0010](../adr/0010-reglas-propias-con-rulesets.md) y aprueba cada decisión de diseño. Todo lo que aparte el
  juego del original, y cada pregunta de diseño abierta, se le pregunta; programación no lo decide
  ([ADR 0022](../adr/0022-primer-ruleset-fiel-al-original.md)).

## Cómo se especifica cada sistema

Para el ruleset original, la especificación de cada sistema es su [ficha de referencia](../reference/README.md) y el
decomp. Cada sistema sigue el [ciclo de diseño](../conventions/design-cycle.md), con el paso del GDD sustituido por la
ficha:

1. ficha de referencia del original en [`docs/reference/`](../reference/README.md);
2. implementación con las reglas en el ruleset `original`;
3. tests escritos a partir de la ficha;
4. ajuste en vivo y pruebas de juego.

Los rulesets del diseño nuevo vuelven al ciclo completo: diseño en el GDD, aprobación e implementación.

## Alcance

- **Rulesets:** el formato, el cargador, la validación en `dwx validate` y el ruleset `original`.
- **Partner:** stats, cuidado (hambre, sueño, aseo, ánimo, disciplina, cansancio, enfermedad y heridas), vida, muerte
  y renacer, entrenamiento y evolución.
- **Batalla.**
- **Inventario y guardado:** ranuras y autoguardado, en ficheros, sin tarjetas de memoria.
- **Panel de ajuste en vivo** y simulaciones de balance.
- **Interfaz** de la batalla y de los menús del partner en RmlUi.
- **Cambio de mapa como el original:** fundido sustractivo a gris y nombre del mapa, que quedaron pendientes de la
  fase 2 (ver [campo](../reference/campo.md)).

## Fuera de alcance

- Historia, diálogos y misiones → fase 4.
- Modos de juego distintos del principal → fase 4 (aunque el ruleset ya los permite).
- El ruleset del diseño nuevo: llega cuando el GDD esté escrito y aprobado.

## Requisitos

| ID | Requisito | Cómo se comprueba |
|---|---|---|
| R3.1 | Las reglas (fórmulas y valores) viven en un ruleset de YAML y Lua dentro de un paquete, nunca fijas en C++. Cuando una regla es lógica y no un número, el ruleset elige la estrategia que la aplica | Revisión; test que cambia un valor del ruleset y ve el efecto; test que cambia la estrategia |
| R3.2 | El primer ruleset reproduce el original: sus constantes salen del decomp, con la referencia en el comentario, y sus tablas son las que `dwx` extrae del disco | Test que lo compara con las tablas de la fase 1; tabla de paridad con el decomp |
| R3.3 | Cada sistema (partner, cuidado, evolución, batalla, inventario) cumple su ficha de referencia con el ruleset `original`, y el documento del GDD con los rulesets del diseño nuevo | Tests escritos a partir de la ficha |
| R3.4 | Un ruleset se puede cambiar en caliente desde el panel de depuración, sin reiniciar | Prueba manual |
| R3.5 | Hay simulaciones que juegan miles de partidas sin gráficos para medir el balance | Herramienta de simulación con informe |
| R3.6 | El guardado usa ranuras y autoguardado; una partida guardada se carga igual tras actualizar el juego, o se migra | Tests de guardado y migración |
| R3.7 | La batalla y los menús del partner se usan con ratón, teclado y mando | Prueba manual con los tres dispositivos |
| R3.8 | El cambio de mapa se ve como en el original: fundido sustractivo a gris y nombre del mapa al entrar (pendientes de la fase 2, ver [campo](../reference/campo.md)) | Test del fundido y captura comparada con el original |
| R3.9 | Todo texto que ve el jugador sale de tablas de idioma de los paquetes, en inglés y español, con una sola búsqueda por clave desde C++, RmlUi y Lua; lo que falta en un idioma sale en inglés con un aviso. El idioma se elige en el menú principal (ratón, teclado y mando), cambia todo el juego sin reiniciar, también el texto del original si hay paquete de traducción, y se guarda en los ajustes del usuario; sin elección, el del sistema si es `en` o `es`, si no inglés. Un paquete añade idiomas o cambia textos sueltos ([localización](../architecture/localizacion.md)) | Tests de carga, respaldo, marcadores, mezcla de paquetes y cambio en caliente; `dwx validate` compara cada idioma con el inglés (claves que faltan o sobran, marcadores, lock de la traducción); la ruta `route-language.yaml` con `--lang en` y `--lang es` cambia el idioma en el menú principal y saca capturas de cada pantalla |

## Diseño

- Módulos: `game/partner`, `game/battle`, `game/inventory`, `game/save` y el formato de ruleset en un paquete de
  contenido.
- **Un ruleset** reúne secciones (cuidado, evolución, batalla…), cada una con su esquema. Los números van en YAML.
  Las tablas por Digimon, por técnica o por objeto no se copian: el ruleset nombra las tablas del paquete base que usa
  (`data/digimon`, `data/evolution`, `data/raise`…), y otro ruleset puede traer las suyas.
- **Estrategias:** cuando una regla es lógica (cómo se elige una evolución, cómo decide la IA de batalla), el sistema
  declara una interfaz y el ruleset dice qué implementación usar por nombre. `original` es la del juego original; un
  diseño nuevo añade la suya, en C++ o en Lua, sin tocar el resto del sistema.
- Las constantes del ruleset `original` sí se escriben en el paquete, con la referencia al decomp, como las de
  `field/rules.yaml` ([fichas de referencia](../reference/README.md#reglas)).
- Un reloj de juego con avance rápido, para los tests, el autotest y las simulaciones.
- La interfaz de estos sistemas usa RmlUi, como en la fase 2.

## Criterio de cierre

El bucle de criar, entrenar, combatir y evolucionar se juega con el ruleset `original`, y un autotest lo recorre
entero con el reloj acelerado: criar, entrenar, combatir y evolucionar.

## Dependencias y riesgos

- Depende de la fase 2.
- Riesgo: el balance. Se mitiga con rulesets editables, ajuste en vivo, simulaciones y pruebas de juego frecuentes.
- Riesgo: perder la identidad del juego. El ruleset `original` y las fichas de referencia permiten decidir a
  conciencia qué se conserva.
- Riesgo: que el ruleset `original` ate los sistemas al original. Se mitiga con las estrategias: lo que es lógica del
  original queda detrás de una interfaz que el ruleset elige.

## Estado (2026-10-20)

En curso. Las fichas de referencia de cuidado, evolución y batalla ya están escritas (fase 1).
