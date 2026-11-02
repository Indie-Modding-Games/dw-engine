# Ejecutar el juego

El flujo habitual es **compilar → extraer tu disco una vez → arrancar con esa carpeta de contenido**. El juego nativo se ejecuta en tu máquina; Docker proporciona el entorno de compilación. No necesitas el repositorio privado de instrucciones ni el decomp para ejecutar el motor.

El proyecto sigue en desarrollo: poder arrancar o abrir una demo no significa que la campaña completa y todos sus menús estén terminados.

## 1. Preparar y compilar

En Linux necesitas Git, Docker funcionando y un controlador gráfico compatible con Vulkan 1.3. Desde la raíz del repositorio:

```bash
scripts/dev.sh image
scripts/dev.sh configure
scripts/dev.sh build
```

La primera configuración compila las dependencias y puede tardar. Los detalles del entorno y del preset están en [Preparar el entorno](setup.md).

## 2. Preparar el contenido de tu disco

Si ya tienes una carpeta extraída con `base/pack.yaml`, pasa al siguiente apartado y reutilízala. No hace falta extraerla en cada arranque.

Para una extracción nueva, utiliza tu propia imagen de una versión reconocida. Es preferible conservar el par CUE/BIN completo: una ISO de sectores de datos puede perder información necesaria para las películas. Mantén el disco fuera del repositorio.

Abre el entorno con la carpeta del disco montada en modo de solo lectura. Sustituye la ruta por una ruta absoluta real:

```bash
DWE_DISC="/ruta/absoluta/a/mi-disco.cue" scripts/dev.sh shell
```

Dentro de esa terminal del contenedor, ejecuta:

```bash
build/linux-clang-debug/apps/dwx/dwx extract --out build/local-content/base "$DWE_DISC"
exit
```

Estos ejemplos usan el preset predeterminado `linux-clang-debug`. Si compilaste otro, sustituye su nombre en la ruta del binario. La salida será una carpeta de contenido con esta estructura:

```text
build/local-content/
└── base/
    ├── pack.yaml
    ├── data/
    ├── digimon/
    ├── map/
    └── ...
```

Elige una salida nueva para tu primera extracción; no uses `--force` sobre una carpeta con archivos propios. Consulta [dwx extract](../formats/extract.md) para las reglas de salida y el informe de cobertura. No incorpores el contenido extraído a Git.

## 3. Arrancar el juego

De nuevo en la terminal normal de tu máquina, desde la raíz del repositorio:

```bash
scripts/dev.sh run --content build/local-content
```

`--content` apunta a la carpeta que **contiene** el paquete `base`, no a `model.glb` ni a un mapa individual. El paquete `game` del proyecto aporta sus reglas y menús; sus dependencias deben poder resolverse en el contenido montado.

Si ya tienes contenido en otra ubicación:

```bash
scripts/dev.sh run --content /ruta/a/mi-contenido
```

Opciones útiles:

```bash
# Español y aspecto clásico con fuente original
scripts/dev.sh run --content build/local-content --lang es --ui-skin original --text-font original

# Inglés
scripts/dev.sh run --content build/local-content --lang en

# Tamaño de ventana
scripts/dev.sh run --content build/local-content --size 1280x720
```

### Jugar la última versión de main con un comando

Cuando ya tienes el contenido preparado en `build/studio-content` (DWE Studio lo deja ahí con
`scripts/studio-run.sh`), `scripts/dwe-play` hace todo de una vez:

```bash
scripts/dwe-play
```

1. Comprueba que el checkout está en `main` y sin cambios; si no, se niega y lo dice.
2. Si ya hay un `dwe-game` abierto desde ese checkout, lo dice y sale sin lanzar otro.
3. `git pull --ff-only`.
4. Compila `dwe-game` en Docker con `scripts/dev.sh build --target game` sobre el directorio de siempre
   (`build/linux-clang-debug`, o el de `PRESET`).
5. Copia el paquete `game` del repositorio a `build/studio-content/packs/game` (`rsync --delete-after`). El juego lee
   los paquetes de esa copia; una copia vieja hacía que el juego no arrancara («The game could not start»).
