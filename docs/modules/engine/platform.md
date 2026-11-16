# platform

Window, events and gamepads on SDL3 ([ADR 0003](../../adr/0003-vulkan-and-sdl3.md)). It translates SDL events into its own types (`dwe::platform::Event`), so the rest of the engine does not see SDL.
`Window::setTextInput` turns on the window's text input; while it is on, what the
keyboard types arrives as `TextTyped` (UTF-8, according to the keyboard layout, not its keys). The game turns it on only
while a name box is open.

`process.hpp` runs other programs (`runProcess`: without a shell, with its standard output collected and its errors going to those
of the calling program) and says where the running executable is (`executableFolder`) and what a program is called
on the system (`executableFileName`). They do not need a `Platform`. The game uses it to ask `dwx` for the disc
image of the development assets.

On Windows, each program of the project (and each test executable) has embedded the manifest
`tooling/cmake/windows/utf8.manifest` (`dwe_use_utf8` in `tooling/cmake/DweModule.cmake`), which asks for UTF-8 as the code page.
This way `argv`, paths made of `std::string` and the CRT's file functions work in UTF-8 as on Linux, and a
path under `%APPDATA%` with a user name outside the ANSI page works. **It needs Windows 10 1903 or
later**; on an earlier one the manifest is ignored. The test "on Windows a program's char strings are UTF-8" checks it
in the Windows CI.
