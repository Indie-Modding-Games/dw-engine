# Localización

Todo texto que ve el jugador sale de tablas de idioma de los paquetes, nunca del código. El juego trae inglés y
español; un paquete puede añadir más idiomas o cambiar textos sueltos. El idioma se elige en el menú principal y
cambia sin reiniciar (R3.9).

## Dónde vive

`engine/localization`, en la capa de **sistemas**, junto a `assets`, del que depende (y de `core`). La lectura y la
comprobación de las tablas (`dwe/assets/text_table.hpp`) están en `assets`.

- Tiene que estar por debajo de `ui` y `script` (servicios), que la usan, y al alcance de `game/*`, que podrá pedir
  textos sin conocer la UI. Por eso no puede ser un servicio.
- Solo necesita leer ficheros del VFS: no pinta ni toca la plataforma. Encaja con los otros sistemas que leen datos de
  los paquetes.
- Las tablas y su comprobación van en `assets` porque `dwx validate` solo puede usar `formats`, `assets` y `core`
  ([reglas de dependencias](dependency-rules.md)). El catálogo (idioma actual, respaldo, marcadores) es del runtime
  y queda fuera de `dwx`.
- El idioma del sistema lo lee `platform` (`platform::systemLanguage()`, con SDL) y lo usa la app. `game/` no lo ve.

## Dos fuentes de texto

| Carpeta | Qué tiene | Quién la escribe |
|---|---|---|
| `localization/<idioma>/<área>.yaml` | Los textos del motor: menús, HUD, avisos de botones, mensajes de guardado | El equipo (`content/packs/game`), en `en` y `es` |
| `text/<idioma>/<fuente>.yaml` | El texto del juego original: nombres de mapas, técnicas, objetos, mensajes | `dwx extract` el inglés (paquete base); las traducciones van en un paquete de traducción |

Las dos usan el mismo formato: un mapping plano de clave a texto, sin metadatos.

```yaml
# localization/es/pause_menu.yaml
title.main: "Pausa"
ui_scale.value: "{percent} %"
```

## Claves

Una clave son segmentos de `a-z`, `0-9` y `_` unidos por puntos. La clave completa lleva delante el nombre del
fichero:

| Origen | Clave completa | Ejemplo |
|---|---|---|
| Motor | `<área>.<clave>` | `pause_menu.title.main`, `prompt.key.enter`, `common.yes` |
| Juego original | `<fuente>.<bloque>.<índice de 4 cifras>` | `main.map_names.0003`, `btl.menu.0087` |