6. Arranca `dwe-game --content build/studio-content --lang es --ui-skin original --text-font original` con la salida
   en `.dwe-studio/user-game-run.log`.

Opciones: `--no-pull` (no actualiza), `--no-build` (usa el binario que ya hay) y `--` para pasar más opciones al
juego, por ejemplo `scripts/dwe-play --no-pull -- --size 1920x1080`. Las variables `DWE_ROOT` (otro checkout),
`PRESET`, `DWE_PLAY_BRANCH` y `DWE_PLAY_CONTENT` cambian el checkout, el preset, la rama exigida y la carpeta de
contenido.

En el menú, empieza una **Nueva partida** en una ranura vacía. Las partidas existentes se gestionan desde la interfaz; no es necesario borrar archivos ni reutilizar una ranura ocupada para arrancar.

## Controles básicos

| Acción | Teclado | Mando |
|---|---|---|
| Moverse | WASD o flechas | Cruceta o stick izquierdo |
| Confirmar | Intro o Espacio | Botón sur |
| Cancelar o volver | Esc o Retroceso | Botón este |
| Abrir o cerrar el menú | Tab | Botón norte (triángulo) |
| Pausa | P o Pausa | Start |
| Opciones durante la partida | F10 | Select (Back) |

| Guardar un reporte de bug | F12 | |

Los nombres de los botones dependen del mando y los controles pueden tener contexto. La ayuda en pantalla muestra los del dispositivo utilizado. Estas son las asignaciones del motor, no las teclas configuradas en DuckStation.

## Reportar un bug

Si ves algo raro mientras juegas, pulsa **F12**: el juego guarda un reporte con la partida, una captura y lo que
estaba pasando, y enseña «Reporte guardado». Los reportes van a la carpeta `reports` de tus datos de usuario
(`~/.local/share/indie-modding-games/dwe/reports/` en Linux). Qué hay en cada uno y cómo se reproduce está en
[reportar un bug](../guides/prueba-vertical.md#reportar-un-bug-f12).

## Revisar una pieza sin iniciar una partida

Para abrir el escenario aislado de UI o el perfil del jugador:

```bash
scripts/dev.sh run --content build/local-content --ui-demo --lang es --ui-skin original --text-font original
scripts/dev.sh run --content build/local-content --ui-demo-player --lang es --ui-skin original --text-font original
```

La demo utiliza valores de ejemplo y no representa una partida cargada. Para inspeccionar modelos y sus animaciones:

```bash
scripts/dev.sh run --content build/local-content --model-viewer
```

Más opciones en [UI](../architecture/ui.md#demo) y [modelos](../architecture/models.md#visor-de-modelos). Para entender qué contiene cada archivo, consulta [Extensiones y archivos](../formats/extensiones.md).

## Si no arranca

| Síntoma | Qué comprobar |
|---|---|
| No encuentra `base` o faltan assets | Que `--content` sea la carpeta padre de `base/`, que exista `base/pack.yaml` y que la extracción haya terminado correctamente. |
| El extractor no reconoce la versión | El informe y la versión de la imagen; cambiar el nombre del archivo no cambia su contenido. |
| Fallan las películas extraídas | Que la imagen conserve los sectores de audio y vídeo originales; revisa [STR](../formats/video.md). |
| No encuentra el ejecutable compilado | Que `scripts/dev.sh build` haya terminado y que uses el mismo preset al compilar y ejecutar. |
| Error al crear el dispositivo Vulkan | El soporte de Vulkan 1.3 y el controlador de la GPU. |
| El idioma solicitado no está disponible | Que el contenido montado incluya sus tablas; empieza con `--lang en` o `--lang es`. |

Los errores se escriben en la terminal. Para guardarlos en un archivo local:

```bash
scripts/dev.sh run --content build/local-content > build/game-run.log 2>&1
```

No borres partidas o contenido para resolver un error de arranque. El modo opcional [assets de desarrollo](../architecture/assets-de-desarrollo.md) ofrece otro flujo para trabajar con discos y carpetas locales, pero no es necesario para el arranque descrito aquí.
