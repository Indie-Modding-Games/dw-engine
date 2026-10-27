# DWE Studio

DWE Studio es un banco de trabajo de escritorio separado del juego. Reúne un editor de código, el árbol del motor, una terminal de comandos y controles para compilar y ejecutar `dwe-game`.

## Ejecutar en desarrollo

Requiere Node.js y npm. Desde esta carpeta:

```sh
npm install
npm run dev
```

Para crear el bundle de la app:

```sh
npm run build
npm start
```

Por defecto abre la raíz del repositorio que contiene `apps/studio`. Se puede seleccionar otra raíz DWE con el botón de carpeta; debe tener `scripts/dev.sh`.

## Flujo de trabajo

- El editor abre y guarda archivos del repositorio. `Ctrl/Cmd+P` busca archivos y `Ctrl/Cmd+S` guarda.
- El explorador observa los cambios en disco. Los cambios guardados desde otra herramienta, como Codex o Claude Code, aparecen en «Cambios recientes» y actualizan las pestañas que no tienen cambios locales.
- El botón de compilar ejecuta `scripts/dev.sh build`; «Ejecutar» compila `dwe-game` y `dwx`, actualiza la caché con `disc_image` y crea una raíz de contenido de ejecución con esa base y los paquetes del proyecto. No modifica `content/base`. Después inicia el juego con `--tuning --dev-assets` y abre el overlay de depuración: FPS, entradas, consola Lua y panel de estado. `F1` lo oculta o vuelve a abrir. La salida de compilación y del juego aparece en el panel inferior. La ruta del disco se configura en `dev-assets.yaml` dentro del proyecto o en la carpeta de datos del usuario.
- La consola mantiene una sesión `bash` desde la raíz seleccionada; puedes cambiar de directorio y los comandos conservan su estado.
- El explorador `DW_DECOMP` abre el árbol local decompilado como referencia de solo lectura. Busca `DWE_DECOMP` o, por defecto, `~/code/dw_decomp`. DWE Studio no copia ni modifica ese repositorio.
- La pestaña «Comparar» muestra dos ventanas lado a lado: DuckStation con el disco del usuario y DWE Engine con la ejecución del proyecto. Studio actualiza las ventanas al abrirse y mientras inicia el juego; reconoce títulos como `dwe`, `dwe-game`, `dw1` y `digimon world`. «Buscar ventanas» fuerza una actualización de la lista. En Linux/X11, haz clic en una pantalla para darle el foco a ese juego; el teclado y el mando se dirigen a la ventana elegida mientras la captura sigue visible en Studio.
- Si detecta ambas ventanas al arrancar y el loop no se detuvo antes, DWE Studio las vuelve a seleccionar por su título, inicia las capturas y reanuda el loop automáticamente. Cada 20 segundos guarda en `.dwe-studio/observer/` tres fotogramas secuenciales por juego (medio segundo entre fotogramas) y los logs recientes; `runtime.json` registra ventanas, capturas y errores para que Codex diagnostique fallos aunque todavía no haya muestras. El chat Codex de este proyecto revisa esos archivos con su monitor recurrente y puede escribir secuencias breves de botones en `control/pending.json`, que Studio envía a las ventanas seleccionadas con X11. «Enviar muestra ahora» fuerza una captura; «Detener loop» apaga el envío y elimina cualquier secuencia pendiente. `control/result.json` registra los botones ejecutados.
- Al cerrar o recargar DWE Studio, las partidas que Studio lanzó permanecen abiertas. Sus PID, argumentos y disco elegido se guardan en los datos de usuario; al abrir de nuevo, Studio verifica esos procesos, vuelve a adjuntar sus logs y detecta sus ventanas para reanudar la captura. Los botones de detener solo terminan procesos que Studio lanzó. Si un juego se inició fuera de Studio, se adjunta a su ventana sin lanzar una copia adicional.

Los scripts Lua del juego se leen desde el árbol fuente y se pueden recargar al editarse. Los cambios en C++ necesitan una compilación nueva; la vista del juego en esta primera entrega es un indicador de la ventana nativa, todavía no una captura de vídeo incrustada.

## Comparar el original y el remake

