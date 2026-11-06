# Pasar el gate de una rama

> **Opcional desde el flujo de pull requests.** Lo que hace falta para fusionar en `main` son las comprobaciones de la
> CI de cada pull request ([Cómo contribuir](../contribuir.md#flujo-de-trabajo)). El gate sigue siendo la forma de
> pasar en local, con el contenido de tu disco, lo que la CI no puede: las rutas de autotest
> ([rutas](../modulos/tests/autotest.md)), el cocinado y clang-tidy. Las reglas del final son las del flujo anterior,
> cuando se subía directamente a `main`.

`scripts/gate.sh` corre el gate sobre el commit en el que está tu worktree (el árbol tiene que estar limpio). Lo
corre en uno de dos niveles, bajo el lock del gate de toda la máquina:

| Nivel | Cuándo | Qué corre |
|---|---|---|
| `short` (por defecto) | Antes de abrir un pull request que toque el juego | `dev.sh check-short`, el build cruzado de Windows y las rutas de humo, más las rutas propias del paquete |
| `daily` | Una vez al día sobre `main`, o por tren | `dev.sh check-gate` en modo completo (los pasos de `check-full`), el build cruzado de Windows y todas las rutas: hoy 21 en `en` y `es` (42 ejecuciones; la pareja de guardar y cargar cuenta como dos) sobre el contenido extraído y 4 sobre el `.pak` cocinado, 46 en total |

Solo funciona en un Linux con Docker, Xvfb y lavapipe. En Windows no hay gate: los pasos a mano siguen en la
[guía de la prueba vertical](prueba-vertical.md).

```sh
# Gate corto de un paquete que añade route-mi-ruta y toca un extractor
DWE_CONTENT=<contenido extraído> DWE_DISC=<imagen del disco> DWE_IMAGE=dwe-dev-speed DWE_JOBS=6 \
    scripts/gate.sh --routes-extra route-mi-ruta --disc-tests "the user's disc, extracted: mapas"

# Ejecución diaria sobre main
DWE_CONTENT=<contenido extraído> DWE_DISC=<imagen del disco> DWE_IMAGE=dwe-dev-speed scripts/gate.sh --tier daily
```

Al acabar escribe `summary.txt` en su carpeta de logs y sale con 0 solo si todo pasó. Las opciones son estas:

| Opción | Qué hace |
|---|---|
| `--tier short\|daily` | El nivel |
| `--tidy full\|incremental` | `short`: el alcance de clang-tidy (por defecto `incremental`). `daily` siempre lo pasa entero |
| `--cook full\|incremental` | `daily`: cocinar desde cero (por defecto) o reutilizar la caché |
| `--routes-extra a,b` | `short`: rutas de `tests/autotest` (sin `.yaml`) que también se corren, en `en` y `es` |
| `--disc-tests <regex>` | `short`: los tests del disco que se corren con el disco, una expresión de ctest; si no coincide con ninguno, falla. Necesita `DWE_DISC`. Los demás tests del disco se saltan |
| `--routes parallel\|sequential` | Las rutas en varias pantallas a la vez (por defecto `parallel`) o una detrás de otra en una sola. Usa `sequential` para un bisect o si la máquina va muy cargada |
| `--displays N` | Cuántas pantallas usa el modo paralelo, de 1 a 4 (por defecto 4) |
| `--lp-threads N` | Fija `LP_NUM_THREADS` (los hilos de lavapipe) en cada ejecución |
| `--steps check,windows,routes` | Solo algunos pasos. Cada paso que se salta deja `gate: <paso>=skipped reason=--steps ...`, y el resultado es `PASS (partial: ... skipped)`: eso no es un gate |
| `--repeat N` | Repite la lista de rutas N veces, para medir fallos intermitentes |
| `--lock <carpeta>` | El lock. Por defecto `_work/logs/p4-locks/full.d` si existe `_work/logs` junto al checkout principal |
| `--logs <carpeta>` | Dónde van los logs. Por defecto `_work/logs/daily-<fecha>` (`daily`) o `_work/logs/<rama>/gate-<hora>` (`short`) |

## Los pasos

**El lock.** Antes de nada, el gate espera al lock con un `mkdir` atómico y escribe en `owner` la rama, el tip, la
hora y su PID. También espera mientras haya junto a él otra carpeta de lock `full-*` (por ejemplo `full-otro.d`); un
fichero que se llame así, como un log, no cuenta. Mientras crea la carpeta y escribe `owner`, retiene las señales, así
que una interrupción en ese momento no deja un lock sin dueño. Al salir lo suelta, también si falla o lo interrumpes,
y solo si `owner` sigue siendo suyo o no llegó a escribirse.

**El espacio libre.** Antes del lock, el gate pide espacio libre en el disco del worktree según los pasos que vas a
correr, y se para si no lo hay:

| Paso | GB | Por qué (medido el 2026-10-22) |
|---|---|---|
| check | 12 | `build/ci` desde cero ocupa 7,3 GB, el cocinado 2,0 GB y su entrada en la caché otros 2,0 GB |
| windows | 2 | El corto reutiliza `build/windows-cross` (4,4 GB) y el diario lo borra antes de compilar otra vez: crece poco |
| routes | 1 | Los logs y capturas de las 42 ejecuciones del diario del 2026-10-22, antes de la pareja de guardar y cargar, ocupaban unos 80 MB |

Un gate completo pide 15 GB; solo las rutas (`--steps routes`), 1 GB.

**Las interrupciones.** Los pasos en Docker corren en segundo plano y el gate los espera con `wait`. Así, un
`SIGTERM` o un `Ctrl+C` en mitad de un check de una hora para el gate al momento: bash no atiende una señal hasta que
acaba el comando en primer plano, pero sí durante un `wait`.

**La comprobación previa.** Con el lock ya tomado, y antes de cualquier paso pesado, el gate comprueba que está todo
lo que necesitan los pasos que vas a correr:

- Para el check y Windows, que corren en Docker, apunta en el resumen la imagen y su ID
  (`gate: image=<nombre> id=<sha256>`). La imagen es `DWE_IMAGE` o, si no la das, la de por defecto de `dev.sh`
  (`dev.sh image-name`); el gate no fija ninguna. Después mira dentro de ella, sin descargar nada (`--pull=never`),
  que están sus herramientas:
  - el check: `cmake`, `ninja`, `ctest`, `ccache`, `clang++`, `clang-format`, `clang-tidy`, `python3`, `stylua`,
    `shfmt`, `luacheck`, `shellcheck`, `node` y `vitepress`;
  - Windows: `cmake`, `ninja`, `clang-cl`, `lld-link`, `llvm-lib`, `llvm-rc` y `llvm-mt`.
- Las rutas corren el juego en el host, así que comprueba en el host `Xvfb`, `timeout` y el driver de lavapipe
  (`/usr/share/vulkan/icd.d/lvp_icd.json`). Si te saltas el check, también que el juego ya esté compilado
  (`build/ci/apps/game/dwe-game`).

Si falta algo, se para en segundos con `gate: preflight=fail reason=...`, que dice qué falta y dónde, en vez de fallar
a mitad del gate. Pasa, por ejemplo, con una imagen anterior a la compilación cruzada de Windows: usa la que diga tu
equipo (`DWE_IMAGE=...`).

**El check.**

- `check-short` pasa el formato, el lint (luacheck y shellcheck), construye el sitio de documentación (falla con
  un enlace roto) y pasa `no-goto`, configura y compila el preset `ci` sin warnings, y corre todos los tests,
  sin dejar fuera ninguna etiqueta. Esa pasada corre sin `DWE_DISC` ni `DWE_CONTENT`, así que los tests del disco y
  del contenido extraído se saltan al momento. A la vez corre clang-tidy (ver abajo). Después, si das `--disc-tests`,
  corre esos tests, ya con el disco y el contenido. No cocina: el log lo dice con `gate: cook=none reason=...`.
- `check-gate` hace los mismos pasos que `check-full`, pero decide el alcance de clang-tidy y si reutiliza el
  cocinado. Con los dos en `full` es igual que `check-full`, y además deja constancia de las pasadas completas para
  los gates siguientes. La CI sigue usando `check-full`, que no cambia.

Los logs de cada paso quedan en `check-logs/`. Con `--disc-tests`, `summary.txt` añade una línea por cada test del
disco que corrió: su nombre, el filtro exacto (`--test-case=...`), las aserciones que contó doctest y el resultado
(`disc-test <nombre> | filter=... | assertions: N | N passed | 0 failed | result=Passed`). Un test con
`assertions: NONE` no comprobó nada: revísalo.

**Windows.** `windows-cross` configura y compila con 0 warnings, en todos los niveles. El nivel `daily` compila en
una carpeta nueva. El corto reutiliza `build/windows-cross` del worktree, porque una carpeta nueva cuesta de 30 a 40
minutos.

Que un build incremental no imprima warnings no basta para fiarse de él:

- no vuelve a compilar los fuentes que no han cambiado, así que no repite sus warnings;
- `/WX` convierte en error un warning del código del proyecto, pero no uno del código de terceros ni uno del
  enlazador.

Por eso, tras un build con 0 warnings, el gate deja una marca en la carpeta con el tip, la imagen y el hash de su
`.ninja_log`. Según esa marca, el gate siguiente compila de una de tres formas:

| Modo | Cuándo | Qué hace |
|---|---|---|
| `incremental` | La marca existe, la imagen es la misma y nadie ha compilado en la carpeta desde entonces (mismo `.ninja_log`) | Build incremental |
| `rebuild` | No hay marca, o se compiló en la carpeta fuera del gate | Limpia lo compilado del proyecto (`--target clean`) y lo vuelve a compilar todo. Es corto gracias a la caché del compilador, que además repite los warnings de cada fuente |
| `fresh` | No hay carpeta, ha cambiado la imagen, o es el nivel `daily` | Borra `build/windows-cross` (solo esa, dentro del worktree; se niega si `build/` es un enlace simbólico) y compila desde cero |

El log lo dice con `gate: windows-cross=<modo> reason=...`. Un build que falla, o que deja algún warning, no deja
marca.

**Las rutas.**

- `short` corre las rutas de humo de `smoke_routes`, al principio de `scripts/gate/route_jobs.sh` (ahora `route-loop-keyboard`,
  `route-keyboard` y `route-save-load`), en `en`. Entre las tres cubren el arranque, el menú principal,
  una partida nueva, el primer mapa, el campo, el menú y guardar y cargar una partida. Después corre las de
  `--routes-extra` en `en` y `es`.
- `daily` corre todas las de `tests/autotest/route-*.yaml` (no hay una cuenta fija: una ruta nueva entra sola), con
  las opciones que necesita cada una:
  - `route-dev-assets` con `--dev-assets`;
  - `route-mod-map` con el mod de ejemplo;
  - `route-skins` con `tests/mods/ui_skin`;
  - `route-save-load` con `--saves <carpeta de la ejecución>/saves`, y en el mismo trabajo, solo si pasa,
    `route-save-load-check` con la misma carpeta y `--load slot_01` (ver abajo);
  - las de `saves_routes` (`route-sleep-save`), con `--saves <carpeta de la ejecución>/saves` vacía, porque
    empiezan una partida nueva en una ranura y guardan en ella;
  - las 4 del `.pak` cocinado, en las que además comprueba que se cargó la fuente de mapa de bits.

**Rutas que siguen a otra.** Algunas rutas comprueban lo que dejó otra y nunca corren solas: están en
`follow_up_routes`, al principio de `scripts/gate/route_jobs.sh`. Ahora es una: `route-save-load` guarda con el comando de
depuración `save("slot_01")` en una carpeta de partidas nueva dentro de la suya (`<idioma>/route-save-load/saves`), y
`route-save-load-check` arranca con `--load slot_01` desde esa carpeta y comprueba el mapa, el sitio del jugador, el
compañero, el reloj y la bolsa. Las dos van en un mismo trabajo, una detrás de otra en la misma pantalla, así que
siguen juntas cuando las rutas corren en paralelo. Cada una tiene su línea en el resumen; si la primera no pasa, la
segunda sale como `NOT RUN` y cuenta como fallo. `--routes-extra route-save-load` corre la pareja; la segunda sola
no se puede pedir. Al escribir la lista de trabajos, el gate vacía esa carpeta de partidas, para que un hueco que dejó
otra ejecución no pueda pasar la comprobación; solo borra una carpeta `saves` dentro de sus propios logs, nunca otra
(ni `_work/saves-play`). La cabecera de las rutas en el resumen dice `jobs=` (trabajos) y `runs=` (ejecuciones: una
más por cada pareja). `scripts/gate/tests/test_follow_up.sh` lo comprueba con ejecuciones simuladas.

Antes de las rutas escribe `dev-assets.yaml` (en forma de lista) en la raíz del repositorio y después lo borra. Si ya
hay uno, no lo pisa y se para: muévelo tú.

## Rutas en paralelo

Las rutas corren en paralelo por defecto, en todos los niveles. El gate:

1. Busca pantallas libres entre `:72` y `:79`. Una pantalla está libre si no existe ni `/tmp/.X<n>-lock` ni
   `/tmp/.X11-unix/X<n>`.
2. Arranca en cada una su propio Xvfb. Xvfb toma el lock de la pantalla por sí mismo, así que si otro la coge en ese
   momento, solo falla ese arranque y el gate pasa a la siguiente. Una pantalla cuenta cuando el Xvfb sigue vivo y su
   socket existe.
3. Nunca usa `:71` (la del emulador de referencia) ni `:61` a `:66`. Si hay menos pantallas libres de las pedidas,
   corre con las que hay y lo dice.
4. Cada pantalla tiene un trabajador (`worker` en `scripts/gate/route_jobs.sh`) que toma el siguiente trabajo que
   nadie ha tomado (un `mkdir` atómico por trabajo). Una pareja de rutas que siguen a otra (ver arriba) es una sola
   línea de la lista, un solo trabajo: sus dos ejecuciones corren una detrás de otra en la misma pantalla, nunca
   repartidas. `test_follow_up.sh` lo comprueba con tres trabajadores a la vez. El orden es el de más largo a más
   corto, según los tiempos del gate anterior (el de una pareja cuenta sus dos ejecuciones), para que los largos no
   queden al final.
5. Cada ejecución escribe su línea de resumen en un fichero propio. Al acabar se juntan en el orden fijo de la lista,
   así que `summary.txt` tiene las mismas líneas, en el mismo orden, que en secuencial. Puedes compararlos con
   `diff <(grep ' rc=' a/summary.txt) <(grep ' rc=' b/summary.txt)`.
6. No reintenta nada: una ruta que falla es un fallo.

El gate solo para lo que arrancó, y nunca por nombre:

- cada Xvfb y cada ejecución quedan apuntados por su PID en `pids/`;
- cada ejecución es un `timeout` que encabeza su propio grupo de procesos, con el juego dentro, y se para ese grupo;
- cada paso en Docker es un `sg docker` que encabeza su propia sesión (`setsid`), y se para su grupo;
- antes de parar nada, comprueba con `ps` que el proceso sigue siendo el que arrancó;
- el orden es primero los trabajadores, luego las ejecuciones, luego los pasos en Docker y por último los Xvfb;
- los contenedores de Docker se llaman `dwe-gate-<nivel>-<rama>-<PID del gate>-<paso>-...`; si el gate se interrumpe
  con uno en marcha, para solo los que empiezan por su propio prefijo, que lleva su PID.

`dev.sh` pone nombre a todos sus contenedores: `DWE_CONTAINER_NAME` (o `dwe-<carpeta del worktree>`), el comando, el
PID de la shell que lo arranca y un contador. Así cualquiera que vea un contenedor sabe de quién es y qué hace.

El modo paralelo es el de por defecto desde que dos pares de ejecuciones, secuencial y paralela sobre un mismo tip,
dieron resúmenes idénticos, sin fallos intermitentes, uno con `--lp-threads` y otro sin él (2026-10-22, 42 ejecuciones
cada una, antes de la pareja de guardar y cargar): de 1096 s a 380 s y de 1123 s a 373 s, unas 3 veces más rápido en
4 pantallas. `--lp-threads` no cambió nada medible, así que queda sin fijar.

## clang-tidy incremental

Con `--tidy incremental` (`DWE_GATE_TIDY=incremental` en `dev.sh`), clang-tidy revisa solo lo que ha cambiado desde
la última pasada completa que se dio por buena.

**La referencia.** Es la última pasada completa que cumple todo esto:

- pasó hace menos de 24 horas;
- usó la misma imagen de Docker y la misma versión de clang-tidy;
- su commit está en `main` y por debajo del tip.

Si no hay ninguna, se pasa entero.

**Qué se revisa.** Primero se listan los ficheros que difieren entre ese commit y el árbol de trabajo. Con esa lista
se pasa entero si ha cambiado alguno de estos:

- una cabecera de una carpeta `include/` pública;
- un `.clang-tidy`;
- el build o el toolchain: `.devcontainer/`, `cmake/`, `triplets/`, el `CMakeLists.txt` raíz, `CMakePresets.json`,
  `vcpkg.json` o `scripts/dev.sh` (que es quien corre clang-tidy);
- una plantilla `*.in` de `configure_file` (como `apps/dwx/src/bundled_recipes.hpp.in`). La cabecera que genera vive
  en la carpeta de build, y el grafo de includes no ve quién la incluye.

Si no, se revisan:

- los `.cpp` cambiados que están en `compile_commands.json`;
- todos los `.cpp` que incluyen una cabecera cambiada, directamente o a través de otras cabeceras;
- todos los `.cpp` de un módulo cuyo `CMakeLists.txt` ha cambiado.

**El grafo de includes.** Sale de las líneas `#include` de los fuentes del repositorio. Cada include se resuelve
contra la carpeta del fichero que lo incluye y contra todas las carpetas `-I`, `-isystem` e `-iquote` del repositorio
que aparecen en `compile_commands.json`. El grafo prefiere pasarse a quedarse corto:

- un include dentro de un `#ifdef` cuenta;
- si un nombre coincide con varios ficheros, cuentan todos.

**El log.** `tidy-plan.log` y el principio de `tidy.log` dicen:

- el alcance y el motivo, con `gate: tidy=full reason=...` o `gate: tidy=incremental reason=...`;
- cada `.cpp` revisado y por qué (`tidy: check <fichero> (changed)`, o `(includes changed <cabecera> via ...)`);
- cada fichero cambiado que no se revisa y por qué (`tidy: not checked`);
- cada `.cpp` que se salta por no haber cambiado (`tidy: skip`).

**Los marcadores.** Una pasada completa que pasa con el árbol limpio deja un marcador: el commit, la fecha, la
imagen y la versión. Los gates siguientes parten de él. La ejecución diaria sobre `main` es la que deja los
marcadores que sirven a los gates cortos. Si un día no corre, los gates cortos pasan clang-tidy entero.

## Reutilizar el cocinado

Con `--cook incremental` (`DWE_GATE_COOK=incremental`), `check-gate` reutiliza un cocinado anterior en vez de cocinar.

**La clave.** Es un sha256 de todo lo que entra en el cocinado, y el log la desglosa parte por parte
(`gate: cook-key part ...`):

- el contenido extraído del paquete `base`: la ruta y el sha256 de cada fichero;
- el árbol de git de cada módulo con el que se compila `dwx`, lo que incluye el extractor y los cocineros. Los
  módulos salen de `ninja -t inputs apps/dwx/dwx` y de las cabeceras que `ninja -t deps` lista para sus objetos; así
  entra también un módulo que solo tiene cabeceras;
- el `CMakeLists.txt` de cada carpeta por encima de esos módulos (por ejemplo `engine/CMakeLists.txt`);
- el árbol de git de `.devcontainer/`, `cmake/`, `triplets/`, `content/enhance/` (las recetas que `dwx` lleva
  dentro), el `CMakeLists.txt` raíz, `CMakePresets.json`, `vcpkg.json` y `scripts/dev.sh`;
- la imagen de Docker y el preset.

La clave no usa el hash del binario de `dwx`: lleva dentro la ruta del worktree y nunca coincidiría entre worktrees.

**Cuándo se reutiliza.** Solo si todo esto se cumple:

- hay un cocinado guardado con esa clave;
- es de hace menos de 24 horas;
- su commit está en `main` por debajo del tip;
- el árbol está limpio;
- nada de lo que entra en el cocinado ha cambiado desde ese commit;
- la copia restaurada en `build/gate-cooked` tiene el sha256 del `.pak` y de la carpeta cocinada que se apuntaron al
  guardarlo.

Aunque se reutilice, los dos `dwx validate` se corren otra vez, porque el paquete `game` cambia con cada rama.

**Cuándo no.** Cualquier otro caso es un fallo de caché: se cocina desde cero, como en `check-full`, sin bucles de
error. El log dice `gate: cook=hit` o `gate: cook=miss`, con la clave y el motivo.

**Cómo se guarda.** Un cocinado se guarda solo después de pasar las dos validaciones. Se copia primero a una carpeta
provisional, que se renombra cuando está completa. Se guardan como mucho dos cocinados (unos 2 GB cada uno).

## Dónde se guarda el estado

Los marcadores de clang-tidy, la caché de cocinados y los tiempos de cada ruta están en `dwe-gate/`, dentro de la
carpeta de git común del repositorio (`git rev-parse --git-common-dir`). Todos los worktrees la comparten y nunca se
sube. `DWE_GATE_STATE_DIR` la cambia.

## Reglas del gate (flujo anterior, 2026-10-22)

1. `check-gate` incremental (clang-tidy incremental y cocinado reutilizado) es el gate de subida solo desde que este
   gate está en `main`. Hasta entonces, clang-tidy y cocinado completos.
2. `check-full` corre sobre `main` al menos una vez al día (la ejecución `daily`). También corre cuando cambia algo de
   esto:
   - una cabecera pública;
   - `.clang-tidy`;
   - el toolchain;
   - lo que entra en el cocinado;
   - el extractor.

   Si falla, no se sube nada más hasta arreglarlo. El gate lo hace cumplir por sí mismo: con esos cambios, o con la
   última pasada completa de hace más de 24 horas, pasa clang-tidy entero y cocina desde cero.
3. El log de cada gate dice, con prefijos fijos:
   - el nivel: `gate: tier=`;
   - el alcance de clang-tidy y su motivo: `gate: tidy=`;
   - la clave del cocinado con todas sus partes, y si fue acierto o fallo: `gate: cook=` y `gate: cook-key`.
4. En todos los niveles son obligatorios el build de Windows con 0 warnings y las rutas.
5. Las rutas en paralelo solo pasan a ser las de por defecto con la prueba de paridad: los resúmenes secuencial y
   paralelo idénticos sobre un mismo tip. Se cumplió el 2026-10-22 y son las de por defecto.

## Windows

Todo esto corre en el host Linux o en el contenedor. En Windows no hay `gate.sh`, y los tests de
`scripts/gate/tests` no se registran (`if(NOT WIN32)` en el `CMakeLists.txt` raíz), así que la CI de Windows no
cambia.

Lo que sí afecta a varias plataformas:

- **Las rutas de git** se leen con `-z` y se tratan como POSIX.
- **El build cruzado de Windows** sigue siendo un paso obligatorio de los dos niveles.
- **Los fuentes que solo compila Windows.** Un `.cpp` que no está en el `compile_commands.json` del preset `ci` se
  lista como no revisado, no se da por bueno. clang-tidy no lo revisaba en Linux antes de este gate, y tampoco ahora.
