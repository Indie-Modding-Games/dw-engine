# Render

Cómo dibuja el motor: la base de GPU en `rhi` (Vulkan 1.3, [ADR 0003](../adr/0003-vulkan-y-sdl3.md)), el flujo de
frame en `render`, y cómo enchufan sus dibujos el mapa, los modelos y la UI.

## Capas

| Módulo | Capa | Qué tiene |
|---|---|---|
| `rhi` | base | `Device` (swapchain, frames en vuelo, `destroyLater`), VMA, `Buffer`, `Image`, `BindlessTable`, `GraphicsPipeline`, `UploadRing`, `CommandList` |
| `render` | sistemas | `Renderer` (flujo de frame, texturas, target de profundidad), `Drawer`, `Camera`, `QuadBatch`, `ScreenFade`, `ModelRenderer` |
| `apps/game` | apps | Junta todo; hoy dibuja una escena de prueba (`TestScene`) hasta que exista la del campo |

Las llamadas a Vulkan solo están en `rhi`. `render`, `ui` y los demás usan sus tipos (`rhi::CommandList`,
`rhi::Buffer`...), nunca funciones `vk*`. La excepción es `debug`, que inicia Dear ImGui con los handles nativos.

## Modelos dentro de la UI

`Renderer::createSceneTexture(extent)` crea un color muestreable compatible con los pipelines de escena y una profundidad independiente. `FrameDesc::textureScenes` recibe el identificador y sus drawers: se preparan antes de aplicar las subidas y se dibujan antes de los pases de la ventana. RHI sincroniza la transición de lectura de textura a attachment y de vuelta; cada uso limpia color transparente y profundidad.

`UiRenderer::bindTexture("runtime/nombre", id)` permite usar `<img src="/runtime/nombre"/>` sin copiar píxeles a CPU. Registra el préstamo antes de cargar el documento: RmlUi conoce las dimensiones y dibuja la imagen entre sus demás elementos, aplicando su recorte. El dueño conserva la textura hasta destruir la UI; liberarla desde RmlUi no destruye el recurso. No se reemplazan bindings, porque los documentos conservan las imágenes en caché. Una imagen `runtime/` sin binding (el retrato no se pudo crear, o un test sin renderer) se carga como un píxel transparente: no se ve y RmlUi no avisa de que no pudo cargarla. El contenido debe respetar el alpha premultiplicado de la UI; las partes opacas de un modelo ya tienen alpha uno.

La ficha de compañero usa este mecanismo con un modelo independiente del actor de campo, su colocación de crianza y una pose fija. Abrir la ficha no cambia posición, rotación ni animación del actor del mundo. El target solo se dibuja mientras Status está visible. Se libera con `destroyTexture`, después de cerrar la UI y antes de destruir Renderer.

## Flujo de un frame

`Renderer::renderFrame(FrameDesc)` hace, en orden:

1. `Device::beginFrame()`: espera al frame anterior del mismo slot y adquiere la imagen del swapchain. Si no hay
   (ventana minimizada, swapchain recién reconstruido) devuelve `false` y no dibuja nada.
2. Reinicia el trozo del anillo de subidas de ese slot.
3. Llama a `Drawer::prepare` de cada drawer de `FrameDesc::scene` y `FrameDesc::ui`, fuera de los pases.
4. Graba las subidas de texturas en cola (`queueTexture`, `queueTextureUpdate`) que quepan en el anillo, con sus
   barreras, en el command buffer del frame.
5. Rehace el target de profundidad si la ventana cambió de tamaño.
6. **Pase de escena** (`Pass::Scene`): color del swapchain (limpiado con `clearColor`) + profundidad (limpiada a 1).
   Llama a cada `Drawer` de `FrameDesc::scene`.
7. Si el frame trae `FrameDesc::post`, la escena del paso 6 se dibujó en una textura del tamaño de la ventana (el
   renderer la rehace al cambiar el tamaño) y el **pase de post** (`Pass::Post`) la dibuja en el swapchain con ese
   drawer, que la lee con `DrawContext::sceneColor()`. Sin `post` la escena va directa al swapchain, como siempre.
   Barrera de color entre pases.
