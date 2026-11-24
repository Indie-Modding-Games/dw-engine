# rhi

El único módulo que habla con Vulkan 1.3 ([ADR 0003](../../adr/0003-vulkan-and-sdl3.md)). Ver
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
  y el único pipeline layout (ese set y 128 bytes de push constants) ([ADR 0019](../../adr/0019-bindless-descriptors.md)).
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

**Informe gráfico** (`gpu_report.hpp`). Un registro de todo el proceso con la GPU, su driver, la actividad en curso y
las últimas 12 llamadas a Vulkan (`gpuCheck`). Una llamada que falla lo escribe en el log, y los manejadores de cierre
del juego lo imprimen en stderr cuando el juego muere por una excepción no atrapada o una señal fatal. Dos rastros
opcionales se activan cuando la GPU los tiene, casi sin coste, y el log dice cuáles (`gpu report: device fault details
on, GPU markers off`):

- `VK_EXT_device_fault`: cuando se pierde el dispositivo, se añaden al informe la descripción del driver, los rangos de
  direcciones del fallo y sus registros del fabricante.
- `VK_AMD_buffer_marker`: la GPU escribe una marca tras cada pasada, escritura de imagen, subida y presentación, así que
  el informe nombra el último trabajo que terminó y el primero al que nunca llegó (`frame markers: last finished #… ;
  first unfinished #…`).

Tras un `VK_ERROR_DEVICE_LOST` ya nada espera a la GPU (`Device::lost`): `beginFrame` no da fotograma, y `waitIdle` y
las subidas vuelven al momento. El juego se cierra entonces sin guardar, con el código de salida 113.
