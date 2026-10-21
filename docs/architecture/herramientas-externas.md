# Compatibilidad con las herramientas del equipo

Hay herramientas de la organización que leen lo que escribe `dwx extract` y escriben paquetes que carga el motor. Lo
que esas herramientas usan es un **contrato**: si el motor lo cambia sin avisar, las herramientas se rompen. Esta
página dice qué es contrato, quién depende de cada parte y cómo se cambia.

## Herramientas

| Herramienta | Repositorio | Qué lee del motor | Qué escribe |
|---|---|---|---|
| Digimon Animation Studio (complemento de Blender) | [3D-Moddeler-Base](https://github.com/Indie-Modding-Games/3D-Moddeler-Base) | `digimon/<slug>/model.glb` del paquete base | Un paquete de la capa `mods` con `digimon/<slug>/model.glb`, con las animaciones nuevas como `anim_NN` |
| DW1 Field Editor (`dw1fe`) | [Digimon-World-Map-Editor](https://github.com/Indie-Modding-Games/Digimon-World-Map-Editor) | `map/<id>/data.yaml`, los fondos y las texturas `map/<id>/texture8/NNN.png` y `texture4/NNN.png` del paquete base (lo usa `engine-check` para compararse); `data/maps.yaml`, `data/digimon.yaml`, `data/skeletons.yaml` y `data/items.yaml`; los modelos de puerta `model/door/doorNN.glb`; `digimon/<slug>/texture.png` y `texture_meta.yaml`; los scripts `script/dg/NNN.yaml` y `script/maphead.yaml`, que su panel Script edita como texto; `texture/etcdat/item.png` y su `_meta.yaml` | Un paquete de la capa `mods` con `map/<id>/data.yaml`, `map/<id>/background_*.png`, `data/maps.yaml` entero y los scripts editados `script/dg/NNN.yaml` y `script/maphead.yaml`, todos en el YAML del motor |

Las dos herramientas se verifican contra la salida de `dwx extract`:

- El Studio comprueba que sus 4843 animaciones coinciden con las del motor.
- El editor comprueba que sus 241 `data.yaml` son idénticos, byte a byte, a los del paquete base.

Un paquete que deja una herramienta se comprueba junto con el paquete base, que es del que depende:
`dwx validate --with <content>/base <paquete>`. La opción `--with` monta también los paquetes de esa carpeta para
resolver las dependencias. Además de los esquemas, las referencias y los contratos, `dwx validate` carga cada asset
con los mismos lectores que usa el juego, así que un paquete que pasa la validación es un paquete que el juego acepta:

- Cada modelo `.glb` con `parseModel` (`engine/assets/src/model_data.cpp`): nodos, una sola piel, articulaciones y
  pesos de cada vértice (que suman 1 y nombran articulaciones de la piel), imágenes PNG incrustadas y los `extras` de
  cada clip (`readClipExtras`: `ticksPerSecond`, `tickCount`, `loops`, `rootMotionNode`…).
- Cada `map/<id>/data.yaml` con `parseMapDocument` (`engine/assets/src/map_document.cpp`).
- `data/maps.yaml` con `parseMapTable` (`engine/assets/src/map_table.cpp`).
- `data/items.yaml`, `data/item_effects.yaml`, `data/shops.yaml` y `data/new_game_stats.yaml` con sus lectores
  (`parseItemTable`, `parseItemEffectTable`, `parseShopTable` y `parseNewGameStats`, en `engine/assets/src/`).
- Los textos, `localization/<idioma>/*.yaml` y `text/<idioma>/*.yaml`, con `parseTextTable`
  (`engine/assets/src/text_table.cpp`).

Los dos lectores de YAML son estrictos: una clave que sobra o que falta hace fallar el documento aunque cumpla su
esquema. Cada fallo sale como un problema con el fichero, el ID del asset y el motivo
(`<fichero>: the engine cannot load <id>: <motivo>`), y `dwx validate` termina con error.

## Qué es contrato

| Parte | Fuente de verdad | Quién depende | Detalle |
|---|---|---|---|
| Manifiesto `pack.yaml` | `content/schemas/pack.schema.json`, [VFS](vfs.md) | Las dos | `id`, `version`, `layer: mods` y `dependencies` con `base` y `min_version` |
| Reglas de ID de asset | [VFS](vfs.md#ids-de-asset) | Las dos | El ID es la ruta sin extensión; un fichero que no da un ID válido hace fallar el paquete |
| Capas y prioridad | [VFS](vfs.md) | Las dos | Un paquete `mods` gana a `base` por ID |
| IDs de los Digimon | `apps/dwx/src/asset_names.cpp` (`digimonSlug`) | Studio | `AGUM` → `digimon/agum/...` |
| IDs de los mapas | `apps/dwx/src/asset_names.cpp`, a partir de `MAP_ENTRIES` | Editor | La carpeta del mapa es el primer registro de `MAP_ENTRIES` que nombra el fichero |
| Modelo `.glb` | `formats/src/model/gltf.cpp` (`writeModelGlb`), [modelos](../formats/modelos.md) | Studio | Nodos `bone_NN`, una piel rígida, ejes `(x, -y, -z)`, escala 4096 = 1 |
| Animaciones | `formats/src/model/animation.cpp`, [modelos](../formats/modelos.md#animaciones) | Studio | `anim_NN` con NN el id, un fotograma por tick a 20 por segundo, movimiento raíz en el hueso 0 y `extras` (`id`, `frameCount`, `hasScale`, `ticksPerSecond`, `tickCount`, `endsInLoop`, `rootMotionNode`, `loops`, `textureMoves`, `sounds`). El motor los lee en `engine/assets/src/model_data.cpp` (`parseModel`, que llama a `readClipExtras`) |
| Contrato del modelo | `content/schemas/model_contract.schema.json`, `digimon/<slug>/contract.yaml` | Studio | Lo que `dwx validate` exige a un modelo que sustituye al original |
| Tabla de mapas `data/maps.yaml` | `content/schemas/maps.schema.json`, `engine/assets/src/map_table.cpp` | Editor | El editor la lee y la escribe entera en su paquete. Usa `maps[].index`, `maps[].id`, `maps[].doors`, `maps[].toilet`, `doors[].doors[].model_id`, `doors[].doors[].position`, `doors[].doors[].rotation` y `toilets[]`. El lector del motor es estricto con las claves: una clave que sobra o que falta hace fallar la tabla |
| Tablas `data/digimon.yaml`, `data/skeletons.yaml` y `data/items.yaml` | `content/schemas/digimon.schema.json`, `skeletons.schema.json`, `items.schema.json` | Editor | El editor las lee |
| Mapa `data.yaml` | `content/schemas/map.schema.json`, `formats/src/map/map_yaml.cpp` (`mapToYaml`) | Editor | Campos, y además **el texto exacto**: el editor escribe con las mismas reglas de `emitYaml` (`engine/core/src/yaml.cpp`) |
| Fondos de mapa | [mapas](../formats/mapas.md) | Editor | `background_day.png`, `background_dusk.png`, `background_night.png` o `background_paletteN.png` |
| Texturas de mapa | `content/schemas/image.schema.json`, [extract](../formats/extract.md) | Editor | `map/<id>/texture8/NNN.png` y `map/<id>/texture4/NNN.png`, cada una con su `NNN_meta.yaml` (`mode`, `image_rect`, `clut_rect`, `palettes`, `offset`) |
| Modelos de puerta | `formats/src/model/gltf.cpp` (`materialExtras`), [extract](../formats/extract.md) | Editor | `model/door/doorNN.glb`; los `extras` de cada material dicen dónde van sus texels: `image`, `clut` y `depth` |
| Textura de Digimon | `content/schemas/image.schema.json` | Studio y editor | `digimon/<slug>/texture.png` y `digimon/<slug>/texture_meta.yaml` |
| Scripts de eventos | `content/schemas/script.schema.json`, `formats/src/script/script_yaml.cpp` (`scriptToYaml`), [scripts](../formats/scripts.md) | Editor | `script/dg/NNN.yaml` y `script/maphead.yaml`; los nombres de las operaciones, los offsets, las etiquetas, `code_end` con su relleno y la cuenta de instrucciones inalcanzables. El editor convierte cada script a texto y de vuelta, y comprueba que sale el mismo YAML |
| Texturas de objetos | [TIM](../formats/tim.md) | Editor | `texture/etcdat/item.png` y `texture/etcdat/item_meta.yaml` |

## Cómo se cambia el contrato

1. **Añadir es libre:** un campo opcional nuevo, un fichero nuevo o un ID nuevo no rompen a nadie, siempre que un
   paquete escrito antes siga siendo válido.
2. **Cambiar o quitar es un cambio de contrato.** Por ejemplo:
   - renombrar un campo, un fichero o un ID;
   - hacer obligatorio un campo;
   - cambiar los ejes, las unidades o los ticks;
   - cambiar cómo `emitYaml` da formato al texto, que rompe la comparación byte a byte del editor.

   Un cambio de contrato:
   - va en su propio commit, con la explicación en el mensaje;
   - se apunta en el registro de abajo, con la fecha y lo que tiene que cambiar cada herramienta;
   - se avisa a los responsables de las herramientas antes de publicarlo.
3. **Si se puede, se acepta lo viejo durante un tiempo.** Por ejemplo, un lector que entiende el campo viejo y el
   nuevo.
4. **Los tests del motor protegen el contrato.** La salida de `dwx extract` es determinista y está cubierta por tests.
   Un test que cambia porque cambia el contrato es la señal de que hay que seguir el paso 2.

## Registro de cambios de contrato

| Fecha | Cambio | Qué tienen que hacer las herramientas |
|---|---|---|
| 2026-10-19 | Aditivo: una carpeta de contenido también monta los archivos `.pak` que tiene directamente dentro (`findPacks`), y si hay una carpeta y un `.pak` con el mismo nombre (`base/` y `base.pak`) gana la carpeta y el archivo se deja fuera. Un paquete en carpeta se monta igual que antes | Nada. Para probar un mod empaquetado, dejar su `.pak` junto a `base/` |
| 2026-10-20 | Aditivo: la tabla lista lo que el editor de mapas ya lee y escribe (`data/maps.yaml` y las otras tablas, texturas de mapa, puertas, textura de Digimon, scripts en YAML del motor), el `extras.rootMotionNode` de las animaciones y `dwx validate --with`. El contrato no cambia | Nada |
| 2026-10-20 | Más estricto al validar: `dwx validate` carga los `.glb`, los `map/<id>/data.yaml` y `data/maps.yaml` con los lectores del juego, así que rechaza lo que el juego rechazaría. El formato no cambia | Nada, si el juego ya cargaba el paquete. Un paquete que ahora falla la validación tampoco lo cargaba el juego |
| 2026-10-21 | Aditivo: documentos nuevos `data/item_effects` y `data/new_game_stats`, el campo opcional `moves.learn_chances` y el opcional `evolution.item_targets` del ruleset, escrito a mano. Las dos herramientas son compatibles con la fase 3 y el contrato no cambia | Nada |
| 2026-10-21 | Más estricto al validar: `dwx validate` también lee con los lectores estrictos del juego `data/items`, `data/item_effects`, `data/shops`, `data/new_game_stats` y los textos (`localization` y `text`). El formato no cambia | Nada, si el juego ya cargaba el paquete. Un paquete que ahora falla la validación tampoco lo cargaba el juego |

**Textos planos (fuera del contrato).** Desde la fase 3, `text/<idioma>/` son ficheros planos `clave: "texto"`
([localización](localizacion.md)). No es parte del contrato, porque ninguna herramienta los lee, pero quien compare con
la salida de `dwx` tiene que volver a extraer un paquete base sacado antes de la fase 3.

## Lo que aún no está probado

Con la [fase 2](../plan/fase-2-prueba-vertical.md) el juego carga mapas y modelos, y se ha probado un paquete de
mod hecho a mano con la forma que dejan las herramientas: capa `mods`, dependencia de `base`, un mapa
(`map/twna01/data.yaml` con un punto de aparición movido) y un modelo de personaje (`model.glb`, `texture.png` y
`texture_meta.yaml` de otro personaje en el ID del partner). Con `--mods`, el registro dice que esos cuatro assets
salen del mod, el partner se dibuja con el modelo cambiado, el jugador aparece en el punto nuevo y la ruta de
teclado pasa con 0 errores de validación.

Falta repetirlo con la salida real del editor de mapas y del de modelos cuando generen un paquete completo.
