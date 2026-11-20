# Variantes de juego: Clásico Mejorado y Modo Folder

En cada partida nueva el jugador elige entre dos experiencias que comparten un motor y un mismo conjunto de packs:

- **Clásico Mejorado**: el juego original tal como era (su historia, reglas, balance y progresión) con las mejoras
  técnicas del motor, las opciones de comodidad y los arreglos de bugs.
- **Modo Folder**: el juego ampliado: contenido oculto, todas las especies como compañero, el paso de Ultimate a Mega,
  los objetos raros y todas las opciones, incluidas las que cambian las reglas.

La decisión y las alternativas están en el [ADR 0026](../adr/0026-game-variants-as-modes.md). Esta página dice qué
activa cada variante, cómo se elige y se guarda, y cómo encajan el cooperativo, los mods y las variantes nuevas.

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

## Qué tiene cada una

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

Los extras que no son opciones (contenido oculto, paso a Mega, todas las especies como compañero) son rulesets, así que
el Clásico Mejorado los deja fuera simplemente al no admitir esos rulesets, y su partida nueva se salta la pantalla de
reglas.

## Elegir una variante

La partida nueva pregunta primero el modo, con la lista que ya dibuja cada skin (`ui/new_game_mode`): primero Clásico
Mejorado, luego Modo Folder, cada uno con una línea que dice qué es. Clásico Mejorado empieza en el acto con las reglas
del original; el Modo Folder sigue a la pantalla de reglas como siempre hizo la partida nueva, con todas las especies
como compañero activadas por defecto. Atrás, desde cualquiera de las dos listas, vuelve a la lista de ranuras.

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

## Pendiente

- Convertir una partida del Clásico Mejorado en una del Modo Folder (el Modo Folder admite todo lo que admite el Clásico
  Mejorado, así que la conversión es segura en un sentido). Necesita una opción en la lista de ranuras y un guardado que
  reescriba su sección `mode`.
- Que la lista de salas y la sala del cooperativo nombren la variante del host antes de unirse (un cambio de protocolo).
- Que las expansiones que monta el launcher (la expansión de historia planeada) declaren los modos en los que se juegan,
  para que una partida del Clásico Mejorado nunca las vea.
- Fijar opciones de gráficos o de interfaz desde un modo, que hoy el juego aplica fuera de la configuración de una
  partida.
