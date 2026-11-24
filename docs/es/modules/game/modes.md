# modes

Los modos de juego (`dwe::game::modes`, capa `game`): qué partidas puede empezar el jugador. Un modo junta una forma de
jugar con los rulesets que admite y, opcionalmente, las opciones que fijan sus partidas; el juego trae dos, las
[variantes de juego](../../architecture/game-variants.md) **Clásico Mejorado** y **Modo Folder**, y un mod añade los
suyos con un manifiesto. Depende de `core` y `assets`; no dibuja nada.

El original no tiene modos: su partida nueva es siempre la misma historia. Los modos existen para que el jugador elija
las reglas del original o el juego ampliado, y para que un mod pueda traer, por ejemplo, una arena de combates sin la
historia (tarjeta #111). Al crear la partida nueva, el selector de modo reemplaza la antigua activación de contenido
oculto: el modo elegido define el tipo de guardado y se guarda en la partida nueva.

## El manifiesto

Cada modo es una carpeta `modes/<id>/` de un paquete con un `mode.yaml` (esquema `content/schemas/mode.schema.json`,
que `dwx validate` comprueba):

```yaml
id: classic                           # el nombre de la carpeta
name: modes.classic_name              # clave de localización del nombre
description: modes.classic_description
ruleset: original                     # el ruleset con el que empieza
rulesets: [original]                  # los que admite; incluye ruleset
order: 0                              # posición en la lista (luego el id)
options:                              # opcional: nombre de opción -> el valor que fijan sus partidas
  difficulty: normal
  rare_items: false
```

Todas las claves menos `options` y `randomizer` son obligatorias y no admite otras. `randomizer: true` deja que las
partidas nuevas del modo sean [partidas aleatorizadas](randomizer.md) (`GameMode::randomizer`, apagado por defecto); el
juego lo enciende solo en el Modo Folder. `options` se lee en `GameMode::options`
(`OptionPin`: opción y valor como texto); el juego la aplica (`apps/game/src/mode_options.hpp`). Un paquete de encima reemplaza un modo dando la misma carpeta,
como cualquier asset.

## Uso

```cpp
auto catalog = dwe::game::modes::ModeCatalog::load(files);   // los modos de los paquetes montados
if (catalog.asksMode()) { /* más de uno: la partida nueva pregunta */ }
const dwe::game::modes::GameMode* mode = catalog.defaultMode();   // folder, o el primero
```

- **Catálogo**: `ModeCatalog::load` lee cada `modes/<carpeta>/mode`, comprueba que cada ruleset del modo existe
  (`rules/<id>/ruleset`) y deja fuera, con su motivo en el log, el modo que no se lee: un mod roto no impide
  arrancar. Ordena por `order` y luego por id.
- **Partida nueva**: la pantalla de título (`apps/game/src/title_screen.cpp`, `NewGameModes`) muestra la lista
  `ui/new_game_mode` solo si hay más de un modo; con uno (un mod que sustituye ambos), no pregunta nada. Este es el paso
  que reemplaza la antigua pregunta «activar contenido oculto»: elegir el modo al crear la partida define su tipo y
  escribe `PlayStart::modeId`. La pantalla de reglas (`ui/new_game_rules`) sigue saliendo para un modo que ofrece
  `original` y `oculto`; en el flujo previsto solo elige un ruleset del Modo Folder y no vuelve a pedir contenido oculto
  ([tarea 10](../../architecture/game-variants.md#plan-de-tareas)). Otro modo empieza con su `ruleset`. Las rutas de
  prueba que no preguntan reglas tampoco preguntan modo: juegan el Modo Folder (`DefaultMode`) con el `--ruleset` de la
  línea de órdenes. Tras el modo y sus reglas, un modo con `randomizer: true` muestra la
  [configuración del aleatorizador](randomizer.md) (`ui/new_game_randomizer`).
- **Guardado**: la sesión guarda el modo en la sección `mode` (`id`) y en el resumen de la cabecera (formato 4); una
  partida guardada antes de los modos no la tiene, y una guardada antes de las variantes tiene `original`: ambas se
  cargan como `folder` (`currentModeId`, `session::ModeSave`). `GameSession::mode()` lo devuelve.

## Pendiente

- Un modo todavía no elige qué sistemas corren (una arena sin campo, por ejemplo): la sesión monta siempre los de la
  historia. Espera al diseño del modo arena (#118).
- La lista muestra los seis primeros modos; no pagina.
- No hay opción de línea de órdenes para elegir modo (`--mode`).
- El `ui/new_game_rules` heredado todavía pide activar contenido oculto después de elegir el modo; la tarea 10 lo integra
  en el selector de modo de la partida nueva.
- El resto del trabajo está en el [plan de tareas de variantes](../../architecture/game-variants.md#plan-de-tareas).
