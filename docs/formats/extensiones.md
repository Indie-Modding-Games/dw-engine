# Extensiones y archivos del juego

Un Digimon o un mapa no corresponde a una sola extensión: combina un modelo, imágenes, animaciones y documentos de datos. Esta guía distingue los archivos del disco original, los archivos editables que extrae `dwx` y los que prepara para distribución.

Para arrancar el proyecto, consulta [Ejecutar el juego](../getting-started/jugar.md). Para los detalles binarios de cada formato, usa los enlaces de las tablas.

## Modelos de Digimon y personajes

| Archivo | Qué contiene | Cómo se utiliza |
|---|---|---|
| `.glb` | Modelo glTF 2.0 binario; puede incluir malla, materiales, texturas, esqueleto y clips de animación | El extractor escribe un modelo por archivo. El motor carga este formato; se puede inspeccionar y editar con herramientas compatibles con glTF, como Blender. |
| `texture.png` | Textura extraída del personaje | Imagen editable. Si usa paletas, necesita conservar su metadato asociado. |
| `texture_meta.yaml` | Modo de imagen, ubicación original y paletas | Permite interpretar el PNG indexado y elegir la paleta correcta; no es otra textura. |
| `contract.yaml` | Contrato del modelo: estructura y animaciones requeridas | Sirve para validar sustituciones y especies añadidas; no contiene la malla. |
| `models/<personaje>.yaml` | Referencia al modelo y correspondencia entre acciones y clips | Indica qué animación utiliza el personaje al estar quieto, andar o correr. |
| `data/digimon.yaml` | Tabla de especies y referencias a otros datos | Describe atributos de las especies; no reemplaza el modelo ni contiene por sí sola toda la lógica de crianza. |

Ejemplo de organización de contenido extraído:

```text
contenido/
└── base/
    ├── pack.yaml
    └── digimon/
        └── agum/
            ├── model.glb
            ├── texture.png
            ├── texture_meta.yaml
            └── contract.yaml
```

Los nombres cortos son identificadores de contenido, no extensiones especiales. En el formato actual, las animaciones del modelo viajan en el `.glb`; no hay que buscar un archivo `.anim` separado. El cocinador actual valida y conserva el `.glb`, sin convertirlo a una supuesta extensión de malla propia.

Detalles: [modelos del original](modelos.md) y [modelos y animación en el motor](../architecture/models.md).

## Mapas y escenarios

| Archivo | Qué contiene | Qué se cambia allí |
|---|---|---|
| `map/<id>/data.yaml` | Configuración de cámara, luces, entidades, sprites, conexiones y colisión del mapa, según el esquema | Los datos espaciales y de presentación. La historia y las reglas pueden referenciar documentos o scripts adicionales. |
| `background_day.png`, `background_dusk.png`, `background_night.png` | Fondos prerenderizados para distintas horas | El aspecto del escenario. Cambiar la imagen no cambia automáticamente la colisión, los pasos ni la cámara. |
| `texture4/*.png`, `texture8/*.png` | Texturas indexadas de elementos del mapa | Sus imágenes; los sufijos 4 y 8 indican la profundidad de índices del original. |
| `*_meta.yaml` | Paletas y metadatos de las imágenes | Conserva la información necesaria para reproducir sus colores y variantes. |
| Otros documentos `.yaml` y scripts `.lua` | Reglas, encuentros, eventos o referencias compartidas | El comportamiento del mapa se compone con estos sistemas; no todo vive en `data.yaml`. |

Por ejemplo, `map/twna01/data.yaml` es un documento y `map/twna01/background_day.png` es una imagen. Un archivo YAML de mapa no es una escena 3D de Blender ni una imagen de fondo. Consulta [Mapas](mapas.md) para el formato y sus secciones.

## Otros archivos editables y de ejecución

| Extensión o nombre | Uso |
|---|---|
| `.yaml` | Tablas, reglas, configuración, traducciones y manifiestos. La estructura depende del documento, no solo de la extensión. |
| `pack.yaml` | Identidad, versión, dependencias, prioridad y esquemas de un paquete. |
| `.schema.json` | JSON Schema para describir y validar documentos de datos; no es un archivo de partida. |
| `.lua` | Scripts de lógica, eventos y comportamiento de UI. Se cargan como texto; el pipeline no distribuye bytecode Lua como sustituto. |
| `.rml` | Estructura de menús y HUD de RmlUi. Se parece a HTML, pero pertenece al sistema de UI del motor. |
| `.rcss` | Estilos de RmlUi: geometría, colores, tipografía y estados. Su compatibilidad no es idéntica a la del CSS de un navegador. |
| `.png` | Texturas, fondos, iconos o atlas de fuentes. Un PNG indexado puede necesitar `*_meta.yaml`. |
| `.ttf` | Fuente tipográfica cargada mediante FreeType. La fuente clásica usa su propio atlas y métricas extraídos localmente. |
| `.sf2` | Banco SoundFont con instrumentos y muestras para sintetizar música. |
| `.mid` | Secuencia MIDI: notas y eventos; necesita los instrumentos correspondientes para sonar. |
| `.wav` | Audio PCM extraído, por ejemplo efectos o el audio de las películas. |
| `.y4m` | Vídeo YUV4MPEG2 extraído. La película se acompaña de audio WAV y un descriptor YAML; no es un MP4. |
| `.slang` | Código fuente de shaders que se compila para la GPU. Es parte del desarrollo del renderizador. |
| `.cpp`, `.hpp` | Implementación y cabeceras C++ del motor y sus herramientas. No son contenido que se edite para cambiar una textura o un diálogo. |

