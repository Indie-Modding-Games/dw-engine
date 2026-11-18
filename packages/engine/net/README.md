# net

The network between two machines: WebRTC with one reliable lane and one without guarantees, lockstep and the signaling
client.

- Folder: `packages/engine/net`; CMake targets: `dwe::net` (`base` layer).
- Depends on: `core`; from outside, libdatachannel (WebRTC and WebSocket, with Mbed TLS) and nlohmann-json.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/engine/net](https://indie-modding-games.github.io/dw-engine/modules/engine/net).

Build and test only this package:

```sh
scripts/dev.sh build --filter net
scripts/dev.sh test --filter net       # its tests (ctest label net)
scripts/dev.sh test --filter ...net    # and those of everything that depends on it
```
