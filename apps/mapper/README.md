# mapper

El editor de mapas de `dwe` (`dwe-mapper`): abre la carpeta de contenido como el juego y dibuja sobre el fondo de cada
mapa sus colisiones, salidas, personajes, puertas y baño. Edita la colisión (pincel, rectángulo, relleno,
cuentagotas), las salidas (destino, llegada, celdas) y los NPC (posición, orientación, campos, ruta), con deshacer y
rehacer, y guarda lo cambiado como `map/<id>/patch.yaml` en un paquete de mod (o el mapa entero). Lee los mapas con los
parches de los paquetes y las bandas de disparo de `field/triggers` igual que el juego, y se conecta a un `dwe-game` en
marcha: sigue su mapa, enseña dónde están sus personajes, le manda el cambio sin guardar como vista previa, lleva al
jugador a un punto o a una salida, lo pausa, recarga su mapa tras guardar y lee y escribe el estado de la historia. Es una app
de escritorio en Rust (egui), aparte de CMake.

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

Para modders: [Editar un mapa con el mapper](https://indie-modding-games.github.io/dw-engine/modding/mapas).
