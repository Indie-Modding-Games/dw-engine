# 0003. Vulkan 1.3 para render y SDL3 para plataforma

- **Estado:** Aceptada
- **Fecha:** 2026-10-18

## Contexto

La plataforma objetivo es **PC: Windows y Linux**, incluida Steam Deck. El render no debe tener las limitaciones de la
GPU de PS1, y queremos control total sobre él.

## Decisión

- **Vulkan 1.3** como base, con dynamic rendering y synchronization2.
- Librerías de apoyo: **vk-bootstrap** (inicialización) y, desde la fase 2, **VMA** (memoria). Las funciones de Vulkan
  se enlazan con el loader del sistema. No se usa volk, porque el backend Vulkan de Dear ImGui que trae vcpkg enlaza
  el loader directamente y mezclar los dos rompe el enlazado.
- Shaders en **Slang**, compilados a SPIR-V.
- **SDL3** para ventana, input (teclado, ratón y mandos), audio y ficheros. No se usa SDL_GPU.
- Todo el acceso a Vulkan queda dentro del módulo `rhi`.

## Consecuencias

- Control total del render y acceso a las funciones modernas de la GPU de PC.
- Más código propio que con una capa de abstracción ya hecha.
- Vulkan 1.3 lo soportan todas las GPU de PC de los últimos años, en Windows y en Linux.
- macOS, consolas y móviles no son plataformas objetivo. Si algún día lo fueran, `rhi` es el único sitio que cambiaría.

## Alternativas descartadas

- **SDL_GPU:** más sencillo, pero menos control.
- **Direct3D 12:** solo funciona en Windows.
- **OpenGL:** una API antigua, con peor control del rendimiento.
