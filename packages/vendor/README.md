# vendor

Build wrappers for external libraries. The code of these libraries is not ours and is not here: vcpkg (`vcpkg.json`)
installs it. This folder only holds the minimum to build them when vcpkg does not ship a CMake package.

- `cgltf/`: builds the two cgltf headers (glTF reading and writing) once into the `dwe_cgltf` library, which `assets`
  and `formats` link.

They are not engine modules: they have no layer or dependency rules, no `pkg_` target of their own, and
`scripts/dev.sh packages` does not list them. A change here affects all the packages (`scripts/dev.sh affected`).
Details in the [repository layout](https://indie-modding-games.github.io/dw-engine/architecture/repository-layout).
