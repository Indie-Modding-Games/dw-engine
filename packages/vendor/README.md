# vendor

Envoltorios de build para librerías externas. El código de estas librerías no es nuestro ni está aquí: lo instala
vcpkg (`vcpkg.json`). Esta carpeta solo tiene lo mínimo para compilarlas cuando vcpkg no trae un paquete de CMake.

- `cgltf/`: compila una vez las dos cabeceras de cgltf (lectura y escritura de glTF) en la librería `dwe_cgltf`, que
  enlazan `assets` y `formats`.

No son módulos del motor: no tienen capa ni reglas de dependencias, ni un target `pkg_` propio, y
`scripts/dev.sh packages` no los lista. Un cambio aquí afecta a todos los paquetes (`scripts/dev.sh affected`).
Detalle en la [estructura del repositorio](https://indie-modding-games.github.io/dw-engine/architecture/repository-layout).