8. **Pase de UI** (`Pass::Ui`): el mismo color, cargado, sin profundidad. Llama a cada `Drawer` de `FrameDesc::ui`
   (el fundido del cambio de mapa, RmlUi) y después a `FrameDesc::overlay` con el `rhi::Frame` crudo (el overlay de
   ImGui). El overlay es siempre lo último que se graba: puede cambiar cualquier estado (pipeline, set de
   descriptores) sin restaurarlo.
9. Cierra el anillo de subidas y `Device::endFrame()` presenta.

Antes de cada `Drawer` el viewport y el scissor son el target entero. Un drawer puede cambiarlos; `setScissor` recorta
el rectángulo al target, así que valen los recortes fuera de pantalla que da RmlUi. Al enlazar un pipeline con depth
bias dinámico el bias vuelve a 0: se fija con `setDepthBias` después de `bindPipeline`.

## Recursos y propiedad

- **Valores con dueño** (`rhi::Buffer`, `rhi::Image`, `rhi::GraphicsPipeline`, `rhi::UploadRing`): se mueven, no se
  copian. Al destruirse mandan sus objetos de Vulkan a `Device::destroyLater`, que los destruye cuando terminan los
  frames que podían usarlos. Se pueden soltar en cualquier momento, pero nunca deben sobrevivir al `Device`.
- **Texturas**: las posee el `Renderer`. `createTexture` devuelve un `TextureId`, un valor con el índice en la tabla
  bindless (lo que recibe el shader) y una generación; `destroyTexture` la suelta y su índice se reutiliza cuando
  acaban los frames en vuelo. Un id destruido deja de valer aunque su índice vuelva a usarse: destruirlo otra vez o
  actualizarlo no hace nada (avisa en el log). Quien crea una textura la destruye. `whiteTexture()` es una textura
  blanca 1×1 para dibujar sin textura.
- **Drawers**: los posee quien los crea (el código del mapa, de los modelos, de la UI); el `Renderer` solo los toma
  prestados durante `renderFrame`.
- **Orden de destrucción**: drawers y texturas → `Renderer` → `Device`.

## Shaders

En Slang, compilados a SPIR-V en el build y embebidos en el ejecutable ([ADR 0018](../adr/0018-shaders-slang-con-vcpkg.md)).

1. Escribe `<módulo>/shaders/<nombre>.slang` con un `[shader("vertex")] vertexMain` y un
   `[shader("fragment")] fragmentMain` (los nombres por defecto de `GraphicsPipelineDesc`).
2. En el `CMakeLists.txt` del módulo: `dwe_add_shaders(dwe_<módulo> FILES shaders/<nombre>.slang)`.
3. En C++: `#include <dwe/shaders/<nombre>.hpp>` y usa `dwe::shaders::<Nombre>` (el nombre en PascalCase:
   `skinned_mesh.slang` → `SkinnedMesh`) como `GraphicsPipelineDesc::spirv`.

Convenciones de los shaders:

- `import dwe.bindless;` da `gTextures[]`, `gSamplers[4]` y `sampleTexture(textura, sampler, uv)`. Los samplers son
  `SamplerNearestClamp`, `SamplerNearestRepeat`, `SamplerLinearClamp` y `SamplerLinearRepeat`, en el orden de
  `rhi::SamplerKind`.
- Push constants: un struct con `[[vk::push_constant]]`, como mucho 128 bytes (`rhi::PushConstantBytes`), con
  layout std430. Su espejo en C++ debe tener los mismos offsets.
- Entradas de vértice con `[[vk::location(n)]]`, que casan con `rhi::VertexAttribute::location`.
- Matrices: `float4x4` en column-major, igual que GLM; `mul(m, v)` es `m * v` de GLM.
- Datos por frame grandes (matrices de huesos): un puntero en las push constants (`float4x4* joints;`) a una
  asignación del anillo (`UploadAllocation::address`).

