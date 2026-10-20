# Estructura del repositorio

```
engine/
├─ .devcontainer/           imagen Docker de desarrollo: Clang, libc++, CMake, vcpkg (ADR 0015)
├─ .github/
│  ├─ workflows/            ci.yml (build, tests, formato, docs), release.yml
│  └─ CODEOWNERS
├─ cmake/                   funciones propias: dwe_add_module, warnings, sanitizers
│  └─ toolchains/           compilador y biblioteca estándar de cada plataforma
├─ docs/                    esta documentación
├─ engine/                  módulos del motor
│  ├─ core/
│  ├─ platform/
│  ├─ rhi/
│  ├─ render/
│  ├─ audio/
│  ├─ input/
│  ├─ assets/
│  ├─ scene/
│  ├─ script/
│  ├─ ui/
│  └─ debug/                consola y paneles de desarrollo (Dear ImGui)
├─ game/                    reglas del juego
│  ├─ partner/              cuidado, vida, evolución
│  ├─ battle/
│  ├─ field/                movimiento, colisión, warps, seguimiento del partner
│  ├─ world/                mapas, NPCs, progreso de la ciudad
│  ├─ story/                eventos, diálogos, flags, misiones
│  ├─ inventory/
│  ├─ save/
│  └─ modes/                framework de modos de juego
├─ formats/                 lectores de PS1: TIM, TMD/MMD, mapas, VAB/SEQ, STR, scripts
├─ apps/
│  ├─ game/                 el ejecutable del juego + launcher
│  ├─ editor/               editor (Dear ImGui)
│  └─ dwx/                  extract · validate · cook · pack
├─ shaders/                 Slang → SPIR-V
├─ content/
│  ├─ schemas/              JSON Schema de cada tipo (digimon, map, item, pack…)
│  ├─ packs/                paquetes en texto creados por el equipo: datos, rulesets y Lua
│  └─ fixtures/             assets pequeños hechos por el equipo para tests y CI
├─ tests/
│  ├─ integration/          tests que cruzan módulos
│  ├─ simulation/           simulaciones de balance sobre los rulesets
│  └─ golden/               capturas de render de referencia
├─ scripts/                 dev.sh: la entrada de todo el desarrollo (image, build, test, run…)
├─ third_party/             solo parches o lo que no esté en vcpkg
├─ triplets/                triplets propios de vcpkg (dependencias con Clang y libc++)
├─ CMakeLists.txt
├─ CMakePresets.json        linux-clang-debug, linux-clang-release, ci (windows-clang llegará después)
├─ vcpkg.json               dependencias con versiones fijadas
├─ .clang-format · .clang-tidy · .editorconfig · .gitattributes · .gitignore
└─ README.md · CONTRIBUTING.md · CHANGELOG.md · LICENSE · THIRD_PARTY.md
```

`content/base/` no aparece porque **nunca está en el repositorio**: se genera a partir del disco de cada usuario y
está en `.gitignore` (ver [política legal](../legal/assets.md)).

## Forma de un módulo

Todos los módulos, del motor y del juego, tienen la misma forma:

```
engine/render/
├─ CMakeLists.txt           dwe_add_module(render DEPS core rhi assets)
├─ README.md                qué hace el módulo y enlace a su página de documentación
├─ include/dwe/render/      API pública: lo único que ven los demás módulos
├─ src/                     implementación y headers privados
└─ tests/                   tests unitarios del módulo
```

- Otro módulo solo puede incluir `<dwe/render/...>`. Lo que está en `src/` es privado.
- Los namespaces siguen la carpeta: `dwe::render`, `dwe::game::battle`.
- Un módulo nuevo se declara con `dwe_add_module`, que aplica los warnings, los sanitizers y el chequeo de
  dependencias.
