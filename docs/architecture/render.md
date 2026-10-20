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
7. Barrera de color entre pases.
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
  ([ADR 0020](../adr/0020-camara-de-mapa-y-capas-de-profundidad.md), [README](../../engine/render/README.md)).

## Samplers

`rhi::SamplerKind`: `NearestClamp`, `NearestRepeat` (píxeles nítidos, mip más cercano) y `LinearClamp`,
`LinearRepeat` (filtrado bilineal, mezcla de mips y anisotropía 16× o el máximo de la GPU). Hoy las imágenes tienen
un solo nivel de mip; los samplers ya están listos para cuando lleguen.

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