## Pipelines

```cpp
auto pipeline = renderer.createPipeline(
    {.spirv = dwe::shaders::SkinnedMesh,
     .vertex = {.stride = sizeof(Vertex), .attributes = Attributes},
     .cull = rhi::CullMode::Back,
     .depth = {.test = true, .write = true},
     .blend = rhi::BlendMode::Opaque,
     .name = "skinned mesh"},
    render::Pass::Scene);
```

`createPipeline` rellena los formatos de color y profundidad del pase. Un pipeline solo sirve para su pase (el de UI
no tiene profundidad). Viewport y scissor son dinámicos; el depth bias también si `DepthState::bias` está activo.
Modos de mezcla: `Opaque`, `Alpha`, `PremultipliedAlpha` (RmlUi), `Additive` y `Subtractive`. Los cuatro modos de
semitransparencia del original salen de ellos: B/2+F/2 = `Alpha` con alfa 0,5; B+F = `Additive` con alfa 1;
B−F = `Subtractive` con alfa 1; B+F/4 = `Additive` con alfa 0,25. `rhi::validate(desc)` comprueba la descripción sin
GPU.

`FrontFace` sigue la definición de Vulkan. Como `render::perspective` invierte la Y del clip space, el resultado es
el de OpenGL: las caras antihorarias vistas de frente (las de glTF) son `FrontFace::CounterClockwise`.

## Paso de luz y color

