# Estructura del repositorio

```
engine/
├─ .devcontainer/           imágenes Docker de desarrollo: Clang, libc++, CMake, vcpkg, Node y VitePress (ADR 0015);
│                           la del lanzador y la de Wine
├─ .github/
│  ├─ workflows/            ci.yml (comprobaciones de los pull requests), release.yml (versión de prueba en cada
│  │                        fusión), promote.yml (versión estable), platform-check.yml, docs-pages.yml
│  ├─ actions/              pasos compartidos de los flujos
│  ├─ CONTRIBUTING.md       enlace a docs/contribuir.md
│  └─ pull_request_template.md
├─ cmake/                   funciones propias: dwe_add_module, dwe_add_shaders, warnings, sanitizers
│  └─ toolchains/           compilador y biblioteca estándar de cada plataforma
├─ docs/                    esta documentación: el sitio de VitePress (dev.sh docs) y la página de cada módulo
│                           (modulos/)
├─ engine/                  módulos del motor (modulos/engine/)
│  ├─ core/                 Result, log, YAML, JSON Schema, matemáticas
│  ├─ platform/             ventana, eventos y mandos (SDL3)
│  ├─ rhi/                  el único que habla con Vulkan
│  ├─ render/
│  ├─ audio/
│  ├─ input/                acciones, nunca teclas
│  ├─ assets/               paquetes, VFS, ids y tablas
│  ├─ animation/            clips de los modelos: poses, bucles, movimiento raíz
│  ├─ scene/                entidades y componentes (EnTT)
│  ├─ script/               Lua y la API de los mods
│  ├─ ui/                   RmlUi con su lógica en Lua
│  ├─ localization/         textos en el idioma del jugador
│  ├─ signs/                carteles de los mapas traducidos
│  ├─ video/                películas
│  ├─ debug/                consola y paneles de desarrollo (Dear ImGui)
│  └─ ruleset/              formato de ruleset, estrategias por nombre y reloj de juego
├─ game/                    reglas del juego (modulos/game/)
│  ├─ partner/              cuidado, vida, entrenamiento
│  ├─ evolution/
│  ├─ battle/
│  ├─ field/                movimiento, colisión, warps, seguimiento del partner
│  ├─ inventory/            bolsa, dinero, uso de objetos y tiendas
│  ├─ fishing/ · weather/ · gym/ · tournament/ · curling/
│  ├─ story/                estado de la historia y scripts de eventos
│  └─ save/                 partidas guardadas: ranuras, autoguardado y migraciones
├─ formats/                 lectores de los formatos del disco original: imágenes, modelos, mapas, audio, vídeo,
│                           scripts (solo los usa dwx)
├─ apps/                    (modulos/apps/)
│  ├─ session/              la partida: junta los sistemas de game/, sin ventana
│  ├─ screens/              modelos de vista de las pantallas
│  ├─ content/              cómo montan los paquetes el juego y dwe-sim
│  ├─ game/                 dwe-game, el ejecutable del juego
│  ├─ dwx/                  extract · validate · cook · pack · export-images
│  ├─ sim/                  dwe-sim: simulaciones de balance sin gráficos
│  ├─ launcher/             el lanzador de escritorio (Tauri)
│  └─ studio/               DWE Studio, banco de trabajo de desarrollo (Electron)
├─ services/
│  └─ beta-gateway/         la pasarela de la beta entre el lanzador y GitHub (Cloudflare Worker)
├─ shaders/                 módulos de Slang compartidos (cada módulo compila los suyos a SPIR-V)
├─ content/
│  ├─ schemas/              JSON Schema de cada tipo de documento
│  ├─ packs/                paquetes en texto del equipo: game (datos, rulesets, UI y Lua) y es-translation
│  └─ enhance/              recetas de escalado de imágenes (dwx extract y dwx upscale)
├─ tests/
│  ├─ autotest/             rutas que juegan el juego de verdad (y training/, la comprobación del entrenamiento)
│  ├─ mods/                 paquetes sintéticos para los tests de mods
│  └─ support/              lo que comparten los tests de todos los módulos
├─ scripts/                 dev.sh: la entrada de todo el desarrollo (image, build, test, run, docs…); gate.sh,
│                           training-check.sh, launcher.sh, el empaquetado (package_*.sh, package/) y la
│                           publicación (ci/, release_local.sh)
├─ prototypes/              maquetas que no se compilan
├─ third_party/             solo parches o lo que no esté en vcpkg
├─ triplets/                triplets propios de vcpkg (dependencias con Clang y libc++)
├─ vcpkg-ports/             ports propios de vcpkg
├─ CMakeLists.txt
├─ CMakePresets.json        linux-clang-*, windows-clang-*, windows-cross*, macos-*, ci y windows-ci
├─ vcpkg.json               dependencias con versiones fijadas
├─ .clang-format · .clang-tidy · .editorconfig · .gitattributes · .gitignore · .luacheckrc · stylua.toml
└─ README.md · LICENSE
```

`content/base/` no aparece porque **nunca está en el repositorio**: se genera a partir del disco de cada usuario y
está en `.gitignore` (ver [política legal](../legal/assets.md)).

## Forma de un módulo

Todos los módulos, del motor y del juego, tienen la misma forma:

```
engine/render/
├─ CMakeLists.txt           dwe_add_module(render LAYER systems DEPS core rhi)
├─ include/dwe/render/      API pública: lo único que ven los demás módulos
├─ src/                     implementación y headers privados
└─ tests/                   tests unitarios del módulo
```

- Su documentación es una página del sitio, `docs/modulos/<capa>/<módulo>.md` (aquí
  `docs/modulos/engine/render.md`), no un README en la carpeta.
- Otro módulo solo puede incluir `<dwe/render/...>`. Lo que está en `src/` es privado.
- Los namespaces siguen la carpeta: `dwe::render`, `dwe::game::battle`.
- Un módulo nuevo se declara con `dwe_add_module`, que aplica los warnings, los sanitizers y el chequeo de
  dependencias.