DuckStation es la referencia del juego original de PS1; DWE es el remake. Cada muestra conserva los fotogramas de ambas ventanas y una imagen conjunta `comparison.jpg`, con PS1 a la izquierda y DWE a la derecha, manteniendo las proporciones de cada captura.

Después de una secuencia en `control/pending.json`, Studio captura automáticamente tres fotogramas de cada juego y enlaza la muestra con el identificador del control. Espera `control/result.json → observation.status: "captured"` y abre `observation.sampleId` antes de decidir la siguiente entrada. Mientras esa captura está pendiente, la cola no ejecuta otra secuencia. Si falla o tarda más de 15 segundos, el loop se detiene y registra el motivo; revisa las ventanas antes de reactivarlo. Las capturas periódicas cada 20 segundos continúan como seguimiento.

Para revisar animaciones, usa **Capturar movimiento (2 s)**. Intenta obtener 40 imágenes por juego a intervalos de 50 ms, con un ancho máximo de 640 píxeles. `sample.json → frameTimes` guarda los tiempos reales: la carga del escritorio puede alterar la cadencia. Los archivos `motion-duckstation.jpg` y `motion-engine.jpg` resumen doce posiciones de la secuencia con su tiempo; los 40 fotogramas completos quedan disponibles para inspección detallada. La muestra de movimiento admite hasta 16 MB; las muestras normales, hasta 8 MB.

Antes de atribuir una diferencia al remake, comprueba el mismo diálogo, menú o fase de combate en ambas ventanas. Para un movimiento irregular, sigue la posición y orientación del personaje a lo largo de la secuencia y compara la duración de sus ciclos. La imagen conjunta y los resúmenes temporales son evidencia para la revisión; el observador todavía no clasifica por sí solo los fallos ni modifica el código del juego.

## Límites de esta entrega

La app no incorpora los chats de Codex o Claude. Los agentes pueden modificar el mismo árbol de archivos y DWE Studio refleja los cambios guardados; Claude Code solo aparece como disponible si su CLI está instalada. La consola mantiene el shell y muestra su salida, aunque todavía no ofrece una terminal PTY para programas interactivos de pantalla completa.

La ejecución de DWE activa los eventos SDL del mando cuando la ventana está en segundo plano, por lo que puedes jugar desde DuckStation con un mando físico y ambos procesos reciben sus entradas. DuckStation debe tener habilitada la entrada SDL y «Disable Background Input» apagado. El teclado solo controla la ventana enfocada. Las capturas son de video: DWE Studio aún no transmite el juego a un agente de IA automáticamente ni aplica cambios de código a partir de cada frame.

Algunos escritorios Linux con PipeWire solo entregan una ventana en el listado de Electron; en ese caso no es posible capturar los dos procesos simultáneamente desde DWE Studio y depende del compositor.

La depuración por línea y la inspección del estado del juego requieren un protocolo de depuración del runtime de DWE. La estructura de paneles y flujo de ejecución toma ideas de VS Code y Godot; no incluye ni copia sus aplicaciones completas.

La ventana usa aislamiento de contexto, preload acotado y sandbox del renderer. Monaco Editor se distribuye bajo MIT; las dependencias se fijan en `package-lock.json`.

## Revisión por componentes e historial

El flujo actual revisa una pieza por vez (primero el menú), usando la decompilación como referencia de comportamiento de solo lectura y DuckStation como evidencia visible. La automatización de recorridos completos se retiró; las acciones y las capturas se solicitan para la pieza activa.

Antes de analizar o recapturar, consulta `odd/tasks/comparacion-por-componentes.md` y `.dwe-studio/observer/processed.json`. Recaptura solo por un cambio de implementación, una duda concreta o una validación pendiente. No repitas secuencias ya explicadas.

El manifiesto conserva 30 muestras recientes. Al salir de esa ventana, las muestras registradas en `processed.json → processed` y las vinculadas a una acción se trasladan a `.dwe-studio/observer/history/<id>/`, conservando imágenes, tiempos, logs y metadatos. Las demás capturas periódicas se eliminan. Estos archivos son locales e ignorados por Git; no incorporar imágenes ni datos del disco original al repositorio. Un registro de revisiones inválido detiene la limpieza sin borrar evidencia. Los archivos eliminados por versiones anteriores no se recuperan automáticamente.
