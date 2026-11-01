# Traducir un cartel de un mapa

Los carteles pintados en los mapas se escriben otra vez en el idioma del jugador al cargar el mapa (módulo
[signs](../modulos/engine/signs.md)). Para un cartel nuevo hacen falta dos cosas: su texto en las tablas de idioma y un
overlay que diga dónde está y cómo se dibuja. Ninguna imagen del juego entra en git: todo se pinta en tiempo de
ejecución desde las texturas que cada jugador extrae de su disco.

## 1. Encontrar el cartel

```sh
dwe-game --content <carpeta> --dump-signs gcan09 --shots _work/tmp/carteles --lang es
```

Escribe en la carpeta, para cada mapa:

| Fichero | Qué es |
|---|---|
| `<mapa>-background.png` | El fondo, en píxeles de la imagen (las coordenadas de `region`) |
| `<mapa>-background-grid.png` | El mismo con una rejilla cada 16 píxeles (más fuerte cada 64) y las regiones de los carteles en cian |
| `<mapa>-page<bits>-<n>-clut<c>.png` | Cada página de textura de los sprites con cada paleta que usan (con su `-grid`) |
| `<mapa>-sprites.txt` | Los sprites (página, paleta y texels) y las instancias que los ponen sobre el fondo |
| `<mapa>-signs.txt` | Los carteles del overlay y un `place("player x z 0")` para ir a verlos en el juego |
| `<mapa>-<idioma>-background.png` | El fondo con los carteles ya pintados, para comprobar |

Abre la imagen con rejilla en cualquier editor y apunta el rectángulo del texto y, si el cartel está en perspectiva,
sus cuatro esquinas. La mayoría de carteles están en el fondo; si no aparece, búscalo en las páginas de textura.

Si en el juego el cartel sigue en el idioma viejo, un sprite lo tapa: busca en `<mapa>-sprites.txt` la instancia
cuyo rectángulo (`at` más el tamaño del sprite) cubre el cartel. Con paleta `-1` el motor ya copia el cartel en él;
con otra paleta, añade un cartel con `target: {page: <n>, bits: 8, clut: <c>}` y las mismas coordenadas movidas a la
página (el texel del sprite más la distancia desde `at`).

## 2. Escribir el texto

En `content/packs/game/localization/en/signs.yaml` y `.../es/signs.yaml`, una clave por cartel (o por línea, si las
líneas tienen colores distintos):

```yaml
gcan09_shop: "TIENDA\nMONOCROMO"
```

`\n` parte la línea. Un cartel que solo existe en español necesita igual la clave en inglés, con el texto del original.

## 3. Escribir el overlay

`content/packs/game/signs/<mapa>.yaml` para todos los idiomas; `content/packs/es-translation/signs/es/<mapa>.yaml`
para uno solo (un cartel que en inglés ya se lee bien, o uno que en español se dibuja distinto, con el mismo `id`).

```yaml
signs:
  - id: shop
    region: [544, 69, 79, 25]
    clear:
      mode: diffuse
      quad: [[545, 69], [623, 75.5], [622, 94], [543, 89]]
    corners: [[547, 71], [621, 77], [620, 92], [545, 87]]
    text: signs.gcan09_shop
    size: 9
    color: "#302828"
```

- `region` es lo único que el cartel puede tocar. `corners` pone el texto en perspectiva; sin ellas el texto va
  derecho en la región (con `rotation` y `skew` si hace falta).
- `clear: diffuse` es el borrado que mejor guarda el tablero: quita lo que no se parece al color del tablero y lo
  rellena desde alrededor. Si el tablero tiene varios colores, da el suyo con `color` y ajusta `tolerance`; si el
  borrado se come el marco, limítalo con `quad`.
- Copia los colores del original (el color de las letras, el contorno, el halo de un neón) y usa `colors: region`
  para que solo salgan colores que el cartel ya tenía.
- El texto encoge solo hasta caber (`fit`); si queda muy pequeño, busca una traducción más corta.

Todos los campos están en el [módulo signs](../modulos/engine/signs.md) y en
`content/schemas/sign_overlay.schema.json`.

## 4. Comprobar

1. Vuelve a lanzar `--dump-signs` y compara `<mapa>-background.png` con `<mapa>-<idioma>-background.png`.
2. En el juego: `--start <mapa> --lang es` y, en la consola, el `place(...)` de `<mapa>-signs.txt`. La opción
   **Carteles traducidos** del menú de pausa enseña el original para comparar.
3. La ruta `tests/autotest/route-signs.yaml` hace las dos capturas sola (añade un paso `script: place(...)` en una
   copia si el cartel no se ve al empezar):

   ```sh
   dwe-game --content <carpeta> --lang es --start gcan09 --autotest tests/autotest/route-signs.yaml --shots _work/tmp/carteles/gcan09-es
   ```

4. `dwx validate` comprueba el overlay contra el esquema.

Las capturas y los volcados se quedan en `_work/tmp`: nunca se suben.
