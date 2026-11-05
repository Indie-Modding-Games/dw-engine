# rhi

El único módulo que habla con Vulkan 1.3 ([ADR 0003](../../adr/0003-vulkan-y-sdl3.md)). Ver
[render.md](../../architecture/render.md) para el conjunto.

- `Device`: GPU, cola, swapchain y frames con dynamic rendering (dos en vuelo; `Frame::slot` dice cuál). El
  swapchain se reconstruye solo al cambiar el tamaño de la ventana. Lo que se suelta mientras la GPU aún puede usarlo
  pasa por `Device::destroyLater`, que lo destruye cuando terminan los frames que pueden usarlo. Lleva el asignador de
  memoria (VMA).
- `Buffer`: memoria de GPU (rellenada con una subida que espera, `createWithData`) o del host (escrita directamente,
  `createMapped`), con dirección de GPU.
- `Image`: imágenes 2D muestreadas, de color o de profundidad; `createTexture` sube RGBA de 8 bits y `write` reemplaza
  una región, esperando a la GPU. Sin esperar: `ImageDesc::initialize = false` y `CommandList::initializeImage` /
  `writeImage` dentro del frame. `bestDepthFormat` elige el formato de profundidad. `ImageDesc::mipmapped` da la
  cadena de mips entera (`mipLevelCount`), que cada escritura rehace con blits; `ImageDesc::samples` hace targets
  multimuestreados y `maxSampleCount` dice cuántas muestras acepta la GPU.
- `BindlessTable`: el único set de descriptores del motor (texturas por índice y generación, `TextureId`, y cuatro
  samplers fijos)
  y el único pipeline layout (ese set y 128 bytes de push constants) ([ADR 0019](../../adr/0019-descriptores-bindless.md)).
- `GraphicsPipeline`: pipelines gráficos desde una `GraphicsPipelineDesc` (SPIR-V con los dos entry points, vértices,
  topología, cull, profundidad, mezcla, formatos). `validate` comprueba la descripción sin GPU. Con
  `GraphicsPipelineDesc::multisampled` (2, 4 u 8) el pipeline lleva además una variante para un pase con esas
  muestras: `CommandList::bindPipeline` usa la que corresponde al pase (`RenderingDesc::samples`), y
  `ColorAttachment::resolve` promedia el target multimuestreado en otra imagen al final del pase.
- `UploadRing`: memoria por frame para datos que cambian cada frame (huesos, vértices de UI) y para las subidas de
  texturas; solo se asigna entre `beginFrame` y `endFrame`.
- `CommandList`: grabación de un frame (pases, subidas a imágenes, pipelines, viewport, scissor recortado al target,
  push constants, buffers, draws).
- `Texture`: sube a la GPU una imagen con su cadena de mipmaps, en RGBA de 8 bits o en BC7 (hoy, el visor de assets
  del overlay); `Device::supportsBc7` dice si la GPU muestrea BC7.

`LinearAllocator` y `SlotAllocator` son la contabilidad de CPU del anillo y de la tabla; sus tests no necesitan GPU.
