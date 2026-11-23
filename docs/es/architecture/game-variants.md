# Variantes de juego: Clásico Mejorado y Modo Folder

En cada partida nueva el jugador elige entre dos experiencias que comparten un motor y un mismo conjunto de packs:

- **Clásico Mejorado**: el juego original tal como era (su historia, reglas, balance y progresión) con las mejoras
  técnicas del motor, las opciones de comodidad y los arreglos de bugs.
- **Modo Folder**: el juego ampliado: contenido oculto, todas las especies como compañero, el paso de Ultimate a Mega,
  los objetos raros y todas las opciones, incluidas las que cambian las reglas.

El modo se elige cuando se crea la partida nueva. Esa elección reemplaza el antiguo paso de activar contenido oculto y
define el **tipo de guardado**: Clásico Mejorado o Modo Folder, registrado en la partida desde el primer fichero.

La decisión y las alternativas están en el [ADR 0026](../adr/0026-game-variants-as-modes.md). Esta página dice qué
activa cada variante, cómo se elige y se guarda, y cómo encajan el cooperativo, los mods y las variantes nuevas. También
recoge la organización de módulos y recursos, la estrategia de compilación y distribución, los sistemas que aún necesitan
refactorización y el plan de tareas.

## Qué es una variante

Una variante es un [modo de juego](../modules/game/modes.md) con un preajuste: un manifiesto `modes/<id>/mode.yaml` que
nombra los rulesets con los que puede empezar una partida nueva y, con su clave opcional `options`, las opciones de la
página de opciones que sus partidas fijan en un valor. No hay una segunda build, ni una rama de código por variante, ni
un flag en los sistemas: los sistemas leen su [ruleset](rulesets.md) y las opciones del jugador como siempre, y la
variante solo decide qué rulesets ofrece la partida nueva y qué opciones se quedan en el valor del original mientras se
juega una de sus partidas.

```yaml
# content/packs/game/modes/classic/mode.yaml
id: classic
name: modes.classic_name
description: modes.classic_description
ruleset: original
rulesets: [original]
order: 0
options:
  difficulty: normal
  rare_items: false
  free_exploration: false
  extra_toilets: false
  choose_egg_after_lives_lost: false
```

El Modo Folder (`modes/folder/mode.yaml`, orden 1) admite los seis rulesets de la pantalla de reglas y no fija nada.

## Matriz de funcionalidades

Todo lo que hace el motor es compartido; las variantes solo difieren en las reglas que puede usar una partida.

| Área | Clásico Mejorado | Modo Folder |
|---|---|---|
| Historia, mapas, sistemas (campo, cuidados, combate, evolución, torneos, trabajo en la tienda...) | Compartidos | Compartidos |
| Motor y mejoras técnicas (escenarios en HD, movimiento suave, iluminación, clima, pantalla ancha, sonido) | Sí, las opciones del jugador | Sí, las opciones del jugador |
| Opciones de comodidad (autoguardado, mapa de la isla, minimapa, combates rápidos, saltar evoluciones, intercambios en lote, nombres largos, extras de la tienda...) | Sí, las opciones del jugador | Sí, las opciones del jugador |
| Arreglos de bugs como opciones (los cuatro caprichos "justos", los arreglos de arte, los de la historia de Angemon y Whamon) | Sí, las opciones del jugador | Sí, las opciones del jugador |
| Reglas | Solo `original` | `original`, contenido oculto (`oculto`), paso a Mega (`oculto_mega`), todas las especies como compañero (`todas` y sus dos mezclas) |
| Dificultad fácil | Fija en normal | La opción del jugador |
| Objetos raros en el campo | Fijos desactivados | La opción del jugador |
| Exploración libre | Fija desactivada | La opción del jugador |
| Baños extra | Fijos desactivados | La opción del jugador |
| Elegir el huevo tras perder todas las vidas | Fijo desactivado | La opción del jugador |
| Cooperativo en línea | Sí (la variante del host rige el mundo compartido) | Sí |
| Versus en línea | Igual en ambas: no es una partida de historia | Igual |

La línea entre las dos sigue la división de la issue: una opción que arregla los bugs del original o lo hace más cómodo
sin cambiar lo que el jugador puede obtener ni lo difícil que es pertenece a ambas; una que cambia el balance, la
progresión o el contenido es del Modo Folder. Todo lo que los jugadores tienen hoy sigue al alcance: está en el Modo
Folder, o en ambas.

