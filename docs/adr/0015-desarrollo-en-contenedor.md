# 0015. Compilar dentro de un contenedor Docker

- **Estado:** Aceptada
- **Fecha:** 2026-10-18
- **Nota (2026-11-06):** la CI ya no pasa `check-full`. Corre solo en los pull requests: formato y lint de lo que cambia, build y tests (`linux`) y el build cruzado (`windows`). clang-tidy y el sitio de la documentación quedan en `check-full` ([Cómo contribuir](../contribuir.md#flujo-de-trabajo)).

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
- Los objetos del proyecto pasan por **ccache**, con su caché en otro volumen (`dwe-ccache`, o la carpeta de
  `DWE_CCACHE_DIR`). Las rutas se guardan relativas a la raíz del repositorio, así que un worktree aprovecha lo que
  compiló otro. Los presets ponen delante del compilador lo que diga `DWE_COMPILER_LAUNCHER`, que la imagen define
  como `ccache`: con una imagen anterior, o en Windows sin nada definido, se compila igual, sin caché.
- El proyecto no busca módulos de C++ en los fuentes (`CMAKE_CXX_SCAN_FOR_MODULES` desactivado, [ADR
  0008](0008-headers-antes-que-modulos.md)): cada fuente se preprocesaba dos veces, y `compile_commands.json` dependía
  de ficheros que escribe el build. Ahora clang-tidy solo necesita el `configure`.
- Las comprobaciones tienen **dos niveles**, los dos en `dev.sh` y con el preset `ci` (warnings como errores):
  - `check-commit`, lo que cumple cada commit: configure, build y los tests salvo los lentos (`ctest -LE slow`);
  - `check-full`, lo que cumple la punta de una rama, y lo que pasa la CI: además el formato, todos los tests y
    clang-tidy sin ningún diagnóstico.

  Cada paso deja su log en `build/logs/` (o en `DWE_LOG_DIR`) y falla también si el log del build trae un warning (los
  del enlazador no los cubre `-Werror`) o el de clang-tidy un diagnóstico.
- Los tests **lentos** son los casos que leen el disco del usuario ("the user's disc: ...") o el contenido extraído de
  él con `DWE_CONTENT` ("the user's disc, extracted: ..."). Cada uno es un test de ctest propio con la etiqueta
  `slow`, así que corren en paralelo entre sí; un caso nuevo que el `configure` aún no ha visto corre en
  `<módulo>.disc`, así que nunca se salta.
- Todo usa los núcleos de la máquina (`DWE_JOBS` lo limita): ninja, ctest, clang-format y clang-tidy, que pasa un
  proceso por fichero e imprime la salida de cada uno de una vez. En `check-full`, clang-tidy corre con menos
  prioridad a la vez que los tests, que son casi todo un único caso largo del disco.
- **El juego se ejecuta en el host**, que tiene la GPU y la pantalla. Para que funcione sin instalar nada, el
  ejecutable enlaza libc++ y SDL3 de forma estática; solo necesita el loader de Vulkan y el driver de la GPU.
- El `devcontainer.json` permite abrir el mismo entorno desde VS Code o CLion.

## Consecuencias

- La única dependencia del host es Docker (y el driver de la GPU para jugar).
- El primer `configure` tarda, porque vcpkg compila todas las dependencias; después sale de la caché.
- La CI se aparta en dos cosas para tardar menos sin caché, y ninguna cambia el código que compila:
  - no instala la capa de validación de Vulkan, que es la feature `validation` de `vcpkg.json`, activa por defecto en
    local, porque ningún test dibuja;
  - en Linux usa el triplet `x64-linux-libcxx-release`, que compila cada dependencia una vez en lugar de dos. Ese
    triplet compila libdatachannel con AddressSanitizer, como los tests que la usan: su código de `std::vector` en
    release, sin instrumentar, se mezcla al enlazar con el del motor y escribe en la capacidad que libc++ marca como
    envenenada, y ASan lo da como desbordamiento. En depuración (`x64-linux-libcxx`) no pasa, porque nada se expande
    en línea.
- La caché binaria de vcpkg guarda cada paquete por separado, así que una dependencia nueva solo compila esa.
- La imagen no puede ser más nueva que el sistema donde se juega: el ejecutable enlaza la glibc de la imagen. Por eso
  es Ubuntu 24.04 aunque 26.04 ya trae LLVM 22 en sus repositorios.
- En Windows el contenedor solo sirve para compilar para Linux; el build nativo de Windows llegará con su propio
  preset.
- Desde Linux, el preset `windows-cross` compila y enlaza en el contenedor lo mismo que `windows-ci`, con el
  `clang-cl` y el `lld-link` de la imagen y las librerías de Microsoft que descarga xwin (`dev.sh windows-sdk`) a un
  volumen. Esas librerías exigen aceptar la licencia de Microsoft a quien las descarga, y nunca entran en el
  repositorio ([Preparar el entorno](../getting-started/setup.md#el-build-de-windows-desde-linux)).
- Antes, la comprobación de una rama (formato; build y tests de `linux-clang-debug` y de `ci`; clang-tidy en serie)
  tardaba unos 26 minutos en una máquina de 12 núcleos. `check-full` tarda unos 7 minutos y medio con la caché de
  ccache vacía y unos 5 y medio con ella llena; casi todo es el caso más largo del disco. El build de
  `linux-clang-debug` se deja fuera: tiene las mismas opciones que `ci` salvo `-Werror`, así que no comprobaba nada
  más.
- Un commit de una rama ya compilada en otro worktree pasa `check-commit` en medio minuto, porque casi todos sus
  objetos salen de la caché; antes eran unos 2 minutos y medio de build.
- La CI guarda la caché de ccache (Linux) y la de sccache (Windows) con `actions/cache`. En Windows los objetos llevan
  la información de depuración dentro (`/Z7`), porque un PDB compartido (`/Zi`) no se puede cachear.
- Para compartir la caché entre worktrees, el compilador recibe los fuentes con rutas relativas a la carpeta del build:
  sus errores dicen `../../engine/...`. Y la información de depuración de un objeto que sale de la caché puede nombrar
  la carpeta del worktree que lo compiló primero: los ficheros son idénticos, pero el depurador puede abrirlos desde
  esa carpeta.

## Alternativas descartadas

- **Instalar el toolchain en el host:** rápido de empezar, pero cada máquina acaba distinta.
- **Nix:** reproducible, pero el equipo tendría que aprenderlo.
- **Colima o una VM:** necesita virtualización por hardware, que no todas las máquinas tienen activada.

Para acelerar las comprobaciones se midieron y se descartaron:

- **mold en lugar de lld:** enlazar todos los ejecutables suma unos 5 segundos de CPU en todo el build.
- **Cabeceras precompiladas:** la biblioteca estándar cuesta ~1,1 s por fuente, ~30 % del CPU de un build en frío,
  pero ccache ya evita repetir esos builds, la PCH oculta los `#include` que faltan y clang-tidy volvería a depender
  del build.
- **Unity builds:** 25 ficheros de tests repiten la misma función auxiliar en un namespace anónimo, y unir fuentes
  rompe la granularidad de ccache y de los builds incrementales.
- **Un disco de prueba más pequeño para los tests lentos:** comprueban justo que cada fichero del disco real tiene
  extractor y que el paquete entero valida; un recorte perdería esa cobertura.
- **clang-tidy solo de los ficheros cambiados en cada commit:** un cambio en una cabecera afecta a fuentes que no
  cambian, así que no garantiza nada; la punta ya lo pasa entero.
- **Menos arranques de contenedor:** cada uno cuesta menos de un segundo.
- **Repartir la CI de Linux en varios jobs:** cada job tendría que volver a crear la imagen y restaurar vcpkg, y
  clang-tidy ya corre a la vez que los tests.
