# platform

Ventana, eventos y mandos sobre SDL3 ([ADR 0003](../../adr/0003-vulkan-y-sdl3.md)). Traduce los eventos de SDL a tipos propios (`dwe::platform::Event`), así que el resto del motor no ve SDL.

`process.hpp` ejecuta otros programas (`runProcess`: sin shell, con su salida estándar recogida y sus errores a los
del programa que lo llama) y dice dónde está el ejecutable que corre (`executableFolder`) y cómo se llama un programa
en el sistema (`executableFileName`). No necesitan una `Platform`. El juego lo usa para pedirle a `dwx` la imagen del
disco de los assets de desarrollo.

En Windows, cada programa del proyecto (y cada ejecutable de tests) lleva incrustado el manifiesto
`cmake/windows/utf8.manifest` (`dwe_use_utf8` en `cmake/DweModule.cmake`), que pide UTF-8 como página de códigos.
Así `argv`, las rutas hechas de `std::string` y las funciones de ficheros del CRT van en UTF-8 como en Linux, y una
ruta bajo `%APPDATA%` con un nombre de usuario fuera de la página ANSI funciona. **Necesita Windows 10 1903 o
posterior**; en uno anterior el manifiesto se ignora. El test «on Windows a program's char strings are UTF-8» lo
comprueba en la CI de Windows.
