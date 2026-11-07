# Añadir un mapa nuevo

Un paquete de mod puede traer mapas que el juego original no tiene y conectarlos con los demás por sus salidas. El
editor de mapas (`dwe-mapper`, [módulo](../modulos/apps/mapper.md#nuevo-mapa)) los crea en el paquete destino: el
paquete base no se toca. Qué ficheros son y qué pide el juego: [Mapas que añade un
paquete](../formats/mapas.md#mapas-que-añade-un-paquete).

## 1. Abrir el contenido y elegir el destino

```sh
scripts/mapper.sh run --content <carpeta de contenido> --mods <carpeta de mods>
```

En «Archivo → Destino» elige el paquete de mod donde irá el mapa, o crea uno con «Nuevo paquete de mod…» (igual que
en [Editar un mapa con el mapper](mapas.md#2-elegir-dónde-se-guarda)).

## 2. Crear el mapa

«Mapas → Nuevo mapa…»:

- **Id:** el nombre dentro del paquete (`a-z`, `0-9` y `_`). El mapa será `<paquete>:<id>`.
- **Nombre:** lo que el juego enseña al entrar. Se escribe en español y, hasta que alguien lo traduzca, igual en
  inglés.
- **Zona:** el nombre de zona con el que se agrupa en la lista.
- **Plantilla:**
  - **Copia de un mapa:** parte de un mapa que ya existe (su cámara, su colisión y sus NPC). Sus imágenes no se copian:
    los datos nuevos dicen `assets_from: <mapa>` y el juego las toma de allí. Marca «copiar sus imágenes» si vas a
    cambiar el fondo. Las salidas del original se vacían, salvo que marques conservarlas.
  - **En blanco:** un suelo libre del tamaño que elijas (en fichas de 128 px, hasta 35 fichas) visto desde arriba. El
    fondo es un PNG tuyo, que se copia en el paquete (tiene que medir fichas enteras y tener como mucho 256 colores),
    o unos cuadros grises para empezar.
- **Conectar con:** una salida de otro mapa (por ejemplo, una que no use la ciudad) y la salida del mapa nuevo por la
  que se vuelve. Las dos quedan conectadas. Si una salida no tenía celdas, el editor le pone una puerta de dos celdas
  y la llegada unas celdas hacia dentro; puedes moverlas después.

«Crear» escribe los ficheros, vuelve a leer el contenido y abre el mapa nuevo. La salida del otro mapa queda escrita
como parche en el mismo paquete.

## 3. Ajustarlo

Edita el mapa nuevo como cualquier otro ([Editar](mapas.md#3-editar)): la colisión, las salidas, los NPC. Ctrl+S lo
guarda entero en su propio `data.yaml` (es del paquete; no hace falta parche). Para conectar más salidas abre el
**Mundo** (tecla M) y arrastra con Mayús de un mapa a otro: el grafo enseña también si algún camino quedó roto, solo
de ida o con la llegada dentro de una pared.

«Mapas → Renombrar mapa…» y «Mapas → Borrar mapa…» solo valen para los mapas que el paquete añadió. Borrar pide que
antes ningún camino lleve al mapa.

## 4. Comprobarlo y jugarlo

```sh
dwx validate --with <carpeta de contenido>/base <carpeta de mods>/<id del paquete>
```

Da como error un mapa sin entrada, una entrada sin datos o un mapa sin fondo. Después arranca el juego con el mismo
`--mods`; para ir directo al mapa nuevo:

```sh
dwe-game --mods <carpeta de mods> --new-game --start <paquete>:<id>,<salida>
```

Lo que todavía no hace el juego con un mapa añadido: no tiene música propia, el transporte en pájaro no lleva a él, la
historia no pone sus NPC (no tiene secciones de historia) y una partida guardada en él no carga si el mod no está
montado.
