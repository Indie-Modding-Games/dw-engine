# script

La máquina virtual de Lua 5.5 con sol2 ([ADR 0004](../../adr/0004-lua-y-sol2.md)) y la única puerta de los mods
al motor: una API de Lua versionada y aislada. Ningún otro módulo da Lua a los mods.

## Sandbox

- Sin `io`, `os`, `package`, `debug`, `load`, `loadfile`, `dofile` ni `string.dump`; `collectgarbage` solo admite
  `"count"`. `setmetatable` no acepta `__gc`: Lua ejecuta los finalizadores fuera de las llamadas y sin hooks, así
  que ningún presupuesto los pararía.
- Cada llamada del motor a Lua es un `ScriptCall` (o su versión interna): la más externa recibe el presupuesto
  completo; las anidadas, como un script que llama a una función de la API que lanza un evento, comparten lo que
  queda. El estado de Lua solo se alcanza con `ScriptCall::lua()`: un módulo que registra su API o llama a Lua lo hace
  siempre dentro de un `ScriptCall`.
- Límites por llamada (`ScriptLimits`): instrucciones (`instructionsPerCall`, 10 millones; el hook de cuenta de Lua
  las cuenta cada 1000, y cada llamada paga esas 1000 por adelantado, porque preparar el hook reinicia su cuenta) y un
  techo de tiempo (`callDeadline`, 1 s). **Las instrucciones deciden** dónde se para un script desbocado: la cuenta es
  la misma en cualquier máquina y con cualquier carga. El techo de tiempo es una red de seguridad holgada, unas diez
  veces lo que tardan las instrucciones en una compilación de release (de 100 a 200 millones de instrucciones de Lua
  por segundo), para el trabajo que la cuenta no ve, como comparar textos grandes. Cuenta el tiempo de procesador
  del hilo (`CLOCK_THREAD_CPUTIME_ID`; en Windows aún `std::chrono::steady_clock`), así que el tiempo que el hilo
  espera un núcleo en una máquina cargada no cuenta; el reloj de procesador solo se lee cuando ya pasó el techo en
  `steady_clock`, que es más barato. Tampoco cuenta lo que tardan las funciones que publica el motor (cargar un
  modelo, colocar un personaje): es trabajo del motor, no del script, y aplaza el techo lo mismo que duró. Antes, las
  secciones de entrada de mapa sin esperas (script 0, secciones 0xd7 de room05b y 0xd8 de room06: cargan modelos y
  colocan NPC de un tirón, como el original, que las corre enteras en un tick) fallaban por el techo de 20 ms del
  resume con la máquina a carga ~40; ahora pasan con carga ~49. Lo que agota uno de los dos se para con «the script ran
  too long and was stopped», seguido del motivo: «: it ran all the instructions of its budget» o «: it reached the
  time ceiling of its budget». `ScriptVm::lastBudgetStop()` da el mismo motivo como `BudgetStop` (`budget_stop.hpp`:
  `Instructions` o `TimeCeiling`) para la última llamada más externa, y los tests comprueban ese motivo, nunca cuánto
  tardó la parada. Una vez parado, cada instrucción falla, así que `pcall` no sirve para seguir. Los manejadores de
  `xpcall` no se ejecutan una vez parado el script: Lua los llamaría con los hooks apagados. `xpcall` y las funciones
  que cobran por bytes llaman con continuación (`lua_callk`), así que una corrutina puede hacer `coroutine.yield`
  dentro de `xpcall`.
- Las funciones de C que trabajan en proporción a la longitud de sus textos pagan por bytes (64 bytes cuestan una
  instrucción): los patrones de `string.find`, `match`, `gmatch` y `gsub` (`pattern_matcher.cpp`, el mismo
  comportamiento que el de Lua, sin `goto`), `string.rep` (antes de construir el resultado), `upper`, `lower`,
  `reverse`, `sub`, `byte`, `char`, `format`, `pack`, `packsize`, `string.unpack`, `utf8.len`, `codepoint`, `char` y
  `offset`, cada paso del iterador de `utf8.codes` (por los bytes que recorre), `table.concat`, `unpack`, `insert`,
  `remove`, `move` y `sort` sin comparador. El asignador también cobra los bytes que reserva, así que construir textos
  grandes con `..` en un bucle se para. Las corrutinas comparten el presupuesto: `coroutine.resume` y `wrap` preparan
  el hook de la corrutina antes de reanudarla. Preparar el hook reinicia su cuenta, así que una corrutina que cede
  antes de llegar a ella correría gratis: por eso su hook mira el presupuesto cada 64 instrucciones y cada
  reanudación paga esas 64 por adelantado. El hook es de cada hilo y su cuenta sigue entre cesiones y reanudaciones;
  cada comprobación cobra la cuenta con la que se preparó (`lua_gethookcount`). Así, una corrutina que el motor crea y
  reanuda desde C++ (`lua_newthread` y `lua_resume` dentro de un `ScriptCall`, como hace el runner de la historia con
  `script_coroutine.hpp`)
  paga lo que corre aunque herede la cuenta de 1000 de su creador, y gasta el presupuesto de la llamada que la reanuda.
