# modes

Los modos de juego (`dwe::game::modes`, capa `game`): qué partidas puede empezar el jugador. Un modo junta una forma de
jugar con el ruleset con el que empieza; el juego trae uno solo, **Historia original**, y un mod añade los suyos con
un manifiesto. Depende de `core` y `assets`; no dibuja nada.

El original no tiene modos: su partida nueva es siempre la misma historia. Es una base de la fase 5
([plan](../../plan/index.md)); la tarjeta #111 la pide para que un mod pueda traer, por ejemplo, una arena
de combates sin la historia.

## El manifiesto

Cada modo es una carpeta `modes/<id>/` de un paquete con un `mode.yaml` (esquema `content/schemas/mode.schema.json`,
que `dwx validate` comprueba):

```yaml
id: original                          # el nombre de la carpeta
name: modes.original_name             # clave de localización del nombre
description: modes.original_description
ruleset: original                     # el ruleset con el que empieza
rulesets: [original, oculto, oculto_mega]   # los que el modo admite; incluye ruleset
order: 0                              # posición en la lista (luego el id)
```

Todas las claves son obligatorias y no admite otras. Un paquete de encima reemplaza un modo dando la misma carpeta,
como cualquier asset.

## Uso

```cpp
auto catalog = dwe::game::modes::ModeCatalog::load(files);   // los modos de los paquetes montados
if (catalog.asksMode()) { /* más de uno: la partida nueva pregunta */ }
const dwe::game::modes::GameMode* mode = catalog.defaultMode();   // original, o el primero
```

- **Catálogo**: `ModeCatalog::load` lee cada `modes/<carpeta>/mode`, comprueba que cada ruleset del modo existe
  (`rules/<id>/ruleset`) y deja fuera, con su motivo en el log, el modo que no se lee: un mod roto no impide
  arrancar. Ordena por `order` y luego por id.
- **Partida nueva**: la pantalla de título (`apps/game/src/title_screen.cpp`, `NewGameModes`) muestra la lista
  `ui/new_game_mode` solo si hay más de un modo; con uno, que es lo que trae el juego, no cambia nada. Después,
  la pantalla de reglas (`ui/new_game_rules`) sale solo para un modo que ofrece las reglas del original y su
  contenido oculto (`original` y `oculto` en `rulesets`); otro modo empieza con su `ruleset`. Las rutas de prueba
  que no preguntan reglas tampoco preguntan modo: juegan la historia original con el `--ruleset` de la línea de
  órdenes.
- **Guardado**: la sesión guarda el modo en la sección `mode` (`id`); una partida guardada antes de los modos no la
  tiene y se carga como `original` (`session::ModeSave`). `GameSession::mode()` lo devuelve.

## Pendiente

- Un modo todavía no elige qué sistemas corren (una arena sin campo, por ejemplo): la sesión monta siempre los de la
  historia. Espera al diseño del modo arena (#118).
- La lista muestra los seis primeros modos; no pagina.
- No hay opción de línea de órdenes para elegir modo (`--mode`).
