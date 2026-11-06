# Estructura del repositorio

El repositorio es un monorepo al estilo de Turborepo: **`apps/`** tiene lo que se ejecuta (el juego, las
herramientas, el lanzador), **`packages/`** tiene las librerías agrupadas por capa (motor, juego y las librerías que
comparten las apps) y **`tooling/`** tiene la configuración de build común. Cada paquete es autocontenido: su
`CMakeLists.txt`, su código, sus tests y un README que dice qué es, de qué depende y cómo compilarlo o probarlo solo
(`scripts/dev.sh build --filter <paquete>`, ver [puesta en marcha](../getting-started/setup.md#compilar-y-probar-un-paquete)).

```
dw-engine/
├─ apps/                    lo que se ejecuta (modulos/apps/)
│  ├─ game/                 dwe-game, el ejecutable del juego
│  ├─ dwx/                  extract · validate · cook · pack · export-images · evolution-guide · upscale
│  ├─ sim/                  dwe-sim: simulaciones de balance sin gráficos
│  ├─ launcher/             el lanzador de escritorio (Tauri)
│  ├─ studio/               DWE Studio, banco de trabajo de desarrollo (Electron)
│  ├─ studio-vscode/        maqueta de DWE Studio como extensión de editor (no se compila)
│  └─ beta-gateway/         la pasarela de la beta entre el lanzador y GitHub (Cloudflare Worker)
├─ packages/                librerías, agrupadas por capa
│  ├─ engine/               módulos del motor (modulos/engine/)
│  │  ├─ core/              Result, log, YAML, JSON Schema, matemáticas
│  │  ├─ platform/          ventana, eventos y mandos (SDL3)
│  │  ├─ rhi/               el único que habla con Vulkan; shaders/ tiene los módulos de Slang compartidos
│  │  ├─ render/
│  │  ├─ audio/
│  │  ├─ input/             acciones, nunca teclas
│  │  ├─ assets/            paquetes, VFS, ids y tablas
│  │  ├─ animation/         clips de los modelos: poses, bucles, movimiento raíz
│  │  ├─ scene/             entidades y componentes (EnTT)
│  │  ├─ script/            Lua y la API de los mods
│  │  ├─ ui/                RmlUi con su lógica en Lua
│  │  ├─ localization/      textos en el idioma del jugador
│  │  ├─ signs/             carteles de los mapas traducidos
│  │  ├─ video/             películas
│  │  ├─ debug/             consola y paneles de desarrollo (Dear ImGui)
│  │  └─ ruleset/           formato de ruleset, estrategias por nombre y reloj de juego
│  ├─ game/                 reglas del juego (modulos/game/)
│  │  ├─ partner/           cuidado, vida, entrenamiento
│  │  ├─ evolution/
│  │  ├─ battle/
│  │  ├─ field/             movimiento, colisión, warps, seguimiento del partner
│  │  ├─ inventory/         bolsa, dinero, uso de objetos y tiendas
│  │  ├─ fishing/ · weather/ · gym/ · tournament/ · curling/
│  │  ├─ story/             estado de la historia y scripts de eventos
│  │  └─ save/              partidas guardadas: ranuras, autoguardado y migraciones
│  ├─ session/              la partida: junta los sistemas de packages/game/, sin ventana
│  ├─ screens/              modelos de vista de las pantallas
│  ├─ content/              cómo montan los paquetes el juego y dwe-sim
│  ├─ formats/              lectores de los formatos del disco original: imágenes, modelos, mapas, audio, vídeo,
│  │                        scripts (solo los usa dwx)
│  └─ vendor/               envoltorios de build de librerías externas que vienen de vcpkg (cgltf), no código propio
├─ tooling/                 configuración de build común
│  ├─ cmake/                funciones propias: dwe_add_module, dwe_add_shaders, warnings, sanitizers
│  │  └─ toolchains/        compilador y biblioteca estándar de cada plataforma
│  ├─ triplets/             triplets propios de vcpkg (dependencias con Clang y libc++)
│  └─ vcpkg-ports/          ports propios de vcpkg
├─ content/
│  ├─ schemas/              JSON Schema de cada tipo de documento
│  ├─ packs/                paquetes en texto del equipo: game (datos, rulesets, UI y Lua) y es-translation
│  └─ enhance/              recetas de escalado de imágenes (dwx extract y dwx upscale)
├─ tests/
│  ├─ autotest/             rutas que juegan el juego de verdad (y training/, la comprobación del entrenamiento)
│  ├─ mods/                 paquetes sintéticos para los tests de mods
│  └─ support/              lo que comparten los tests de todos los módulos
├─ docs/                    esta documentación: el sitio de VitePress (dev.sh docs) y la página de cada módulo
│                           (modulos/)
├─ scripts/                 dev.sh: la entrada de todo el desarrollo (image, build, test, run, docs, packages…);
│                           packages.py (los paquetes y lo que afecta un cambio), gate.sh, training-check.sh,
│                           launcher.sh, el empaquetado (package_*.sh, package/) y la publicación (ci/,
│                           release_local.sh)
├─ .devcontainer/           imágenes Docker de desarrollo: Clang, libc++, CMake, vcpkg, Node y VitePress (ADR 0015);
│                           la del lanzador y la de Wine
├─ .github/
│  ├─ workflows/            ci.yml (comprobaciones de los pull requests), release.yml (versión de prueba en cada
│  │                        fusión), promote.yml (versión estable), platform-check.yml, docs-pages.yml
│  ├─ actions/              pasos compartidos de los flujos
│  ├─ CONTRIBUTING.md       enlace a docs/contribuir.md
│  └─ pull_request_template.md
├─ CMakeLists.txt           add_subdirectory(packages) y add_subdirectory(apps), y los tests del repositorio
├─ CMakePresets.json        linux-clang-*, windows-clang-*, windows-cross*, macos-*, ci y windows-ci
├─ vcpkg.json               dependencias con versiones fijadas
├─ .clang-format · .clang-tidy · .clangd · .editorconfig · .gitattributes · .gitignore · .luacheckrc · stylua.toml
└─ README.md · LICENSE
```

`content/base/` no aparece porque **nunca está en el repositorio**: se genera a partir del disco de cada usuario y
está en `.gitignore` (ver [política legal](../legal/assets.md)).

Los paquetes de `packages/` no pueden depender de nada de `apps/`, el motor solo usa el motor y `packages/game/` solo
tiene módulos de la capa de juego. CMake lo comprueba al configurar ([reglas de dependencias](dependency-rules.md#reglas-de-carpeta)).
`packages/vendor/` queda fuera de esas reglas: no tiene módulos, solo envoltorios de build para librerías externas
que instala vcpkg sin un paquete de CMake.

## Forma de un paquete

Todos los paquetes con código C++, del motor, del juego o de las apps, tienen la misma forma:

```
packages/engine/render/
├─ CMakeLists.txt           dwe_add_module(render LAYER systems DEPS core rhi)
├─ README.md                qué es, de qué depende y los comandos para compilarlo y probarlo solo
├─ include/dwe/render/      API pública: lo único que ven los demás módulos
├─ src/                     implementación y headers privados
└─ tests/                   tests unitarios del módulo
```

- El nombre del paquete es el de su carpeta (`render`). CMake le da un target `pkg_render` que compila sus
  librerías, programas y tests, y una etiqueta de ctest `render` con sus tests.
- Su documentación larga es una página del sitio, `docs/modulos/<grupo>/<módulo>.md` (aquí
  `docs/modulos/engine/render.md`); el README de la carpeta es corto y enlaza a ella.
- Otro módulo solo puede incluir `<dwe/render/...>`. Lo que está en `src/` es privado.
- Los namespaces siguen el módulo: `dwe::render`, `dwe::game::battle`.
- Un módulo nuevo se declara con `dwe_add_module`, que aplica los warnings, los sanitizers y el chequeo de
  dependencias; un programa, con `dwe_add_app`.
