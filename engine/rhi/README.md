# rhi

El único módulo que habla con Vulkan 1.3 ([ADR 0003](../../docs/adr/0003-vulkan-y-sdl3.md)): dispositivo, swapchain y frames con dynamic rendering. El swapchain se reconstruye solo al cambiar el tamaño de la ventana. `Texture` sube a la GPU una imagen con su cadena de mipmaps, en RGBA de 8 bits o en BC7, para muestrearla (hoy, el visor de assets del overlay); `Device::supportsBc7` dice si la GPU muestrea BC7. Lo que se suelta mientras la GPU aún puede usarlo pasa por `Device::destroyLater`, que lo destruye cuando terminan los frames que pueden dibujarlo.
