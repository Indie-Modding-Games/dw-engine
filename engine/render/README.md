# render

El render del juego, encima de `rhi`. Ver [render.md](../../docs/architecture/render.md).

- `Renderer`: dueño de la tabla bindless, del anillo de subidas, del target de profundidad (que sigue al tamaño de la
  ventana) y de las texturas (`TextureId`). `renderFrame` dibuja un frame: escena con profundidad → UI → overlay de
  depuración. `createPipeline` hace pipelines para un pase. Las texturas se crean y actualizan esperando
  (`createTexture`, `updateTexture`, en la carga) o en cola para el siguiente frame (`queueTexture`,
  `queueTextureUpdate`, con el juego en marcha).
- `Drawer`, `PrepareContext` y `DrawContext`: la interfaz por la que el mapa, los modelos y la UI preparan sus datos
  antes de los pases y graban sus draws en un pase.
- `Camera`, `perspective`, `pixelProjection`: matrices para el clip space de Vulkan (Y hacia abajo, profundidad 0..1).
- `QuadBatch`: rectángulos texturizados en píxeles en un solo draw (sprites, minimapa, imágenes de depuración).

Sus shaders están en `shaders/` y se compilan con `dwe_add_shaders` ([ADR 0018](../../docs/adr/0018-shaders-slang-con-vcpkg.md)).