Las partidas también son documentos YAML, pero tienen su propio contrato de versión y se guardan en la ubicación de datos del jugador. No son tablas del paquete `base`. Véanse [guardados](../architecture/save.md), [UI](../architecture/ui.md), [localización](../architecture/localizacion.md) y [audio](audio.md).

## Archivos cocinados y paquetes

«Cocinar» significa preparar contenido para cargarlo o distribuirlo, conservando los archivos fuente para futuras ediciones.

| Fuente | Salida actual | Observación |
|---|---|---|
| `.png` | `.ktx2`, cuando el plan de textura permite convertirlo | Textura preparada para GPU. Las imágenes que necesitan selección de paleta en ejecución conservan PNG y sus metadatos. |
| `.glb` | `.glb` validado | Mantiene el formato glTF binario. |
| `.wav` de sonido | `.opus` | Audio comprimido en contenedor Ogg. El WAV de una película se conserva con su vídeo. |
| YAML, Lua y otros archivos sin conversor específico | Copia del archivo | No se convierten automáticamente en un binario propio. |
| Carpeta de paquete | `.pak`, si se empaqueta | Archivo que reúne assets y manifiesto; no es un ejecutable ni un formato del disco original. |

Detalles: [KTX2](ktx2.md), [Opus](opus.md), [PAK](pak.md) y [pipeline de assets](../architecture/assets-pipeline.md). Para editar, vuelve a las fuentes; no modifiques cachés o salidas generadas como si fueran la fuente del proyecto.

## Extensiones del disco original

Son entradas del extractor, no los formatos editables del nuevo motor.

| Formato | Significado | Referencia |
|---|---|---|
| `.cue` + `.bin` | Hoja de pistas y datos del disco. El CUE referencia su BIN; deben conservarse juntos. | [Disco](disco.md) |
| `.iso` | Imagen de sectores de datos. Puede carecer de información de audio/vídeo necesaria para extraer películas completas. | [Disco](disco.md), [vídeo](video.md) |
| `.tmd` | Modelo del formato gráfico original. | [Modelos](modelos.md) |
| `.mmd` | Paquete de modelo y animaciones del juego. | [Modelos](modelos.md) |
| `.tim` | Imagen original, a menudo indexada y con paletas CLUT. | [TIM](tim.md) |
| `.map` | Datos de cámara, luces, imágenes, entidades y colisión del escenario original. | [Mapas](mapas.md) |
| `.tfs` | Datos del fondo prerenderizado original. | [Mapas](mapas.md) |
| `.scn` | Datos y bytecode de eventos, como `MAPHEAD.SCN` y `DG.SCN`. No es un script Lua. | [Scripts](scripts.md) |
| `.vhb`, VAB y SEQ | Bancos y secuencias del sistema de sonido original; algunos están agrupados dentro de otros archivos. | [Audio](audio.md) |
| `.str` | Película con vídeo MDEC y audio XA intercalados. | [Vídeo](video.md) |
| `.efe` | Datos de efectos y técnicas del original. | [Efectos](efectos.md) |
| `*_REL.BIN` | Overlay de código y datos cargado por el juego original. No es una biblioteca nativa del motor. | [Overlays](overlays.md) |

La misma extensión `.bin` puede designar una imagen de disco o un archivo interno distinto. Algunos ejecutables regionales tampoco terminan en `.exe`. El extractor identifica estructuras y versiones; renombrar la extensión no convierte un formato en otro.

## Rutas e identificadores

El motor suele referenciar un asset **sin extensión**: `digimon/agum/model` o `map/twna01/data`. El sistema de archivos virtual resuelve el archivo de su paquete, por ejemplo `model.glb` o `data.yaml`. Esto permite conservar un identificador aunque cambie su representación al cocinar el contenido.

No todos los YAML son intercambiables: `pack.yaml`, `contract.yaml`, un mapa, una traducción y una partida tienen responsabilidades y esquemas diferentes. Consulta el [VFS](../architecture/vfs.md) y los [esquemas](../architecture/json-schema.md) antes de cambiar su estructura.