En resumen: el Clásico Mejorado es la experiencia original en todo lo que es jugabilidad —historia, reglas, balance y
progresión—, con solo mejoras técnicas, de comodidad y arreglos encima. El Modo Folder es el lugar de todo lo que cambia
la jugabilidad: contenido adicional, cambios de reglas, de balance y de progresión, y cualquier sistema o mecánica nueva.

Los extras que no son opciones (contenido oculto, paso a Mega, todas las especies como compañero) son rulesets, así que
el Clásico Mejorado los deja fuera simplemente al no admitir esos rulesets, y su partida nueva se salta la pantalla de
reglas.

## Elegir una variante

**Requisito de diseño:** el selector de modo reemplaza el antiguo paso de «activar contenido oculto» al crear una
partida nueva. El jugador elige el modo cuando se crea el fichero de guardado, y esa única elección define el tipo de
guardado: Clásico Mejorado o Modo Folder. El contenido oculto no es un interruptor aparte; forma parte de los rulesets
del Modo Folder. El modo se escribe en la partida nueva en el acto y no se puede cambiar después, así que cada partida
siempre juega la variante con la que se creó.

La partida nueva pregunta primero el modo, con la lista que ya dibuja cada skin (`ui/new_game_mode`): primero Clásico
Mejorado, luego Modo Folder, cada uno con una línea que dice qué es. Clásico Mejorado empieza en el acto con las reglas
del original, que no tienen contenido oculto. El Modo Folder es la partida ampliada; su lista de rulesets, cuando todavía
se muestra, solo elige con qué ruleset del Modo Folder empieza la partida. En el flujo previsto nunca pregunta si el
contenido oculto está encendido: el modo ya lo ha decidido. El `ui/new_game_rules` actual todavía conserva las filas
heredadas de contenido oculto; integrarlas en la elección de modo es la [tarea 10](#plan-de-tareas). Atrás, desde
cualquiera de las dos listas, vuelve a la lista de ranuras.

La variante pertenece a la partida, no al jugador ni a la instalación, así que el launcher no la elige: una build, una
descarga, un conjunto de packs, y cada partida lleva su propia variante. Una partida nueva que la línea de comandos
empieza sin el título (`--ruleset`, las rutas de test que se saltan los menús) juega el Modo Folder, que no fija nada.

## Opciones fijas mientras se juega

Cuando empieza una partida de un modo con `options`, el juego (`apps/game/src/mode_options.hpp`, `ModeOptions`):

- aplica esos valores en lugar de los del jugador: `PlaySession::playSettings()` es la configuración del jugador con
  los valores del modo encima, leída con el propio lector del archivo de configuración, ya que sus claves son los
  nombres de las opciones;
- los publica sobre los valores del jugador, con `options.locked.<opción>` activado y la clave del nombre del modo en
  `options.variant`; la [API de UI](../modding/ui-api.md) los responde con `options.locked(name)` y
  `options.variant()`, y `options.set` rechaza una opción fija; un valor fijo llega a la UI con el tipo de su opción
  (`true` y `false` como booleanos, un número entero como número, lo demás como texto);
- rechaza cualquier comando que cambiaría una (el valor se publica de nuevo);
- cuando la partida termina, vuelve a publicar lo que la UI mostraba antes, así que la página de opciones del título
  vuelve a mostrar los valores del jugador.

El archivo de configuración del jugador nunca cambia: un jugador que activó la dificultad fácil la encuentra activada
en su próxima partida del Modo Folder. La página de opciones agrupa las filas fijas al final de su pestaña, en gris y
sin flechas, con la etiqueta "Fija en Clásico Mejorado" y una línea de ayuda que dice dónde se puede cambiar. En el
título no hay nada fijo.

Un modo solo puede fijar las opciones que una partida aplica desde su propia configuración: la dificultad, los objetos
raros, la exploración libre, los baños extra, el huevo tras perder todas las vidas, los cuatro caprichos "justos", el
rango de conversación generoso, el inicio rápido de combate, saltar evoluciones, un cancelar que cierra una elección,
los arreglos de la historia de Angemon y Whamon, el modo del trabajo en la tienda y la dificultad del cooperativo
(`FixableOptions`). Un valor fijo sobre otra opción, o con un valor que la opción no admite, se deja fuera con su motivo
en el log, y los demás se mantienen.

## Partidas guardadas

- El modo y el tipo de guardado se deciden en el mismo paso en que se crea la partida nueva. El selector de modo
  reemplaza la antigua pregunta «activar contenido oculto»: elegir Clásico Mejorado o Modo Folder escribe ese id en el
  primer guardado, y ningún paso posterior activa contenido oculto ni cambia el tipo de una partida existente.
- La sesión guarda el modo en su sección `mode` (`id`), como antes. Una partida de antes de las variantes registraba
  `original`, el único modo que había, que no fijaba nada: se carga como Modo Folder (`game::modes::currentModeId`), así
  que conserva cada opción y ruleset que tenía. Una partida sin sección `mode` se carga igual.
- El formato de guardado 4 añade `mode` al resumen de la cabecera, para que la lista de ranuras nombre la variante de
  cada partida sin leer sus secciones. La migración desde el formato 3 lo deja vacío, que la lista de ranuras lee como
  Modo Folder, y el siguiente guardado lo rellena ([partidas guardadas](save.md)).
- Una partida siempre juega su propia variante: no hay forma de cargar una partida del Modo Folder como Clásico
  Mejorado, ni al revés, así que las dos no se pueden mezclar por accidente. La lista de ranuras muestra la variante
  junto a cada partida (tras la fecha en el diseño moderno, tras el compañero en el recuadro de resumen del diseño
  original).
- Una partida cuyo modo ya no tiene ningún pack (el modo de un mod, con su mod fuera) se carga sin ninguna opción fija,
  con un aviso en el log.

## Cooperativo

Un invitado del cooperativo juega la partida del host, así que la partida del invitado tiene el modo del host: la
variante del host rige el mundo compartido, y la página de opciones del invitado muestra las mismas filas fijas. Cuando
la partida que trae el invitado juega la otra variante, nada se mezcla en silencio: el aviso de inicio del invitado
dice que el mundo juega otra variante, y el host oye qué jugador la trajo. Ambos lados comparan las partidas que ya
intercambian, así que el protocolo no cambia. El compañero y la bolsa del invitado vuelven a su partida solo cuando su
variante admite todo lo que puede dar la del host (`game::modes::takesProgressFrom`: el mismo modo, o uno que no fija
nada y admite todos los rulesets del del host). Un invitado del Modo Folder en un mundo de Clásico Mejorado se lleva su
progreso; un invitado de Clásico Mejorado en un mundo del Modo Folder no se lleva nada, y su aviso dice que su partida
queda como estaba, así que una partida de Clásico Mejorado nunca guarda lo que hicieron las reglas del Modo Folder. La
lista de salas aún no nombra la variante del host: necesita que el directorio de salas la lleve (pendiente). La
issue ponía el multijugador en el Modo Folder; el cooperativo se queda en ambas variantes, ya que no cambia ninguna regla
de una partida, y quitarlo del Clásico Mejorado quitaría una función a sus jugadores. La dificultad del cooperativo (los
ayudantes hacen los combates más duros) es una opción de ambas.

## Mods y packs de contenido

- El pack base (lo que `dwx` extrae del disco) y el pack del juego son compartidos por ambas variantes. El contenido que
  solo usa el Modo Folder llega como rulesets o secciones de ruleset que solo el Modo Folder admite (el contenido oculto,
  el paso a Mega, todas las especies como compañero, la sección `rare_items` de los objetos raros), así que no hay una
  segunda copia de ningún recurso.
- Un mod añade una variante con su propio `modes/<id>/mode.yaml`: sus rulesets y las opciones que fija. Sustituye una de
  las del juego dando la misma carpeta (`modes/folder/mode.yaml`), como cualquier recurso, por ejemplo para admitir su
  propio ruleset en el Modo Folder.
- Un mod que cambia la historia o el balance para todas las partidas se sigue aplicando a ambas variantes, ya que los
  packs se montan para todo el juego. Un mod así debería traer sus reglas como un ruleset que solo admitan su modo o el
  Modo Folder.

## Añadir una variante

Una variante nueva es un `mode.yaml` nuevo con sus rulesets, sus opciones fijas y sus dos textos
(`localization/<idioma>/`), nada más: la lista, la partida guardada, la lista de ranuras y la página de opciones lo
siguen. La lista muestra los seis primeros modos.

## Organización de módulos y recursos

La lógica de las variantes es deliberadamente delgada: está en los bordes del juego, no en los sistemas que lo juegan.

| Capa | Dónde | Qué contiene |
|---|---|---|
| Reglas y datos | `content/packs/game/modes/<id>/mode.yaml`, `content/schemas/mode.schema.json` | Los rulesets que admite un modo, su ruleset inicial, su posición en la lista y sus opciones fijas. Sin código por modo |
| Catálogo de modos | `packages/game/modes` (`ModeCatalog`, `parseGameMode`) | Lee y valida los manifiestos, comprueba que existen los rulesets y ordena la lista. Un modo roto se omite y se registra |
| Partida nueva | `apps/game` (`title_screen`, `NewGameModes`) | Pregunta el modo al crear la partida nueva: esa elección define el tipo de guardado y reemplaza la activación de contenido oculto; la pantalla de reglas heredada aún debe integrarse en ella (tarea 10) |
| Sesión de juego | `apps/game` (`PlaySession`, `ModeOptions`) | Aplica las fijaciones del modo sobre la configuración del jugador, las publica como bloqueadas, rechaza cambios y restaura los valores del jugador al salir |
| Partidas guardadas | `packages/session`, `packages/game/save` | Escribe el modo en la sección `mode` y en el resumen del formato 4; migra archivos antiguos. `save` no conoce nombres de variantes |
| Cooperativo | `apps/game` (`coop_*`, `online_rooms`) | Compara la partida del invitado con el modo del host y decide qué progreso puede volver a su partida |
| Sistemas | `packages/game/*` (campo, cuidados, combate, evolución, historia, tienda...) y `packages/engine/*` | Lean su ruleset y sus opciones como siempre; nunca preguntan qué variante se juega |

El contenido se organiza por lo que es, no por variante:

```text
content/packs/game/
├─ modes/classic/mode.yaml      # solo original, cinco opciones fijadas
├─ modes/folder/mode.yaml       # seis rulesets, sin fijaciones
├─ rules/original/...           # reglas compartidas, con las desviaciones aprobadas
├─ rules/oculto/...             # contenido oculto del Modo Folder
├─ rules/oculto_mega/...        # paso a Mega del Modo Folder
├─ rules/todas*/...             # rulesets de todas las especies del Modo Folder
└─ save/settings.yaml           # ranuras compartidas; el autoguardado es regla del ruleset
```

El pack base que extrae `dwx` y el resto de los recursos del juego son compartidos. Un pack más alto en la
[pila](vfs.md) puede sustituir un manifiesto de modo o añadir un ruleset; como la pila compone las tablas entrada por
entrada, dos variantes no necesitan dos copias de una tabla. Un recurso exclusivo es el que solo referencia un ruleset
del Modo Folder, no una carpeta duplicada.

## Compilación y distribución

Hay un binario, un conjunto de packs y una descarga. La variante es un dato dentro del pack del juego y un id dentro de
cada partida, no un objetivo de compilación, un ajuste del launcher ni una elección al instalar
([ADR 0026](../adr/0026-game-variants-as-modes.md)).

- **Compilación:** los mismos objetivos y presets de CMake generan el juego para todas las plataformas. No hay flag de
  compilación, símbolo de preprocesador ni objetivo por variante: añadir un modo no recompila los sistemas.
- **Contenido:** los packs `base`, `game`, de remasterización y de traducción se montan para ambas variantes. Los
  rulesets extra del Modo Folder son datos adicionales, no una segunda copia del pack base. Un mod lleva su
  `modes/<id>/mode.yaml` y sus rulesets en su propio pack.
- **Instalación y actualización:** el [launcher](../modules/apps/launcher.md#publicar-una-versión) publica un artefacto
  por cada fusión en `main` (Testing) y promueve el probado a Stable. Ambas variantes se actualizan juntas y no tienen
  números de versión independientes. La `version` y las `dependencies` del pack, junto con `dwx validate`, mantienen
  coordinados el ruleset de un mod y el manifiesto que lo usa.
- **Identidad visual:** la lista de partida nueva nombra y describe cada variante, la lista de ranuras muestra la
  variante del resumen de la partida, la página de opciones marca como fijas las filas bloqueadas por la variante
  activa, y los mods y skins leen `options.variant()`. No hay un icono ni un ejecutable distinto.
- **Distribución independiente:** hoy no hace falta y deliberadamente no se construye. Si una variante necesitara su
  propio ritmo de publicación, la costura es su manifiesto de modo y los packs que nombra: se publican aparte y se
  montan sobre el mismo binario. No se copia ni se bifurca código del juego.

## Sistemas que necesitan refactorización

La primera versión cumplió el diseño sin cambiar los sistemas. Los límites siguientes son los que notaría una tercera
variante o una expansión; están ordenados por dependencia.

| # | Sistema actual | Límite | Refactorización |
|---|---|---|---|
| 1 | `FixableOptions` y `ModeOptions::withPins` (`apps/game/src/mode_options.hpp`) | Las opciones que un modo puede fijar son un array de C++ y sus valores se comprueban con una ida y vuelta por YAML en el lector de la configuración; añadir una opción fijable requiere código | Dar a cada ajuste sus propios metadatos de fijabilidad y dominio de valores (esquema o declaración) y validar las fijaciones una sola vez contra ellos. El manifiesto del modo sigue siendo la única descripción de una variante |
| 2 | Manifiesto de pack (`content/schemas/pack.schema.json`) | Un pack no puede declarar en qué modos se juega, así que una expansión del Modo Folder se monta también en una partida del Clásico Mejorado | Añadir una lista opcional de modos compatibles a `pack.yaml`, resolverla con las `dependencies` del pack y ocultar un pack cuyos modos no incluyan la variante de la partida |
| 3 | Directorio de salas y sala del cooperativo (`apps/game/src/online_rooms.cpp`) | La variante del host solo se conoce por la partida después de unirse, así que la lista de salas no puede mostrarla | Llevar el id del modo en la entrada del directorio y en la petición de unión, y comprobar la compatibilidad antes de entrar. Es un cambio de protocolo ([cooperativo](multiplayer.md)) |
| 4 | `currentModeId` y `ModeNotMounted` (`packages/game/modes`, `apps/game/src/play_session.cpp`) | Un modo ausente, heredado o que ya no está montado pasa silenciosamente a Modo Folder sin opciones fijadas: correcto para partidas antiguas, indistinguible de una errata o de un mod retirado | Registrar el motivo de la vuelta atrás y ofrecer en la lista de ranuras la conversión en un sentido Clásico Mejorado → Modo Folder; la carga sigue siendo permisiva |
| 5 | `ShownModes` (`apps/game/src/new_game_mode.hpp`) y la línea de comandos | La lista muestra solo los seis primeros modos y no hay opción `--mode`: suficiente para dos, no para un catálogo de variantes | Paginar la lista y añadir `--mode` para rutas y pruebas, manteniendo el Modo Folder por defecto cuando se salta el título |
| 6 | Opciones de gráficos e interfaz (`apps/game/src/graphics_options.hpp` y los skins) | Se aplican fuera de la configuración de una partida, así que un modo no puede fijarlas | Poner sus valores bajo los mismos metadatos que las opciones del juego y permitir fijaciones cuando el dominio de valores del manifiesto las cubra |
| 7 | Manifiesto de modo (`GameMode`) y montaje de la sesión | Un modo elige rulesets y fijaciones, no qué sistemas se ejecutan: una arena sigue montando el campo y los cuidados de la historia (#118) | Ampliar el manifiesto con los sistemas que activa una partida y hacer que la sesión monte ese conjunto. La clave nueva es opcional para que los modos actuales no cambien |
| 8 | Pantalla de reglas de partida nueva (`ui/new_game_rules`) | Todavía pide activar contenido oculto después de elegir el modo, así que el selector de modo aún no reemplaza ese paso | Integrar la activación de contenido oculto en la elección de modo; dejar como mucho la elección de ruleset del Modo Folder y escribir el modo elegido en el primer guardado |

## Migración desde la arquitectura anterior

Antes de las variantes había un modo implícito (`original`): el título iba directo a la pantalla de reglas, las opciones
eran globales para el jugador y la partida solo llevaba `ruleset`. La migración se hizo por capas, para que las partidas
antiguas sigan jugando.

| Paso | Antes | Ahora | Regla de compatibilidad |
|---|---|---|---|
| Identificar la variante | Sin id en la partida; `original` era el único modo | La sesión escribe el id del modo en su sección `mode`, que la partida ya tenía | Un id vacío o `original` se carga como Modo Folder (`currentModeId`) y conserva los rulesets y las opciones de la partida |
| Nombrarla en la lista de ranuras | El resumen no tenía variante | El formato 4 añade `summary.mode` | 3 → 4 lo deja vacío; la lista lee vacío como Modo Folder y el siguiente guardado lo rellena |
| Conservar la configuración del jugador | Las opciones cambiaban de forma global | `ModeOptions` superpone las fijaciones durante una partida y nunca escribe el archivo de configuración | Los valores del jugador vuelven al terminar la partida; una partida nueva del Modo Folder los ve |
| Separar reglas de comodidad | Una página de opciones mezclada | El manifiesto solo fija opciones de balance, progresión y contenido; los arreglos de bugs y la comodidad siguen en ambas | Toda función sigue accesible en al menos una variante |
| Reemplazar la activación de contenido oculto | Una partida nueva preguntaba si el contenido oculto estaba activado | El selector de modo de la partida nueva define el tipo de guardado; el contenido oculto viene de los rulesets del Modo Folder | La pantalla de reglas heredada se mantiene solo hasta la tarea 10, nunca como forma de cambiar el tipo de una partida existente |
| Añadir variantes | Una partida nueva significaba una build o una rama nueva | Un `mode.yaml` nuevo y sus dos claves de localización | No se duplica código: el catálogo, la partida guardada, la lista de ranuras y la página de opciones siguen los datos |

Las cadenas de migración solo van hacia delante: el juego actual lee partidas antiguas, pero un juego antiguo no lee una
partida de formato 4 ni una sección que no conoce ([partidas guardadas](save.md#versiones-y-migraciones)). Un jugador que
vuelve a una build antigua necesita la versión nueva, no una partida degradada.

## Plan de tareas

El trabajo está dividido para que cada paso se pueda revisar y publicar por separado. Los cuatro primeros están en el
código actual; el resto son los pendientes, enlazados con las refactorizaciones anteriores.

| # | Tarea | Entregable | Estado |
|---|---|---|---|
| 1 | Definir las dos experiencias y su frontera | Esta página, la matriz de funcionalidades y el [ADR 0026](../adr/0026-game-variants-as-modes.md) | Hecho |
| 2 | Infraestructura compartida de modos | `mode.schema.json` con `options`, validación del catálogo, lista de modos de partida nueva, id de modo por partida | Hecho |
| 3 | Preajuste del Clásico Mejorado y opciones fijas | `modes/classic/mode.yaml`, `ModeOptions`, filas bloqueadas en la página de opciones, rutas con `--ruleset` | Hecho |
| 4 | Partidas guardadas y cooperativo seguros | Resumen del formato 4, mapeo de partidas antiguas, etiqueta en la ranura, variante del host en el cooperativo, regla de progreso en un sentido | Hecho |
| 5 | Robustecer las fijaciones (refactorización 1) | Metadatos de opciones, validación en un solo lugar, un mod puede fijar una opción nueva sin cambiar código | Pendiente |
| 6 | Packs conscientes del modo (refactorización 2) | Lista de compatibilidad en `pack.yaml` y validación al montar; las expansiones no se filtran al Clásico Mejorado | Pendiente |
| 7 | Anunciar la variante antes de unirse (refactorización 3) | Id del modo en el directorio de salas y en la petición de unión, con pruebas del protocolo | Pendiente |
| 8 | Conversión de ranuras y diagnóstico de partidas (refactorización 4) | Acción Clásico Mejorado → Modo Folder que copia la partida, con el motivo de la vuelta atrás en el log | Pendiente |
| 9 | Escalar la lista y las herramientas (refactorizaciones 5–7) | Paginación, `--mode`, activación de sistemas en el manifiesto, fijaciones de opciones de gráficos e interfaz | Pendiente |
| 10 | El selector de modo define el tipo de guardado (refactorización 8) | La elección de modo de la partida nueva reemplaza la activación de contenido oculto de `ui/new_game_rules`; el modo elegido se escribe en el primer guardado y ningún paso posterior vuelve a pedir contenido oculto | Pendiente |

La tarea 5 desbloquea a los mods que añaden variantes; las tareas 6 y 7 son independientes entre sí; la tarea 8 puede
publicarse cuando estén acordadas las reglas de conversión. La tarea 10 cierra la distancia entre la pantalla de reglas
actual y el flujo de partida nueva que exige esta página. Cada tarea mantiene las dos reglas de este diseño: los
sistemas siguen siendo ajenos a la variante y una partida siempre juega la variante con la que empezó.