- Comparar dos textos (`<`, `<=`, `==`) es trabajo de la VM y no pasa por ninguna función que se pueda cobrar por
  bytes. Es una decisión aceptada: cada comparación recorre como mucho un bloque (`largestBlockBytes`, 4 MiB), y un
  bucle que compara textos grandes se para por el techo de tiempo de la llamada (`callDeadline`).
- Memoria: el límite total (`memoryBytes`, 64 MiB) guarda un octavo para el trabajo del propio motor fuera de las
  llamadas; cada paquete tiene su cuota (`packMemoryBytes`, 16 MiB) y ningún bloque de una llamada pasa de
  `largestBlockBytes` (4 MiB). Cada bloque lleva en una cabecera quién lo pagó, para devolvérselo al liberarlo. Un
  script que se pasa recibe un error de memoria, nunca para el motor.
- Los scripts globales del motor y de la interfaz (`runAsset`, `runString`, `ScriptCall::lua()`) y todo `ScriptCall` corren a
  nombre del motor, sin cuota de paquete. Es una decisión aceptada: son código de confianza de las capas base y
  remaster, y les sigue aplicando el límite total de la VM, el presupuesto de instrucciones y el de tiempo.
- El código que corre a nombre del motor solo viene de un paquete que el motor encontró en sus propias carpetas
  (`assets::PackOrigin::Engine`: la carpeta de contenido, los paquetes del equipo, los de desarrollo) y que declara la
  capa base o remaster. Decide dónde se encontró el paquete, nunca solo su `pack.yaml`, que escribe el propio paquete:
  un mod que declara `layer: base` sigue siendo un mod. `engine_code.hpp` (`isEngineCode`, `checkEngineScript`,
  `checkEngineDocument`) es el único dueño de esa regla. `runAsset` la usa, y la UI también: un documento de un mod se
  carga, pero sus scripts en línea y sus atributos de evento (`onclick`…) no corren, y un `<script src>` solo corre si
  el fichero lo da un paquete del motor, sea cual sea el documento. Que un mod
  programe su propia UI en su entorno es de la fase 5.
- La metatabla de las cadenas está bloqueada (`getmetatable("")` da «locked by the engine») y su `__index` es una
  copia de solo lectura de la librería `string`: lo que un script haga a su tabla `string` no cambia los métodos
  (`("a"):upper()`) de nadie.
- `print` escribe como mucho 100 líneas por llamada, y el log corta en 4096 bytes cualquier texto que venga de un
  script (líneas, errores de manejadores, errores de recarga).
- Lo que escribe cada paquete (sus `print` y los errores de sus manejadores) tiene además un cubo de bytes
  (`pack_logs.hpp`): 16 KiB por segundo, con ráfagas de hasta 64 KiB. Lo que no cabe se descarta, con un aviso al
  empezar y la cuenta de líneas descartadas al volver a escribir. Un error idéntico al último del paquete no se
  repite: se cuenta («the last error repeated N times») cuando llega otro distinto, o cada 10 s si sigue igual. Si los
  errores paran, la cuenta pendiente se escribe en el siguiente fotograma pasados esos 10 s (`beginFrame`), y toda la
  que quede al cerrar la VM, aunque el cubo del paquete esté vacío.

## Presupuestos de un script, para quien hace mods

> **CAMBIO DE CONTRATO (2026-10-22).** Los manejadores de eventos de un paquete tienen menos instrucciones que antes:
> 250 000 por evento lanzado (antes 1 millón) y 500 000 por fotograma (antes 2 millones). Los techos de tiempo suben
> a 20 ms por evento (antes 5 ms) y 40 ms por fotograma (antes 8 ms). El error de una parada añade el motivo. Las
> llamadas (`instructionsPerCall`, 10 millones; `callDeadline`, 1 s) no cambian. Por bajar los presupuestos por
> defecto, la versión de la API de Lua (`LuaApiVersion`) sube de 1.0.0 a **1.1.0**. Cuando `docs/modding/` tenga una
> página de presupuestos, esta sección pasa allí.

Cada llamada del motor a tu script tiene un presupuesto, para que un mod roto o en bucle no cuelgue el juego:

