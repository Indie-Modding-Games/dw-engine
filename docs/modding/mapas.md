# Editar un mapa con el mapper

El editor de mapas (`dwe-mapper`, [módulo](../modulos/apps/mapper.md)) cambia la colisión, las salidas y los NPC de
un mapa y guarda solo lo cambiado como un **parche** (`map/<id>/patch.yaml`) en un paquete de mod. El paquete base no
se toca nunca: el juego aplica el parche encima al cargar el mapa, igual que el editor
([parches de mapa](../formats/mapas.md#parches)).

## 1. Abrir el contenido con tus mods

```sh
scripts/mapper.sh run --content <carpeta de contenido> --mods <carpeta de mods>
```

`--mods` es la misma carpeta (o paquete suelto) que se le da al juego. Sin ella el editor no tiene dónde guardar
hasta que crees un paquete nuevo. Elige el mapa en la lista de la izquierda.

## 2. Elegir dónde se guarda

«Archivo → Destino» lista los paquetes que el editor puede escribir: los mods montados y, en un checkout del
repositorio, el paquete `game` del equipo. «Nuevo paquete de mod…» crea uno: una carpeta de mods y un id (`a-z`, `0-9`
y `_`). El editor escribe su `pack.yaml` en la capa `mods`, lo monta y lo deja como destino; la próxima vez lo
recuerda. La barra de estado de abajo dice siempre el destino.

## 3. Editar

- **Colisión:** elige el valor (pared, libre, un disparador de paso o de confirmación, una salida, el baño) y pinta
  con el pincel (B), el rectángulo (R) o el relleno (F). El cuentagotas (I, o Alt+clic) toma el valor de una celda.
- **Salidas:** con «Seleccionar» (V) haz clic en las celdas de una salida o en su flecha de llegada. En el inspector
  cambia el mapa de destino (con filtro), la salida de destino, la llegada y la orientación; arrastra la flecha para
  mover la llegada y con Mayús para girarla. «Pintar sus celdas» deja el pincel listo para las celdas de esa salida.
- **NPC:** arrastra un NPC para moverlo, con Mayús para girarlo (o Q y E). En el inspector están sus campos y su ruta:
  añade puntos («Añadir punto» o W bajo el cursor), arrástralos, cámbiales el tipo (esperar, andar, correr), súbelos,
  bájalos o quítalos (Supr).

Ctrl+Z deshace y Ctrl+Y rehace; un trazo o un arrastre es un solo paso. «Editar → Atajos de teclado» tiene la lista.

## 4. Guardar

Ctrl+S («Guardar (parche)») comprueba el mapa y escribe el parche. Si algo no vale (una salida a un mapa que no existe,
una salida con celdas que no lleva a ningún sitio, una ruta de más de 8 partes, un destino debajo del paquete que trae
el mapa), sale en «Problemas al guardar» y no se escribe nada. Un parche queda así:

```yaml
warps:
- exit: 0
  target_exit: 3
npcs:
- index: 1
  position:
    x: 700
    y: 0
    z: -1800
collision:
  cells:
  - column: 29
    row: 39
    width: 3
    height: 3
    value: 128
```

Solo están las salidas y los NPC que cambiaron, con sus campos cambiados; la colisión va por rectángulos de celdas o,
si el cambio es grande, como la rejilla entera. Guardar otra vez rehace el parche entero contra el mapa de debajo, así
que deshacer un cambio y guardar lo quita del fichero. «Guardar mapa completo» escribe en cambio el mapa entero
(`map/<id>/data.yaml`) en el paquete.

## 5. Verlo en el juego

Arranca el juego con el mismo `--mods` y con el control remoto (`--control tcp:<puerto>`, ver
[control remoto](../guides/control-remoto.md#edición-de-mapas-en-vivo)). En el panel «En vivo» del editor escribe
`tcp:<puerto>` y pulsa «Conectar». Con «Recargar al guardar» encendido, cada Ctrl+S hace que el juego relea los
paquetes y vuelva a entrar en el mapa donde está el jugador. «Capa de depuración del juego» enseña en el juego las
celdas y las salidas tal como las ve el motor.

Sin guardar: con «Vista previa en vivo» el juego enseña el cambio en cuanto lo haces (solo en memoria; no toca el
paquete ni la partida). «Seguir al juego» abre el mapa al que entra el juego, la capa «Juego» enseña dónde están el
jugador y los NPC, y con el botón derecho sobre el mapa puedes llevar al jugador a un punto o probar una salida. Más
en [mapper → En vivo](../modulos/apps/mapper.md#en-vivo).

Para comprobar el paquete sin abrir el juego:

```sh
dwx validate <carpeta de mods>/<id del paquete>
```

## Poner un baño en un mapa

Con la opción «Baños extra», cualquier mapa puede tener una caseta de baño: una línea en `field/extra_toilets.yaml` del
paquete (mapa, posición y hacia dónde mira) da la caseta dibujada, sus casillas sólidas, las casillas de delante que
llevan al compañero al baño y la rutina del baño de siempre. Los detalles y un ejemplo están en
[Baños extra](../modulos/apps/game.md#banos-extra).