En el motor el área es el documento o la parte que usa el texto (`main_menu`, `pause_menu`, `hud`, `prompt`), y
`common` son las palabras que comparten varios. Las del original son ids estables del disco
([textos](../formats/textos.md#tablas-de-texto)): `localization::gameTextKey(fuente, bloque, índice)` y, para el
nombre de un mapa, `localization::mapNameKey(loadingName)`. Una clave no puede estar en dos tablas del mismo idioma.

## Marcadores

Un texto lleva valores entre llaves: `{percent}`, `{count}`. Quien lo pide da los valores por nombre; los números que
el original escribía con `%d` son `{0}`, `{1}`... y se dan por su número. Los números se escriben sin decimales si
son enteros (`3`, no `3.0`). `{{` y `}}` son llaves literales; una `{` sin cerrar es un error de la tabla.

Los códigos de control del original son etiquetas legibles en todos los idiomas, también en el inglés extraído
([lista completa](../formats/textos.md#etiquetas)). El texto que da `text`, `tr`, `t()` y `text-key` es texto plano
para la UI: `{br}` y `{wait}` son un salto de línea, `{color:N}` desaparece y las demás etiquetas
(`{digimon:pstat=3}`) se quedan escritas, para que se vea lo que falta. Quien dibuja el texto con colores y esperas
(la caja de diálogo) lee el texto tal cual con `Catalog::find`.

Cada idioma tiene que tener en cada clave el mismo conjunto de marcadores que el inglés. Así el orden de las palabras
puede cambiar entre idiomas sin tocar el código.

## Cómo se pide un texto

Hay una sola búsqueda, por clave, igual en todas partes:

| Desde | Cómo |
|---|---|
| C++ | `localization::tr("pause_menu.title.main")` o `catalog.text(clave, argumentos)`, con `TextArgument{.name = "percent", .value = "150"}` |
| RML | `<span text-key="main_menu.play"></span>`: la UI pone el texto al cargar el documento y otra vez al cambiar de idioma |
| Lua de la UI | `t("pause_menu.ui_scale.value", {percent = 150})`; `ui.languages()` da los idiomas que hay |
| Lua del juego (`ScriptVm`) | `t(clave[, argumentos])` y `texts.language()`, tras `bindTexts(catalog)` |
| Minimapa | `ui.setMinimap(rejilla, claveDelNombre)`: la UI busca el nombre y lo vuelve a buscar al cambiar de idioma |
| Nombre del mapa en el cambio de mapa | La app publica la clave en el ajuste `map_title_key` (de `FieldScene::nameKeyOf`, el único sitio que busca el nombre de un mapa) y el HUD la escribe con `t()`, también en su `on_language` |

Una clave que no tiene el idioma elegido sale en inglés. Al cargar, el log avisa de cada tabla que falta y de las
claves que faltan, con un máximo de cinco claves por aviso. Una clave que no tiene ni el inglés sale como la propia
clave (`menu.nothing`), para que se vea en pantalla. Los mensajes de log para desarrolladores no se traducen: son
constantes en inglés en el `messages.hpp` de cada módulo.

## Elegir el idioma

1. `--lang en|es` en la línea de órdenes. Es solo para autotests: no se guarda.
2. El idioma guardado en los ajustes del usuario (`settings.yaml` en la carpeta de datos del usuario, la de
   `SDL_GetPrefPath`), si los paquetes todavía lo tienen.
3. El idioma del sistema (`SDL_GetPreferredLocales`), si los paquetes lo tienen. Si no, inglés.

El jugador lo cambia en el **menú principal**, que sale al arrancar el juego: Jugar, Idioma y Salir. Se usa con
ratón, teclado y mando, como el de pausa. Izquierda y derecha, o confirmar, pasan al idioma siguiente. Esa elección
cambia todo el juego, también el texto del original cuando hay un paquete de traducción, y se guarda en los ajustes.
Un autotest no lee ni escribe los ajustes del usuario.

El menú envía el comando `language` con el idioma. La app (`LanguageChanger`) hace esto:

1. Carga el catálogo del nuevo idioma. Si falla, el juego sigue en el idioma anterior.
2. Publica el ajuste `language`.
3. Llama a `Ui::refreshTexts()`: vuelve a poner cada `text-key` y el nombre del mapa, y llama a
   `on_language(document)` en los documentos visibles para que su Lua reescriba lo que puso con `t()`.
4. Guarda los ajustes.

Un documento oculto se actualiza al mostrarse, en su `on_show`.

## Paquetes, idiomas nuevos y mods

- **Idioma nuevo:** un paquete con `localization/<idioma>/` y todas las áreas del inglés aparece en el selector del
  menú principal. `localization/<idioma>/language.yaml` tiene `name`, el nombre del idioma en ese idioma.
  `text/<idioma>/` traduce el texto del original y puede ser parcial: lo que falta sale en inglés.
- **Cambiar un texto:** un paquete más alto en la pila con el mismo fichero y solo las claves que cambia. Las tablas
  se mezclan clave a clave de abajo arriba (`assets::readTextTable`), al contrario que el resto de los assets, donde
  gana el fichero entero.
- **Traducción del original:** el español del texto del juego va en `content/packs/es-translation`
  (`text/es/<fuente>.yaml`). Los nombres de Digimon y de técnicas se quedan en inglés y no se repiten en esos ficheros:
  el lock de la traducción (`text/es/.sources.lock`, ver [textos](../formats/textos.md#lock-de-una-traducción)) los
  lista y guarda el SHA-1 del inglés de cada cadena traducida. Cómo traducir: [traducir](../guides/traducir.md).

## Comprobaciones

`dwx validate` compara cada idioma con el inglés (`assets::checkTranslations`, además de `checkContent`):

- Cada tabla de `localization/en` tiene que estar en cada idioma que tenga tablas del motor, con las mismas claves.
- Una clave de más, o una clave con otros marcadores, es un error en las dos fuentes. En `text/` puede faltar una
  clave.
- Una tabla sin su tabla en inglés es un error, y también una clave repetida en dos tablas del mismo idioma.
- El lock de una traducción del original: cadenas cuyo inglés cambió desde que se tradujeron, cadenas sin SHA-1 y
  claves que el inglés no tiene.

Los tests: `dwe_assets_tests` (`text_table_tests`) para el formato, los marcadores y la comprobación;
`dwe_localization_tests` para la carga, el respaldo, la mezcla de paquetes y el cambio en caliente;
`dwe_ui_tests` y `dwe_script_tests` para `text-key`, `t()`, `on_language` y el menú principal. La ruta
`tests/autotest/route-language.yaml` corre una vez por idioma (ver [prueba vertical](../guides/prueba-vertical.md)).

## Añadir un texto

1. Escoge el área (el documento que lo muestra, o `common`) y una clave: `pause_menu.save_done`.
2. Añádela a `content/packs/game/localization/en/<área>.yaml` y a `.../es/<área>.yaml`, con los mismos marcadores.
3. Úsala: `text-key="pause_menu.save_done"` en el RML, `t("pause_menu.save_done")` en Lua o
   `localization::tr("pause_menu.save_done")` en C++.
4. `dwx validate content/packs/game --with <base>` comprueba que los dos idiomas cuadran.

## Nota legal

Los textos del motor (`localization/`) son del equipo y van en git. El texto del original no: el inglés lo extrae
`dwx` del disco del usuario y nunca entra en el repositorio, y el lock de la traducción solo guarda su SHA-1. Lo que
no tiene traducción sale en inglés: hoy, todo el texto del original hasta que llegue el paquete de español, y siempre
los nombres de Digimon y de técnicas. La traducción del original al español es un derivado
([assets](../legal/assets.md)).

## Fuentes

La fuente de la UI (Noto Sans) cubre el español: `ñ`, vocales con tilde, `ü`, `¿` y `¡`. Un idioma con otro alfabeto
necesita una fuente que lo cubra en su paquete.
