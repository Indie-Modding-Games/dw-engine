# 0019. Un único set de descriptores bindless y datos por frame por dirección de buffer

- **Estado:** Aceptada
- **Fecha:** 2026-10-19

## Contexto

El render de la fase 2 tiene varios clientes (fondos y capas del mapa, modelos con skinning, RmlUi, minimapa) que
dibujan con texturas distintas y datos que cambian cada frame (matrices de huesos, vértices de la UI). Gestionar sets
de descriptores por material o por draw añade mucho código en `rhi`, pools que se agotan y sincronización con los
frames en vuelo. Vulkan 1.2 ya trae descriptor indexing y buffer device address, y los soportan todas las GPU de PC
que cumplen el ADR 0003.

## Decisión

- **Un solo set de descriptores en todo el motor** (`rhi::BindlessTable`): binding 0, un array de imágenes
  muestreadas (4096 por defecto, limitado por la GPU) con `UPDATE_AFTER_BIND`, `UPDATE_UNUSED_WHILE_PENDING` y
  `PARTIALLY_BOUND`; binding 1, cuatro samplers inmutables (nearest/linear × clamp/repeat). Los nearest toman el mip
  más cercano y no filtran (pixel art, UI); los linear mezclan mips y usan anisotropía 16× (o el máximo de la GPU).
- Una textura se nombra por `rhi::TextureId`: su índice en el array, que es lo único que recibe el shader (en push
  constants o en los vértices), y la generación de ese hueco. La tabla sube la generación al quitar una textura, así
  que un id viejo deja de valer aunque su índice se reutilice; quitarla dos veces no hace nada.
- **Un solo pipeline layout**: ese set más 128 bytes de push constants para todas las etapas gráficas. Se enlaza una
  vez por frame.
- **Datos por frame por dirección de buffer**: `rhi::UploadRing` reparte un buffer host-visible en un trozo por frame
  en vuelo; cada asignación da su dirección de GPU, que el shader lee como puntero (`float4x4*` en Slang). Los
  vértices dinámicos se enlazan como vertex buffer con offset dentro del mismo anillo.
- Las texturas se liberan con `Device::destroyLater`; su índice se reutiliza solo cuando terminan los frames que
  podían usarlo. Por eso un descriptor solo se escribe en huecos que ningún frame pendiente usa, que es lo que
  `UPDATE_UNUSED_WHILE_PENDING` permite hacer con frames aún en vuelo.
- **Color en espacio gamma**: el swapchain es `B8G8R8A8_UNORM` (o `R8G8B8A8_UNORM` si la GPU no ofrece el primero),
  no sRGB, y las texturas son `R8G8B8A8_UNORM`. Los shaders trabajan con los valores tal cual vienen y la mezcla
  ocurre en espacio gamma, como en el original y como espera Dear ImGui. Un render lineal (texturas y swapchain sRGB)
  sería otra decisión.

## Consecuencias

- Añadir una textura es escribir un descriptor; un pipeline nuevo no necesita layouts propios.
- Los draws de la UI con varias texturas caben en un solo draw (el índice va en el vértice).
- Requiere Vulkan 1.2 con `descriptorIndexing`, `runtimeDescriptorArray`, `descriptorBindingPartiallyBound`,
  `descriptorBindingSampledImageUpdateAfterBind`, `descriptorBindingUpdateUnusedWhilePending`,
  `shaderSampledImageArrayNonUniformIndexing`, `bufferDeviceAddress` y `separateDepthStencilLayouts` (el target de
  profundidad usa `DEPTH_ATTACHMENT_OPTIMAL`), y `samplerAnisotropy`; el dispositivo los exige al elegir la GPU.
- Los shaders deben usar `NonUniformResourceIndex` cuando el índice no es uniforme (lo hace `sampleTexture`).
- Las push constants usan `VK_SHADER_STAGE_ALL_GRAPHICS` y no `VK_SHADER_STAGE_ALL`: con `ALL`, el driver de NVIDIA
  probado pierde memoria en cada `vkCmdPushConstants` (lo detecta LeakSanitizer).
- Buffers de almacenamiento o uniformes con descriptores, si hicieran falta, se añadirían como bindings nuevos del
  mismo set.

## Alternativas descartadas

- **Sets por material o por draw:** más código, pools que gestionar y más llamadas por draw.
- **Push descriptors:** siguen necesitando un layout por tipo de draw y no sirven para la UI con muchas texturas.
- **Uniform buffers dinámicos para las matrices:** necesitan descriptores y alineaciones propias; la dirección de
  buffer es más simple y no tiene límite de tamaño práctico.
