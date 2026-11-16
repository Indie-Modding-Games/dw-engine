# Modding

Un mod es un **paquete**: una carpeta con un `pack.yaml` y sus ficheros, que el juego monta encima de los suyos. Con
un paquete se puede sustituir cualquier asset (un menú, una textura, un texto), añadir entradas a las tablas (una
especie, un mapa, un objeto), cambiar las reglas con un ruleset, añadir un modo de juego o un aspecto de la interfaz y
reaccionar a lo que pasa en la partida con scripts de Lua. Nada de esto toca el motor: el juego arranca aunque un mod
esté roto, y lo deja fuera con el motivo en el log.

El juego no trae nada del disco original: cada jugador extrae el suyo ([jugar](../getting-started/play.md)). Un mod
tampoco debe repartir datos, imágenes ni textos del original; trae solo lo suyo
([política de assets](../adr/0011-asset-legal-policy.md)).

## Páginas

| Página | Qué cuenta |
|---|---|
| [Tu primer mod](first-mod.md) | Paso a paso: un paquete con un script que reacciona a la partida |
| [Paquetes e ids](packs.md) | El `pack.yaml`, las capas, los ids de asset y de contenido, sustituir y añadir |
| [API de Lua](lua-api.md) | El script de entrada, el entorno de un mod, los eventos del juego y las funciones publicadas |
| [Modos de juego](modes.md) | Añadir un modo con su `mode.yaml` |
| [Aspectos](skins.md) | Cambiar el estilo de la interfaz con un aspecto propio |
| [Interfaces](ui-addons.md) | Instalar o hacer una interfaz de la comunidad que cambia las pantallas del juego |
| [API de la interfaz](ui-api.md) | Lo que cada pantalla lee del juego y le manda: ajustes, comandos y opciones |
| [API de historia](story.md) | Los scripts de eventos, sus parches por sección y la capacidad de disparadores y variables |
| [Editar un mapa](maps.md) y [añadir uno](new-maps.md) | El mapper |

## Lo que hay que saber antes

- **Versión de la API.** Un mod declara en su `pack.yaml` la versión de la API de Lua para la que está escrito
  (`api: "1.x"` o `api: "1.4"`). Es obligatoria en las capas `mods` y `user`: un mod con scripts que no la
  declara se deja fuera (uno solo de datos todavía se monta, con un aviso).
- **Ids.** Todo se nombra por id, nunca por número: los assets por su ruta (`ui/hud/document`) y lo que añade un
  paquete por `<id del paquete>:<nombre>` ([paquetes e ids](packs.md)).
- **Partidas.** Una partida guarda su ruleset y su modo. Si se carga sin el mod que añadió algo (disparadores,
  variables, especies), lo que ya no cabe se descarta con un aviso en el log y la partida sigue.
- **Presupuestos.** Cada llamada a Lua tiene un presupuesto de instrucciones y de tiempo; un script desbocado se para
  sin colgar el juego ([script](../modules/engine/script.md#presupuestos-de-un-script-para-quien-hace-mods)).
- **Comprobar.** `dwx validate` comprueba un paquete (esquemas de sus YAML, API, capa) sin arrancar el juego
  ([dwx validate](../architecture/assets-pipeline.md#dwx-validate)).