Mejoras opcionales del campo, ninguna con equivalente en el original ni con efecto en reglas, colisión o números
aleatorios (opciones y textos: [game.md](../modulos/apps/game.md#sombras-luz-y-colores-vivos)).

- **Sombras.** La redonda es la del original (`drop_shadow.c: renderDropShadow`): un billboard plano de 2 × radio de
  la especie bajo los pies, SPOT256 (uv 0x40, 0x80, 63 × 63) restado con 0x30/128. `MapRenderer::groundDrawer()` las
  dibuja justo después del fondo y antes de los modelos, como el original en su entrada 0xFFD de la tabla de orden:
  si fueran billboards de las capas (después de los modelos), la profundidad oscurecería las piernas. La del sol es
  otra mancha más larga (`sunShadow`) que sale de los pies en la dirección contraria al sol: `sunAt` mueve el sol de
  +x al empezar el día a −x al empezar la noche, más alto a mitad (sombra de 1,3 radios) y bajo en los extremos
  (hasta 3,7), con fuerza 0 de noche y en mapas sin hora y ×0,65 con la paleta del atardecer. Los billboards planos
  giran ahora alrededor del eje vertical (`MapBillboard::turn`), sin cambiar los que tienen giro 0.
- **Tinte por hora.** `daylightAt` interpola nueve claves fijadas a las horas del horario (`TimeOfDaySchedule`): dorado
  al amanecer (1,06; 0,99; 0,89), neutro a mediodía, naranja al atardecer (1,07; 0,94; 0,87), azul de noche (0,91;
  0,96; 1,10). Es pequeño a propósito: las paletas del fondo ya cambian con la hora (map.slang) y el tinte solo
  inclina hacia donde ya va la pintura. Los mapas sin hora (hora fija o una sola paleta: cuevas, casas) quedan
  neutros y con las lámparas encendidas.
- **Luces del mapa.** `field/lights` del paquete del juego: por mapa, luces en píxeles del fondo (así siguen a la
  lámpara pintada con cualquier tamaño de ventana y desplazamiento de la cámara) con radio, color, intensidad y tipo:
  `fire` (siempre, más fuerte de noche, parpadea con tres senos), `lamp` (del atardecer al amanecer; siempre en mapas
  sin hora) o `glow` (siempre, pulso lento). El parpadeo es una onda del tiempo real, nunca del generador del juego.
  `windowLights` las pasa a píxeles de la ventana con la cámara del mapa, quita las de fuera y deja las 16 más
  fuertes. Con una cámara de escena propia no se dibujan (el fondo no se mueve con ella).
- **Paso de color** (`SceneGrade`, `shaders/scene_grade.slang`): un triángulo que cubre la ventana lee la escena
  texel a texel, la multiplica por el tinte, suma las luces (aclaran sobre todo multiplicando, para que una pared
  oscura siga siéndolo, y añaden un brillo propio pequeño), aplica vibrance (los colores apagados ganan más
  saturación que los vivos), un contraste suave hacia una curva en S que deja el negro y el blanco, y un brillo con
  cuatro texels cercanos por encima de 0,72. Con «Colores vivos»: vibrance 0,3, contraste 0,12, brillo 0,22. El
  fundido de pantalla y la UI van en el pase de UI, después: nunca se tocan.

Coste en llvmpipe a 1280 × 720 (medido con otros agentes cargando la máquina, carga ~20 en 12 núcleos, en pares
lanzados a la vez para igualar la carga): con las tres opciones apagadas el frame cuesta lo mismo que antes del
cambio; con todo encendido, en torno a un 25 % más de tiempo de frame (p. ej. 67 → 85 ms y 77 → 96 ms en twna01 de
noche). La primera versión, con muestreo lineal y ocho muestras de brillo, costaba el doble.

## Dibujar: `Drawer`

```cpp
class MapDrawer final : public render::Drawer {
    void draw(render::DrawContext& context) override {
        auto joints = context.upload().upload(std::span<const glm::mat4>{matrices});  // válido este frame
        context.commands().bindPipeline(pipeline_);
        context.commands().bindVertexBuffer(vertices_);
        context.commands().bindIndexBuffer(indices_, rhi::IndexType::Uint16);
        context.commands().pushConstants(Push{.joints = joints->address, .texture = texture_.index});
        context.commands().drawIndexed(indexCount_);
    }
};
```

`Drawer::prepare(PrepareContext&)` es opcional y corre antes de los pases: ahí se crean o actualizan texturas con
`queueTexture`/`queueTextureUpdate` para dibujarlas en ese mismo frame. Una librería de UI que crea texturas y
geometría mientras "renderiza" (RmlUi) lo hace en `prepare` y repite los draws guardados en `draw`.

`DrawContext` trae la `CommandList` (`commands()`), el `UploadRing` (`upload()`), el pase (`pass()`) y el tamaño del target en píxeles (`extent()`). Los drawers se pasan
al frame en `FrameDesc::scene` o `FrameDesc::ui`, en el orden en que deben dibujarse. Al rellenar `FrameDesc` hay que nombrar todos sus campos (un
span vacío si un pase no dibuja nada): el build avisa de los que faltan.

## Mallas, texturas y datos por frame

Hay dos momentos: **la carga**, donde se puede esperar a la GPU, y **el juego en marcha**, donde nada debe esperar.

| Qué | En la carga (espera a la GPU) | Con el juego en marcha (sin esperar) |
|---|---|---|
| Malla estática | `rhi::Buffer::createWithData(device, BufferUsage::Vertex, bytes)` (memoria de GPU) | `rhi::Buffer::createMapped(device, usage, bytes)` (memoria host, dibujable al momento) |
| Textura nueva | `renderer.createTexture(extent, rgba)` | `renderer.queueTexture(extent, rgba)` |
| Cambiar una región | `renderer.updateTexture(id, rect, rgba)` | `renderer.queueTextureUpdate(id, rect, rgba)` |
| Datos de cada frame | — | `context.upload().upload(span)` o `allocate(bytes)` en `prepare`/`draw` |

- Texturas: RGBA de 8 bits, filas desde arriba; sin píxeles = transparente.
- Las de cola devuelven el id al momento y guardan una copia de los píxeles. Se graban al empezar el siguiente frame,
  antes de sus pases: llamadas entre frames o en `prepare` se ven en ese frame; llamadas en `draw`, en el siguiente
  (una textura creada en `draw` no se dibuja hasta entonces). Se aplican en orden; si una no cabe en el anillo de ese
  frame espera al siguiente con las que vienen detrás. Una sola subida no puede pasar de
  `RendererDesc::uploadBytesPerFrame`.
- El anillo (16 MiB por frame por defecto, alineado a 16) solo existe durante un frame: el `Renderer` lo da a los
  drawers en `prepare` y `draw`; asignar fuera del frame aborta. Si se llena, devuelve vacío y lo registra en el log.

## Mallas con skinning

Convención para el track de modelos: **un vertex buffer entrelazado** (binding 0, `VertexLayout::stride`) con
`position` `Float3`, `normal` `Float3`, `uv` `Float2`, `joints` `Uint8x4` o `Uint16x4` y `weights` `Unorm8x4`,
`Unorm16x4` o `Float4`, más un index buffer. Los modelos añaden `color` `Float4` (location 3, el color de vértice) y
guardan vértices e índices en un solo buffer, con los índices detrás de los vértices. El skinning se hace en el vertex shader con las matrices de huesos del
frame en el anillo, leídas por puntero (`float4x4* joints` en push constants). No usamos varios bindings ni vertex
pulling: un binding basta mientras la animación es por CPU y deja que `rhi::validate` compruebe el layout.

## Cámara y espacios

- `render::Camera`: `view`, `projection`, `viewport`; `viewProjection()`.
- `render::perspective(fovY, aspect, near, far)`: proyección derecha con profundidad 0..1 y la Y invertida para el
  clip space de Vulkan. El mundo es el de [world-space.md](world-space.md) (+Y arriba).
- `render::pixelProjection(extent)`: píxeles con origen arriba a la izquierda → clip space, para 2D.
- `render::QuadBatch`: rectángulos texturizados en píxeles, en un único draw, en cualquiera de los dos pases.
- `render::ScreenFade`: resta un gris de todo el target (mezcla `Subtractive`, fondo − gris) sobre lo dibujado
  antes, como el rectángulo del fundido del original (`fade.c: renderFade`); no mezcla hacia negro. El juego lo pone
  el primero de `FrameDesc::ui`: resta de la escena y la UI queda encima (el nombre del mapa delante del fundido).
- `render::MapCamera` y `render::MapRenderer`: la cámara fija de un mapa de campo, con proyección descentrada sobre
  su fondo, y el dibujo del fondo y sus capas de profundidad
  ([ADR 0020](../adr/0020-camara-de-mapa-y-capas-de-profundidad.md), [página del módulo](../modulos/engine/render.md)).

## Samplers

`rhi::SamplerKind`: `NearestClamp`, `NearestRepeat` (píxeles nítidos, mip más cercano) y `LinearClamp`,
`LinearRepeat` (filtrado bilineal, mezcla de mips y anisotropía 16× o el máximo de la GPU). Las texturas creadas con
`mipmapped` (las de los modelos con `texture_filter` en `smooth` o `upscaled`) tienen su cadena entera; las demás, un
solo nivel.

## Nitidez: suavizado de bordes y texturas de modelos

Dos mejoras opcionales, encendidas por defecto, que solo cambian la definición de la imagen (las razones y las
medidas están en [Nitidez](remaster-pipeline.md#nitidez)):

- **Suavizado de bordes** (`antialiasing`): `Renderer::setMultisampling` dibuja el pase de escena en un color y una
  profundidad de 4 muestras por píxel (`MsaaSamples`; Vulkan obliga a aceptar 4 en color y profundidad, así que no
  hace falta otro camino) y los promedia al final del pase en la ventana o en la textura del paso de luz. Cada
  pipeline de `Pass::Scene` se crea con su variante multimuestreada. Las texturas de las escenas propias
  (`textureScenes`, el pase del fondo HD, los retratos) siguen con una muestra. El fragment shader corre una vez por
  píxel: solo se suavizan los bordes de los triángulos (los contornos de los modelos y de los cuadros), y lo que un
  shader descarta (los texels transparentes, la máscara de las capas) sigue siendo el píxel del original.
- **Texturas de modelos** (`texture_filter`): `ModelRenderer::setTextureFilter`. `Nearest` es el texel del original;
  `Smooth` filtra con mipmaps y anisotropía (`FlagSmooth` en `skinned_model.slang`), y antes rellena el color de los
  texels transparentes con el de sus vecinos dibujados para que el filtrado no meta un borde oscuro; `Upscaled`, el
  valor por defecto, filtra las texturas dobladas al subir el modelo por el escalador del motor (`upscaleRgba`,
  [Nitidez](remaster-pipeline.md#nitidez)), con el alfa interpolado: los recortes por alfa dan contornos suaves. Las
  texturas que se reescriben en juego (`updateImage`, las caras) se doblan igual. Cambiar el filtro vuelve a subir
  las texturas de cada modelo.

## Modelos

`render::ModelRenderer` sube cada `assets::ModelData` una vez y dibuja las instancias que se le envían cada frame
con sus matrices de huesos (de `animation`) en el anillo. Da dos drawers para `FrameDesc::scene`: `opaque()` (lo
opaco y lo recortado por alfa) y `translucent()` (lo semitransparente, sin escribir profundidad). El orden de la
escena del juego es **fondo → modelos opacos → capas del mapa → modelos semitransparentes**: las capas del mapa
tapan a los modelos opacos por profundidad, y lo semitransparente se mezcla sobre todo lo opaco. `opaque()` va
siempre antes: su `prepare` toma las instancias y sube las matrices, y `translucent()` dibuja de eso; solo,
`translucent()` no dibuja nada. Lo semitransparente se ordena **por instancia** (la profundidad en espacio de vista
de su origen), no por triángulo. Cómo pinta luz, transparencias y caras está en
[modelos](models.md#dibujar-modelrenderer).

## Requisitos de la GPU

Vulkan 1.3 con dynamic rendering y synchronization2; de 1.2, descriptor indexing (array en runtime, partially bound,
update-after-bind y update-unused-while-pending de imágenes, índices no uniformes), buffer device address y layouts
de profundidad separados; filtrado anisótropo ([ADR 0019](../adr/0019-descriptores-bindless.md)). Swapchain
`B8G8R8A8_UNORM` o `R8G8B8A8_UNORM`, con la mezcla en espacio gamma. Profundidad: `D32_SFLOAT`, o `X8_D24` o `D16` si
no hay.

Para probar la vida de las texturas, `dwe-game --texture-churn` crea y destruye una textura en cada frame (el cuadrito
bajo la barra naranja); con la capa de validación no debe salir ningún mensaje `vulkan:`.

## AddressSanitizer y vk-bootstrap

vk-bootstrap es una librería C++ de vcpkg compilada sin AddressSanitizer. Al enlazarla, sus copias de plantillas de
libc++ (el crecimiento de `std::vector<std::string>`, por ejemplo) pueden ganar a las copias instrumentadas del
proyecto, o al revés; mezclar las dos deja desfasadas las anotaciones de los contenedores y ASan informa de
desbordamientos de contenedor que no existen (pasó en `assets::DiskFileSystem::listFiles`). Cambiar el orden de
enlazado solo mueve el problema al código de vk-bootstrap, e instrumentarla exigiría otro triplet de vcpkg.

Por eso `rhi/src/device.cpp` define `__asan_default_options` con `detect_container_overflow=0` en las builds con
ASan: solo lo llevan los ejecutables que enlazan `Device` (y con él vk-bootstrap), es decir el juego y los tests de
`render`. El resto de suites mantiene la detección. `ASAN_OPTIONS` lo puede volver a activar.
`render/tests/link_test.cpp` reproduce el caso con el orden de enlazado malo.

## Pendiente

- Mipmaps, texturas sRGB y formatos comprimidos (KTX2, fase 1) en `rhi::Image`.
- Pruebas de render con capturas de referencia (`tests/golden/`): la imagen Docker no tiene GPU.
