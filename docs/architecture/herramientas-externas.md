# Compatibilidad con las herramientas del equipo

Hay herramientas de la organización que leen lo que escribe `dwx extract` y escriben paquetes que carga el motor. Lo
que esas herramientas usan es un **contrato**: si el motor lo cambia sin avisar, las herramientas se rompen. Esta
página dice qué es contrato, quién depende de cada parte y cómo se cambia.

## Herramientas

| Herramienta | Repositorio | Qué lee del motor | Qué escribe |
|---|---|---|---|
| Digimon Animation Studio (complemento de Blender) | [3D-Moddeler-Base](https://github.com/Indie-Modding-Games/3D-Moddeler-Base) | `digimon/<slug>/model.glb` del paquete base | Un paquete de la capa `mods` con `digimon/<slug>/model.glb`, con las animaciones nuevas como `anim_NN` |
| DW1 Field Editor (`dw1fe`) | [Digimon-World-Map-Editor](https://github.com/Indie-Modding-Games/Digimon-World-Map-Editor) | `map/<id>/data.yaml` y los fondos del paquete base (lo usa `engine-check` para compararse); los scripts `script/dg/NNN.yaml` y `script/maphead.yaml`, que su panel Script edita como texto; `texture/etcdat/item.png` y su `_meta.yaml` | Un paquete de la capa `mods` con `map/<id>/data.yaml` y `map/<id>/background_*.png`. Los scripts editados todavía se guardan en el formato del disco |

Las dos herramientas se verifican contra la salida de `dwx extract`:

- El Studio comprueba que sus 4843 animaciones coinciden con las del motor.
- El editor comprueba que sus 241 `data.yaml` son idénticos, byte a byte, a los del paquete base.

## Qué es contrato

| Parte | Fuente de verdad | Quién depende | Detalle |
|---|---|---|---|
| Manifiesto `pack.yaml` | `content/schemas/pack.schema.json`, [VFS](vfs.md) | Las dos | `id`, `version`, `layer: mods` y `dependencies` con `base` y `min_version` |
| Reglas de ID de asset | [VFS](vfs.md#ids-de-asset) | Las dos | El ID es la ruta sin extensión; un fichero que no da un ID válido hace fallar el paquete |
| Capas y prioridad | [VFS](vfs.md) | Las dos | Un paquete `mods` gana a `base` por ID |
| IDs de los Digimon | `apps/dwx/src/asset_names.cpp` (`digimonSlug`) | Studio | `AGUM` → `digimon/agum/...` |
| IDs de los mapas | `apps/dwx/src/asset_names.cpp`, a partir de `MAP_ENTRIES` | Editor | La carpeta del mapa es el primer registro de `MAP_ENTRIES` que nombra el fichero |
| Modelo `.glb` | `formats/src/model/gltf.cpp` (`writeModelGlb`), [modelos](../formats/modelos.md) | Studio | Nodos `bone_NN`, una piel rígida, ejes `(x, -y, -z)`, escala 4096 = 1 |
| Animaciones | `formats/src/model/animation.cpp`, [modelos](../formats/modelos.md#animaciones) | Studio | `anim_NN` con NN el id, un fotograma por tick a 20 por segundo, movimiento raíz en el hueso 0 y `extras` (`id`, `frameCount`, `hasScale`, `ticksPerSecond`, `tickCount`, `endsInLoop`, `loops`, `textureMoves`, `sounds`) |
| Contrato del modelo | `content/schemas/model_contract.schema.json`, `digimon/<slug>/contract.yaml` | Studio | Lo que `dwx validate` exige a un modelo que sustituye al original |
| Mapa `data.yaml` | `content/schemas/map.schema.json`, `formats/src/map/map_yaml.cpp` (`mapToYaml`) | Editor | Campos, y además **el texto exacto**: el editor escribe con las mismas reglas de `emitYaml` (`engine/core/src/yaml.cpp`) |
| Fondos de mapa | [mapas](../formats/mapas.md) | Editor | `background_day.png`, `background_dusk.png`, `background_night.png` o `background_paletteN.png` |
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

## Lo que aún no está probado

Ninguna de las dos herramientas ha visto su salida cargada en el motor en ejecución, porque todavía no carga modelos
ni mapas. Eso llega con la [fase 2](../plan/fase-2-prueba-vertical.md). Cuando cierre, se prueba un paquete de cada
herramienta en el juego y se apunta aquí el resultado.
