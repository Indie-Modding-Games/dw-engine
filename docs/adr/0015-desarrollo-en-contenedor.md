# 0015. Compilar dentro de un contenedor Docker

- **Estado:** Aceptada
- **Fecha:** 2026-10-18

## Contexto

El motor necesita Clang 22, libc++, CMake reciente y vcpkg. Las distribuciones traen versiones distintas, y
instalarlas a mano ensucia la máquina de cada desarrollador y da builds que solo funcionan en una máquina.

## Decisión

- La compilación, los tests, el formato y clang-tidy se ejecutan dentro de una **imagen Docker** definida en
  `.devcontainer/Dockerfile`: Ubuntu 24.04, LLVM 22 de apt.llvm.org, vcpkg en un commit fijo y la CMake que fija
  ese commit de vcpkg (la de Ubuntu 24.04 no conoce C++26).
- Todo se lanza con **`scripts/dev.sh`**. La CI usa los mismos comandos, así que un build que pasa en local pasa en
  la CI.
- El contenedor corre con el usuario del host, y el repositorio se monta en la misma ruta: los ficheros generados
  son del desarrollador y las rutas de `compile_commands.json` sirven también fuera.
- La caché binaria de vcpkg vive en un volumen (`dwe-vcpkg-cache`), así que las dependencias se compilan una vez.
- **El juego se ejecuta en el host**, que tiene la GPU y la pantalla. Para que funcione sin instalar nada, el
  ejecutable enlaza libc++ y SDL3 de forma estática; solo necesita el loader de Vulkan y el driver de la GPU.
- El `devcontainer.json` permite abrir el mismo entorno desde VS Code o CLion.

## Consecuencias

- La única dependencia del host es Docker (y el driver de la GPU para jugar).
- El primer `configure` tarda, porque vcpkg compila todas las dependencias; después sale de la caché.
- La CI se aparta en dos cosas para tardar menos sin caché, y ninguna cambia el código que compila:
  - no instala la capa de validación de Vulkan, que es la feature `validation` de `vcpkg.json`, activa por defecto en
    local, porque ningún test dibuja;
  - en Linux usa el triplet `x64-linux-libcxx-release`, que compila cada dependencia una vez en lugar de dos.
- La caché binaria de vcpkg guarda cada paquete por separado, así que una dependencia nueva solo compila esa.
- La imagen no puede ser más nueva que el sistema donde se juega: el ejecutable enlaza la glibc de la imagen. Por eso
  es Ubuntu 24.04 aunque 26.04 ya trae LLVM 22 en sus repositorios.
- En Windows el contenedor solo sirve para compilar para Linux; el build nativo de Windows llegará con su propio
  preset.

## Alternativas descartadas

- **Instalar el toolchain en el host:** rápido de empezar, pero cada máquina acaba distinta.
- **Nix:** reproducible, pero el equipo tendría que aprenderlo.
- **Colima o una VM:** necesita virtualización por hardware, que no todas las máquinas tienen activada.
