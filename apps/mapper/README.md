# mapper

El editor de mapas de `dwe` (`dwe-mapper`): por ahora un visor de solo lectura que abre la carpeta de contenido como el
juego y dibuja sobre el fondo de cada mapa sus colisiones, salidas, personajes, puertas y baño. Lee los mapas con los
parches de los paquetes (`map/<id>/patch`) y las bandas de disparo de `field/triggers` igual que el juego, y se conecta
a un `dwe-game` en marcha para recargar su mapa, encender su capa de depuración y leer el estado de la historia. Es
una app de escritorio en Rust (egui), aparte de CMake.

- Carpeta: `apps/mapper` (el programa, `crates/mapper-core` y `crates/mapper-live`); usa los crates
  `packages/rust/dwe-pack` y `packages/rust/dwe-map`, que están en el mismo workspace (`Cargo.toml` de aquí).
- Depende de: ningún paquete de CMake.
- Documentación: [modulos/apps/mapper](https://indie-modding-games.github.io/dw-engine/modulos/apps/mapper).

Compilar, probar y abrirlo (en la imagen del lanzador, `scripts/mapper.sh`):

```sh
scripts/dev.sh test --filter mapper
scripts/mapper.sh run --content <carpeta de contenido>
scripts/mapper.sh run --content <carpeta de contenido> --mods <carpeta de mods> --map <id>
```
