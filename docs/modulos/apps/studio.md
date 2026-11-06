# studio

DWE Studio (`apps/studio`): un banco de trabajo de escritorio para desarrollar el motor, aparte del juego y del
lanzador. Junta un editor de código, el árbol del repositorio, una terminal y los botones para compilar y arrancar
`dwe-game`, y una pestaña para ver el original y el remake lado a lado. Es una herramienta de desarrollo en pruebas:
no se publica ni la necesita nadie para compilar o jugar. Uso detallado, límites y el flujo de comparación: el
[README de Studio](https://github.com/Indie-Modding-Games/dw-engine/blob/main/apps/studio/README.md).

## Pila

Electron con `electron-vite` y el editor Monaco; `chokidar` vigila el disco. No es un módulo de CMake: se instala y se
arranca con npm desde `apps/studio`:

```sh
npm install
npm run dev          # desarrollo; npm run build && npm start para el bundle
```

`scripts/build-native-input.mjs` compila antes dos programas pequeños en C para Linux/X11 (`src/main/x11_input.c`,
`x11_focus.c`) que dan el foco y envían pulsaciones a la ventana de un juego.

| Carpeta | Qué tiene |
|---|---|
| `src/main/` | El proceso principal: el proyecto (árbol, leer y escribir ficheros), los procesos (compilar, arrancar, parar), la terminal, las capturas de ventanas y el observador |
| `src/preload/` | El puente acotado entre la ventana y el proceso principal (aislamiento de contexto, renderer en sandbox) |
| `src/renderer/` | La interfaz: editor, explorador, paneles y la pestaña «Comparar» |

## Qué hace

- **Proyecto:** abre la raíz del repositorio (o cualquier otra con `scripts/dev.sh`), edita y guarda ficheros, busca
  con `Ctrl/Cmd+P` y refleja los cambios que otra herramienta guarda en disco.
- **Compilar y ejecutar:** «Compilar» es `scripts/dev.sh build`. «Ejecutar» es `scripts/studio-run.sh`: compila
  `dwe-game` y `dwx`, actualiza la caché del disco que dice `disc_image` en `dev-assets.yaml`, monta una raíz de
  contenido con esa base y los paquetes del repositorio (sin tocar `content/base`) y arranca el juego con
  `--tuning --dev-assets` y el overlay de depuración ([assets de desarrollo](../../architecture/assets-de-desarrollo.md)).
- **Referencia:** abre en solo lectura la copia local del decomp (`DWE_DECOMP`); nunca la copia ni la cambia.
- **Comparar:** captura la ventana de un emulador con el disco del jugador y la de `dwe-game`, una junto a otra. Cada
  20 s guarda muestras (fotogramas y logs) en `.dwe-studio/observer/`, que no entra en git, y puede enviar a las
  ventanas secuencias cortas de botones escritas en `control/pending.json`. «Capturar movimiento» guarda 2 s de
  fotogramas de cada juego para comparar animaciones.

Las partidas que lanzó siguen abiertas al cerrar Studio; al volver, las encuentra por su PID y vuelve a adjuntar sus
logs. Solo para procesos que lanzó él.

## Límites

- La captura y el envío de botones a otra ventana solo funcionan en Linux con X11.
- La terminal es un shell sin PTY: no sirve para programas interactivos de pantalla completa.
- No depura por línea ni inspecciona el estado del juego; para eso están el [control remoto](../../guides/control-remoto.md)
  y la [traza de estado](../../guides/traza-de-estado.md).
- No tiene tests automáticos.

`apps/studio-vscode/index.html` es la maqueta en HTML de la interfaz con la que empezó (prueba de concepto).
