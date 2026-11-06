# content

Cómo montan los paquetes las aplicaciones: la carpeta de contenido, los paquetes del equipo y la instalación desde el
disco del jugador.

- Carpeta: `packages/content`; objetivos de CMake: `dwe::content` (capa `apps`).
- Depende de: `assets`, `core`, `platform`.
- Estructura: `CMakeLists.txt` (el paquete y sus dependencias, con `dwe_add_module`), `include/` (lo público, como
  `<dwe/...>`), `src/` y `tests/`.
- Documentación: [docs/modulos/apps/content.md](../../docs/modulos/apps/content.md).

Compilar y probar solo este paquete:

```sh
scripts/dev.sh build --filter content
scripts/dev.sh test --filter content       # sus tests (etiqueta de ctest content)
scripts/dev.sh test --filter ...content    # y los de todo lo que depende de él
```