| Qué | Instrucciones de Lua | Techo de tiempo |
|---|---|---|
| Una llamada (cargar un script, una función de la API) | 10 000 000 | 1 s |
| Los manejadores de tu paquete para un evento lanzado | 250 000 | 20 ms |
| Todos los manejadores de tu paquete en un fotograma | 500 000 | 40 ms |

- **Lo que para un script es la cuenta de instrucciones.** Es la misma en cualquier máquina y con cualquier carga:
  un bucle que se para en un PC lento se para en el mismo punto en uno rápido. Las funciones de la biblioteca que
  recorren textos o tablas grandes cobran también por lo que recorren (64 bytes, una instrucción).
- **El reloj es solo un techo holgado**, unas diez veces lo que tardan esas instrucciones en una compilación de
  release. Solo lo alcanza el trabajo que la cuenta no ve, como comparar textos grandes con `<` o `==` en un bucle.
  Ni la carga de la máquina ni lo que tardan las funciones del motor lo acercan. No cuentes
  con él: un manejador que necesita más de 250 000 instrucciones debe repartir el
  trabajo entre fotogramas.
- Cuando se agota, el script se para con un error que dice el motivo:
  - `the script ran too long and was stopped: it ran all the instructions of its budget`: gastó sus instrucciones;
  - `the script ran too long and was stopped: it reached the time ceiling of its budget`: llegó al techo de tiempo.
- Una vez parado, cada instrucción falla: `pcall` no sirve para seguir. Si un paquete agota el presupuesto de un evento
  o del fotograma, el resto de sus manejadores se salta hasta el siguiente, y el log lo dice.
- Las corrutinas gastan del presupuesto de la llamada que las reanuda, también entre cesiones.

## Versión de la API

> **CAMBIO DE CONTRATO (2026-11-07).** `LuaApiVersion` sube de 1.3.0 a **1.4.0**: los eventos pueden llevar una
> tabla versionada (`HookTable`), el juego publica sus eventos para los mods y ejecuta su `scripts/main.lua`
> ([API de Lua](../../modding/api-lua.md)), y `api` es obligatoria en las capas `mods` y `user`.

`LuaApiVersion` (`api_version.hpp`) es la versión semántica de toda la API de Lua. Es un alias de
`assets::SupportedLuaApi` (`packages/engine/assets`, `lua_api.hpp`), que es su dueño: la versión es un contrato de datos con el
`pack.yaml`, y así `dwx validate` la comprueba sin depender de Lua. Un paquete declara en su `pack.yaml` la versión que
piden sus scripts ([VFS](../../architecture/vfs.md)): `api: "1.x"`, cualquier 1.y, o `api: "1.3"`, la 1.3 o una
posterior. `assets::checkPackApi` rechaza otra versión mayor o una menor más nueva que la del motor. `packages/content` lo comprueba al montar y deja fuera, con el motivo, el mod que no la cumple.

