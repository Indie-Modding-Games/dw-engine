# 0018. Shaders en Slang compilados en el build con `slangc` de vcpkg

- **Estado:** Aceptada
- **Fecha:** 2026-10-19

## Contexto

El [ADR 0003](0003-vulkan-y-sdl3.md) elige Slang para los shaders, pero no dice cómo se compilan ni cómo llega el
SPIR-V al juego. Las condiciones: todo se compila dentro del contenedor (ADR 0015) y en Windows con
`x64-windows-static`; no se instala nada en el host; el juego no debe buscar ficheros de shaders al arrancar; y los
demás módulos (mapa, modelos, UI) tienen que poder añadir sus propios shaders sin tocar el render.

## Decisión

- **`slangc` es una herramienta de host de vcpkg**: `{"name": "shader-slang", "host": true}` en `vcpkg.json`. El port
  trae los binarios oficiales de Slang y solo admite enlazado dinámico, así que nunca es una dependencia del
  ejecutable: solo corre durante el build. Linux usa el triplet de host `x64-linux-libcxx` (CRT dinámico); los presets
  de Windows fijan `VCPKG_HOST_TRIPLET=x64-windows` para que el target estático no arrastre el port. CMake busca
  `slangc` solo en las herramientas de ese triplet (`NO_DEFAULT_PATH`), para que un SDK de Vulkan en el `PATH` no
  cuele otra versión, y escribe la versión al configurar.
- **`dwe_add_shaders(<target> FILES a.slang ...)`** (`cmake/DweShaders.cmake`) compila cada fichero, con todos sus
  entry points marcados con `[shader("...")]`, a **un único módulo SPIR-V** (`-fvk-use-entrypoint-name`,
  `-matrix-layout-column-major` como GLM, warnings como errores, dependencias de `import` por depfile y, por si un
  generador lo ignora, de todos los módulos compartidos de `shaders/`).
- **El SPIR-V se embebe** en una cabecera generada `<dwe/shaders/<nombre>.hpp>` con
  `dwe::shaders::<Nombre>`, un `std::array<std::uint32_t, N>`, privada del target. No hay ficheros de shaders en
  runtime.
- Los **módulos de Slang compartidos** viven en `shaders/` (por ejemplo `shaders/dwe/bindless.slang`, que se importa
  con `import dwe.bindless;`). Los shaders de cada módulo viven en `<módulo>/shaders/` y los compila su propio
  `CMakeLists.txt`.

## Consecuencias

- Los shaders se compilan y validan en cada build, en la CI incluida; un error de Slang rompe el build.
- Cada track añade shaders con una línea de CMake, sin tocar `render`.
- El ejecutable lleva sus shaders dentro: no hay rutas que resolver ni ficheros que puedan faltar.
- Cambiar un shader recompila solo el `.cpp` que incluye su cabecera.
- La recarga en caliente de shaders queda para más adelante (la haría `debug`, compilando con la API de Slang).
- La versión de Slang la fija la baseline de vcpkg (2026.19 hoy).

## Alternativas descartadas

- **glslang/GLSL:** funciona, pero el ADR 0003 ya eligió Slang (módulos, genéricos, punteros) y el port de vcpkg
  funciona en el contenedor.
- **Instalar el SPIR-V junto al ejecutable:** obliga a buscar ficheros al arrancar y a empaquetarlos aparte.
- **Compilar en runtime con la librería de Slang:** añade una DLL grande al juego y no deja ver los errores en el
  build.
