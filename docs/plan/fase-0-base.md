# Fase 0: Base

## Objetivo

Un motor mínimo que compila igual en cualquier máquina del equipo, abre una ventana con Vulkan, lee teclado y mandos,
ejecuta Lua y tiene herramientas de depuración. Es el suelo sobre el que se construye todo lo demás.

## Por qué

Sin esta base, cada persona compilaría distinto, los fallos dependerían de la máquina y cada sistema nuevo tendría
que resolver de nuevo la ventana, la entrada o el log. La fase 0 resuelve eso una sola vez y deja una CI que impide
que se rompa.

## Alcance

- Documentación inicial: arquitectura, ADR, plan y convenciones.
- Build reproducible: CMake con presets, dependencias con vcpkg y compilación dentro de un contenedor Docker.
- Calidad automática: clang-format, clang-tidy y CI en Linux y Windows.
- Módulos del motor: `core`, `platform`, `rhi`, `input`, `script` y `debug`.
- `dwx verify`: reconoce qué versión del disco tiene el usuario.

## Fuera de alcance

- Leer cualquier formato del disco → fase 1.
- Dibujar algo más que un marcador de prueba: modelos, fondos, UI del juego → fase 2.

## Requisitos

| ID | Requisito | Cómo se comprueba |
|---|---|---|
| R0.1 | El proyecto compila con un solo comando, igual en todas las máquinas, sin instalar nada en el sistema salvo Docker | `scripts/dev.sh build` en una máquina limpia ([preparar el entorno](../getting-started/setup.md)) |
| R0.2 | El ejecutable funciona en el sistema del jugador sin depender de librerías del contenedor | `ldd` del binario sin libc++; se ejecuta fuera del contenedor |
| R0.3 | Compila sin warnings con los warnings como errores, y clang-tidy no da avisos | Preset `ci` y paso de clang-tidy en la CI |
| R0.4 | Se abre una ventana con un dispositivo Vulkan 1.3; la swapchain se rehace al cambiar el tamaño | Ejecución con la capa de validación: cero errores |
| R0.5 | La entrada funciona por acciones (no por teclas), con teclado, ratón y mandos conectados en caliente ([ADR 0014](../adr/0014-mandos-y-entrada.md)) | Tests de `input` y prueba manual con mando |
| R0.6 | La lógica corre a paso fijo, separada del render | Tests de `FixedStep` |
| R0.7 | Una VM de Lua aislada (sin acceso a ficheros ni al sistema) ejecuta scripts y los recarga en caliente | Tests de `script`, incluido el sandbox |
| R0.8 | Hay log y una consola ImGui con línea de comandos de Lua | Prueba manual: F1 abre la consola |
| R0.9 | `dwx verify` identifica las versiones soportadas del disco: USA, Europa, Japón y Japón rev. 1 | Tests con los SHA-1 de los ejecutables |

## Diseño

- Módulos: `core` (tipos, log, errores con `std::expected`, sin excepciones: [ADR 0007](../adr/0007-sin-excepciones.md)),
  `platform` (SDL3), `rhi` (Vulkan con vk-bootstrap), `input`, `script` (Lua 5.5 + sol2), `debug` (Dear ImGui).
- Decisiones: [ADR 0002](../adr/0002-cpp26.md) (C++26), [ADR 0003](../adr/0003-vulkan-y-sdl3.md) (Vulkan y SDL3),
  [ADR 0004](../adr/0004-lua-y-sol2.md) (Lua), [ADR 0015](../adr/0015-desarrollo-en-contenedor.md) (contenedor).
- Demo: `apps/game` mueve un marcador con el teclado o el mando desde un script de Lua.

## Criterio de cierre

Se abre una ventana, un mando o el teclado mueven algo en pantalla, se ejecuta un script de Lua y la CI pasa.

## Dependencias y riesgos

- Depende solo de Docker en la máquina de desarrollo.
- Riesgo: Windows. Los presets y el job de Windows están escritos, pero solo se verifican cuando la CI corre en
  GitHub.

## Estado (2026-10-19)

Hecha en Linux: cero warnings, cero avisos de clang-tidy, tests en verde y el juego corre en el host sin errores de
validación. La CI corre en GitHub desde el primer push; falta confirmar el job de Windows y que el equipo decida la
limpieza de los repositorios antiguos.