Un paquete de las capas `mods` o `user` tiene que declarar `api` (desde la 1.4.0, #115): `checkPackApi` y
`modEnvironment` lo rechazan sin ella, y `packages/content` deja fuera, con el motivo, el mod con ficheros de Lua que
no la declara. Un mod sin `api` que no trae ficheros de Lua (solo datos, como los paquetes que escribían el lanzador y el mapper antes de este cambio) todavía se monta, con un aviso en el log; un mod con Lua sin `api` se deja fuera. Los paquetes del motor (`base`, `remaster`) pueden omitirla. La documentación para quien hace mods está
en [modding](../../modding/index.md).

Subir la versión: la menor cuando se añade algo (una función, un evento, un argumento opcional); la mayor cuando se
quita o cambia algo que un mod ya usa.

## Entornos de los mods

Cada paquete tiene su propio entorno (`ScriptVm::modEnvironment`, `ModEnvironment`):

- copias de las tablas estándar (`string`, `table`, `math`...): lo que un mod cambia en ellas no lo ve otro;
- `print` escribe en el log como `mod <paquete>: ...`;
- las globales son estrictas: leer una global que no existe o crear una nueva es un error, así que el código del mod
  usa `local`. `rawset(_G, ...)` tampoco crea globales;
- los espacios de la API (`hooks` y los que publica el motor) son vistas de solo lectura: escribir en ellas, `rawset`
  y `setmetatable` fallan, y `getmetatable` devuelve «locked by the engine».

`runModScript(files, id)` ejecuta el asset de Lua `id` de la VFS en el entorno del paquete que lo da, con el nombre de
trozo `@<paquete>:<id>` en los errores. Solo se aceptan ficheros `.lua`. `runAsset`, que ejecuta con las globales del
motor, solo acepta assets de los paquetes del motor de las capas `base` y `remaster` (`isEngineCode`): un mod que
sustituyera ese asset tendría los privilegios del motor, como los comandos de la consola.

`ModEnvironment::loadModule(files, id)` carga un asset del paquete como módulo (`ScriptModule`), lo que devuelve el
Lua, sin recordarlo para la recarga. Del módulo, `coroutine(ruta)` hace una corrutina que el motor reanuda con la
función que hay en esa ruta (lecturas crudas, sin metatablas), `value(ruta)` lee un valor simple y
`functionKeys(ruta)` lista las claves enteras de una tabla cuyos valores son funciones. `coroutine(ruta, función)` le
pasa a la función, como primer argumento, una función de **otro** módulo de la misma VM (o `nil` si no la hay): así
un parche de historia recibe la sección que envuelve. Esa función conserva el entorno de su módulo, y la corrutina
entera la paga el paquete del módulo que la crea.

## Recarga en caliente

`reloadChanged()` vuelve a ejecutar los ficheros de disco (`runFile`) y los assets (`runAsset`) que cambiaron. Para un
mod es una transacción: crea un entorno nuevo, ejecuta otra vez todos sus assets y solo si todos van bien cambia el
entorno y sus manejadores; si alguno falla, sigue el anterior y el error va al log. Los mods se recorren por índice,
porque un script puede crear el entorno de otro paquete mientras se recarga.

## Publicar funciones: `ApiRegistry`

El dueño de un sistema en C++ publica funciones bajo un espacio con nombre:

```cpp
vm.api().publish("field", "map_id", [](const script::ApiCall& call) -> core::Result<script::ApiValue> { ... });
```

En Lua es `field.map_id()`. `ApiCall` da el paquete que llama (`pack()`) y los argumentos, que son escalares
(`nil`, booleano, entero, número, texto); el resultado también. Las tablas llegarán como subida menor cuando un
sistema las necesite. Un error vuelve a Lua como error del script. Los
nombres son `[a-z][a-z0-9_]*`; `hooks` está reservado y no se puede tapar una global estándar.
Un valor publicado con `publishValue` puede llamarse además `[A-Z][A-Z0-9_]*`, como las constantes de Lua
(`story.ENDED`).

## Eventos: `HookRegistry`

El motor declara eventos `espacio.nombre` y los lanza con argumentos escalares:

```cpp
vm.hooks().declare("field.map_entered");
vm.hooks().fire("field.map_entered", arguments);
```

Un evento también se lanza con una tabla (`fire(evento, HookTable)`, desde la 1.4.0): sus campos escalares y
`version`, la versión de la tabla; cada manejador recibe una tabla nueva, así que lo que uno cambia no lo ve el
siguiente. Los eventos del juego (`session::GameHooks`, `game_hooks.hpp`) se lanzan así
([API de Lua](../../modding/api-lua.md#eventos-del-juego)).

Un mod se suscribe con `hooks.on("field.map_entered", function(t) ... end)`. Los manejadores se ejecutan en el
orden de los paquetes y, dentro de cada uno, en el orden en que se registraron. Los manejadores de un paquete
comparten una llamada, con su cuota de memoria y un presupuesto menor que el de una llamada: `instructionsPerHook`
(250 000, unos 2 ms en release) y el techo `hookDeadline` (20 ms) por evento lanzado. Además, todos los manejadores de
un paquete comparten un presupuesto por fotograma, `hookInstructionsPerFrame` (500 000) y el techo `hookTimePerFrame`
(40 ms); como en las llamadas, las instrucciones paran primero y el reloj es solo el techo holgado. El módulo que lleva
los fotogramas llama a `vm.hooks().beginFrame()` una vez por fotograma para rellenarlo. Cuando un paquete agota el de
un evento o el del fotograma, el resto de sus manejadores se salta, y el log lo dice como mucho una vez cada 10 s.
Si `fire` se llama desde dentro de una llamada (un script que llama a una función de la API que lanza un evento),
cada paquete recibe lo menor entre su presupuesto de evento y lo que le queda a la llamada (y el plazo que antes
venza), y la llamada paga lo que gastó; un manejador parado por tiempo no cuenta como si hubiera gastado todas sus
instrucciones. Un paquete sin manejadores del evento no abre llamada ni paga nada. Un paquete registra como mucho 64
manejadores por evento. Si uno falla, el error va al log con el paquete («`mod <paquete>: a handler of <evento>
failed: ...`») y siguen los demás. `fire` devuelve cuántos se ejecutaron, cuántos fallaron y cuántos se saltaron.

## Consola y pruebas

El entorno global sigue para la consola de depuración y las rutas de autotest: la tabla `input`, los textos del
jugador como `t(clave[, valores])` y `texts.language()` ([localización](../../architecture/localizacion.md)), y
`bindCommand`, que añade funciones globales de un argumento de texto para los comandos de la consola
(`text_font("original")`).
