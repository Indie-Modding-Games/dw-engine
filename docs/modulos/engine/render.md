# render

El render del juego, encima de `rhi`. Ver [render.md](../../architecture/render.md).

- `Renderer`: dueño de la tabla bindless, del anillo de subidas, del target de profundidad (que sigue al tamaño de la
  ventana) y de las texturas (`TextureId`). `renderFrame` dibuja un frame: escena con profundidad → UI → overlay de
  depuración. `createPipeline` hace pipelines para un pase. Las texturas se crean y actualizan esperando
  (`createTexture`, `updateTexture`, en la carga) o en cola para el siguiente frame (`queueTexture`,
  `queueTextureUpdate`, con el juego en marcha). Las copias en cola dejan libre una cuarta parte de la memoria de
  subidas del frame (`DrawUploadShare`, `textureCopyFits` en `<dwe/render/upload_budget.hpp>`): los pases suben sus
  vértices y matrices después de ellas, y antes un mapa recién cargado llenaba los 16 MiB con sus texturas y el
  primer frame no podía subir los vértices del mapa («upload ring is out of room»). Lo que no cabe espera al frame
  siguiente; una copia mayor que lo que queda sin la reserva va sola, la primera de su frame.
  `textureWritePending` dice si una textura aún tiene una copia esperando. `setMultisampling` dibuja el pase de
  escena con 4 muestras por píxel (el ajuste `antialiasing`; [nitidez](../../architecture/render.md#nitidez-suavizado-de-bordes-y-texturas-de-modelos)).
- `FrameDesc::textureScenes`: escenas que se dibujan en una textura propia (`createSceneTexture`) antes de los pases
  de la ventana; con `TextureScene::keep` se dibuja encima de lo que la textura tenía en vez de borrarla.
- `Drawer`, `PrepareContext` y `DrawContext`: la interfaz por la que el mapa, los modelos y la UI preparan sus datos
  antes de los pases y graban sus draws en un pase.
- `Camera`, `perspective`, `pixelProjection`: matrices para el clip space de Vulkan (Y hacia abajo, profundidad 0..1).
- `ModelRenderer`: modelos con piel (`assets::ModelData`), subidos una vez y dibujados N veces por frame con skinning
  en la GPU, luz del original y semitransparencias ordenadas, con un drawer para lo opaco y otro para lo
  semitransparente. `SlotMap` guarda sus modelos tras handles con generación. Ver [modelos](../../architecture/models.md).
  `setTextureFilter` elige cómo se muestrean sus texturas (`TextureFilter`: `Nearest`, `Smooth`, `Upscaled`, el
  ajuste `texture_filter`).
- `scenery_upscale.hpp`: el escalador ×2 del motor (`upscaleScenery` para el fondo en todas sus variantes,
  `upscaleRgba` para texturas con alfa) y `upscale_cache.hpp` (`UpscaleCache`, `UpscaleKey`): lo que hace se guarda
  comprimido con zstd en `<datos del usuario>/cache/upscale`. Ver [Nitidez](../../architecture/remaster-pipeline.md#nitidez).
- `QuadBatch`: rectángulos texturizados en píxeles en un solo draw (sprites, minimapa, imágenes de depuración).
- `ScreenFade`: resta un gris de toda la pantalla sobre lo dibujado antes (mezcla `Subtractive`), como el fundido
  del cambio de mapa del original (`fade.c: renderFade`); `setWhiteness` suma el mismo gris (mezcla `Additive`,
  `FADE_MODE` 1) para el fundido a blanco de la historia.

- `SceneGrade` (`<dwe/render/scene_grade.hpp>`): el paso de luz y color del campo (mejora, opción): dibuja la escena
  en la ventana con el tinte de la hora, luces puntuales, vibrance, contraste y brillo, como `FrameDesc::post`
  (`Pass::Post`), antes de la UI. `scene_light.hpp`: la tabla de luces por mapa (`parseMapLights`), el tinte por hora
  (`daylightAt`), el sol de las sombras (`sunAt`) y la intensidad de cada luz (`lightStrength`). `ground_shadows.hpp`:
  la sombra redonda del original (`roundShadow`) y la del sol (`sunShadow`), billboards que `MapRenderer` dibuja con
  `groundDrawer()` entre el fondo y los modelos. Ver [render.md](../../architecture/render.md#paso-de-luz-y-color).

Sus shaders están en `shaders/` y se compilan con `dwe_add_shaders` ([ADR 0018](../../adr/0018-shaders-slang-con-vcpkg.md)).

## Mapas de campo

Cómo dibuja el original un mapa está en [Mapas: cómo se dibuja](../../formats/mapas.md#cómo-se-dibuja); la
política para cualquier resolución, en el [ADR 0020](../../adr/0020-camara-de-mapa-y-capas-de-profundidad.md).

- `MapCamera` (`<dwe/render/map_camera.hpp>`, solo CPU): la cámara fija del mapa y una proyección perspectiva
  descentrada con la distancia focal `H` en píxeles del fondo y el centro óptico en el centro del fondo. Con
  `setViewport` y `follow` da la vista, la proyección (profundidad de Vulkan, 0 en el plano cercano de 100 y 1 a
  65 536) y la ventana del fondo que se ve: escala `min(ancho / 320, alto / 240)`, centrada en el jugador y sujeta a
  los bordes, con bandas si el fondo es más pequeño que la ventana (`backgroundRect`). Los helpers pasan del mundo al
  fondo y a la ventana (`projectToBackground`, `worldToWindow`), de la ventana al suelo (`pickGround`, para el clic
  para moverse) y de un píxel del fondo con una profundidad a clip (`backgroundToClip`, para el fondo y las capas de
  profundidad). `mapCameraDesc` convierte la cámara cruda del mapa (doblando punto de vista y referencia, como
  `loadMapSetup`). `setShake` corre la ventana del fondo unos píxeles en x después de sujetarla, para el temblor de
  pantalla de la historia (`map.c: tickMeramonShake`, que mueve `DRAWING_OFFSET_X`).
- Los tests comparan las proyecciones con el cálculo en coma fija del original (rotación 4.12, RTPS, división por
  `SZ` y `cameraX` sujeto a los bordes) en mapas reales: coinciden a un píxel y cuarto en el fondo.
- `loadMapSceneData` (`<dwe/render/map_scene.hpp>`) lee un mapa por su id a través del VFS: el `MapDocument`, su
  cámara y sus imágenes (`assets::loadMapImages`: el fondo como índices con una paleta por variante y la hoja de
  sprites ya resuelta). Las reglas de VRAM del original (páginas, filas de CLUT, límites del orden) viven en `assets`,
  no aquí ([ADR 0020](../../adr/0020-camara-de-mapa-y-capas-de-profundidad.md)). `timeOfDayAt` y
  `backgroundVariant` eligen la variante como `updateTimeOfDay`.
- `MapDepthLayers` (`<dwe/render/map_layers.hpp>`): las capas de profundidad. Cada instancia de sprite toma su
  entrada de la tabla de orden (`assets::mapSpriteOrderValue` con la profundidad de la cámara) y se dibuja como un
  plano a `orderValue · 16` de distancia; sus animaciones van a 20 fotogramas por segundo, como el campo del
  original. El orden de dibujo se calcula una vez al cargar; `quads()` solo rellena un búfer que guarda.
- `MapRenderer` (`<dwe/render/map_renderer.hpp>`): dibuja un mapa con todo lo anterior en el pase de escena. Sube
  el fondo como índices a su resolución nativa, una textura de paletas con una fila por variante y otra para congelar
  un fundido interrumpido, y una textura por textura de la hoja de sprites (en la carga, con `createTexture`: así
  están en el primer frame del mapa sin pasar por la memoria de subidas),
  compartida por todas las horas: los sprites de `clut` −1 también son índices y miran la paleta de la hora en el
  shader. `update` avanza las animaciones y el fundido de la hora (`setVariant`, 25 fotogramas del original; un
  cambio a mitad de fundido sigue desde los colores que se ven) y guarda el foco; el `Drawer` del fondo ajusta la
  cámara al tamaño del fotograma en `prepare`. Da dos `Drawer`: el fondo (sin profundidad, primero) y las capas (los
  sprites de lejos a cerca con prueba de profundidad `LessOrEqual` y su mezcla, y las bandas negras al final). Lo que
  haya entre los dos (los modelos) usa `camera().view()` y `camera().projection()` y queda tapado por las capas que
  tiene delante. `setMarkers` dibuja rectángulos de prueba de pie en el suelo. `setBillboards` dibuja cuadros con textura (de `addBillboardTexture`, que duran entre mapas) de pie y de cara a la cámara o tumbados en el suelo, opacos o semitransparentes como los sprites de las capas: los objetos del suelo y sus sombras (`item.c: renderDroppedItem`). Dibujar no reserva memoria: los
  búferes de cada fotograma se reutilizan. `setGrade` (`MapGrade`, mejora para las estaciones de
  [packages/game/weather](../game/weather.md#estaciones)) colorea el fondo, las capas y los cuadros en el shader: los verdes
  giran hacia otro tono, luego saturación, ganancia por canal y `lift`; las bandas nunca. Neutro por defecto: los
  colores salen tal cual.
- `MapRenderer::setHdScenery` (por defecto encendido): el fondo y las capas se filtran en `map.slang` siguiendo los
  bordes a la resolución de la ventana (`SamplingSmooth`); el fondo y los sprites que siguen la hora se leen como
  texturas RGBA con la paleta que se ve (`resolveBackgroundColors`, rehechas solo cuando cambia la paleta).
  `hdSceneryScene()` da el pase del fondo para `FrameDesc::textureScenes`: guarda el fondo filtrado en una textura
  del tamaño de la ventana que da la vuelta al desplazarse la cámara (`<dwe/render/hd_scenery_wrap.hpp>`), así que
  cada frame filtra solo la franja que el desplazamiento descubre, y entera cuando cambian los colores, el tamaño de
  la ventana o el mapa. El fondo (`SamplingScreen`) y los recortes (`SamplingCutoutScreen`) la leen con una lectura
  por píxel; sin el pase filtran al vuelo, con el mismo resultado. Apagado, cada cuadro usa `SamplingSharp` y la
  imagen es la del original exacta. Ver [Alta definición en el motor](../../architecture/remaster-pipeline.md#alta-definición-en-el-motor).
  `setSceneryQuality` (`SceneryQuality::Upscaled` por defecto, el ajuste `scenery_quality`): el pase del fondo
  remuestrea el fondo doblado por `upscaleScenery` (`SamplingUpscaledPrefilter`), hecho en un hilo al cargar el mapa
  y guardado en la caché (`setUpscaleCache`); hasta que está subido, y si no se puede hacer, se usa el filtro de
  siempre. Ver [Nitidez](../../architecture/remaster-pipeline.md#nitidez).
- `MapCutouts` (`<dwe/render/map_cutouts.hpp>`, solo CPU): dice, por sprite y posición, si un sprite de las capas es
  un recorte exacto del fondo (`clut` −1, píxel entero, cada texel dibujado igual al índice del fondo debajo). Los
  recortes se dibujan en `SamplingCutout` (o `SamplingCutoutScreen`): su máscara y su clase son las del sprite, el
  color el del fondo filtrado.
- `dwe-game --map <id>[,<id>...]` lo enseña sin la escena de campo: `--time day|dusk|night`, `--focus x,z` y
  `--size AnchoxAlto`; las flechas mueven un marcador relativo a la cámara, el clic lo manda al punto del suelo, las
  acciones `debug_time_day`, `debug_time_dusk` y `debug_time_night` (1, 2 y 3) cambian la hora y `debug_next_map` (N)
  pasa al siguiente mapa de la lista y registra cuánto tardó el cambio.
