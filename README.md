# dwe

**dwe is a native C++26 reimplementation of a classic late-1990s creature-raising RPG, built for PC and modding.**
It keeps the original game's look and behavior on a new engine, with editable content packs and Lua scripting. The
whole original story is playable from the title screen to the credits; the project is in closed beta. `dwe` is a
working name.

No original assets, code or data are distributed. Each player supplies their own disc, and the `dwx` tool converts
it locally. See the [asset policy](https://indie-modding-games.github.io/dw-engine/legal/assets). This is an
independent project, unaffiliated with and not endorsed by the original rights holders.

## Play the beta

Install the launcher from the [releases](https://github.com/Indie-Modding-Games/dw-engine/releases) page, enter your
beta key and pick your disc. The launcher installs and updates the game for Linux, Windows and macOS. Details:
[Linux beta](https://indie-modding-games.github.io/dw-engine/guides/beta-linux),
[Windows and macOS beta](https://indie-modding-games.github.io/dw-engine/guides/beta-windows-macos).

## Develop

On Linux with Git, Docker and a Vulkan 1.3 driver:

```bash
scripts/dev.sh image && scripts/dev.sh configure && scripts/dev.sh build
scripts/dev.sh run --content /path/to/extracted-content
```

[Setup](https://indie-modding-games.github.io/dw-engine/getting-started/setup) and
[running the game](https://indie-modding-games.github.io/dw-engine/getting-started/jugar) explain the environment and
how to extract your disc.

## Documentation

The documentation (in Spanish) is at <https://indie-modding-games.github.io/dw-engine/>: architecture, modules,
formats, guides and the [roadmap](https://indie-modding-games.github.io/dw-engine/roadmap). To contribute, read
[how to contribute](https://indie-modding-games.github.io/dw-engine/contribuir).

## License

The code is released under the [MIT License](LICENSE). Third-party dependencies keep their own licenses; original
game content is governed by the [asset policy](https://indie-modding-games.github.io/dw-engine/legal/assets).
