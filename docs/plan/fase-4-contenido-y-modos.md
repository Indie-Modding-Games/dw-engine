# Fase 4: Contenido y modos

## Objetivo

El juego completo con su historia reescrita, personajes con más profundidad, modos de juego nuevos y una API para que
cualquiera pueda hacer mods. Termina con la publicación de la primera versión del mod, y a partir de ahí sigue
abierta.

## Por qué

Es lo que justifica el proyecto: cambiar la historia, dar profundidad a los personajes y crear modos nuevos. Va al
final porque se construye sobre todos los sistemas anteriores.

## Alcance

- **Historia:** los scripts de eventos originales se traducen a Lua como punto de partida y luego se reescriben.
- **Personajes:** árboles de diálogo, afinidad y misiones.
- **Modos de juego:** un framework donde cada modo combina los sistemas con su propio ruleset.
- **API de mods:** paquetes que añaden o sustituyen contenido por ID, y hooks de Lua versionados y en sandbox.
- **Más contenido:** más Digimon y mapas, fondos en alta resolución (capa `remaster`).
- **Editor de mapas:** el editor externo de la organización (Digimon-World-Map-Editor, `dw1fe`), que ya lee y escribe
  nuestro formato; el motor no tiene editor propio.

## Fuera de alcance

- Distribuir contenido original del juego: cada usuario extrae su disco
  ([política legal](../legal/assets.md)). Las mejoras de arte hechas a partir del original se distribuyen como
  receta, no como imágenes.

## Requisitos

| ID | Requisito | Cómo se comprueba |
|---|---|---|
| R4.1 | Los scripts de eventos del original se convierten a Lua legible y el juego los ejecuta | Test que recorre los eventos convertidos |
| R4.2 | Un mod es un paquete que añade o sustituye contenido por ID, sin tocar el motor | Mod de ejemplo en los tests |
| R4.3 | Los mods solo ven la API de Lua, versionada y en sandbox ([ADR 0004](../adr/0004-lua-y-sol2.md)) | Tests del sandbox |
| R4.4 | Añadir un Digimon o un mapa nuevo no exige cambiar código | Paquete de ejemplo con un Digimon nuevo |
| R4.5 | Cada modo de juego declara su ruleset y los sistemas que usa | Dos modos de ejemplo |
| R4.6 | La documentación de modding (`docs/modding/`) explica la API y los formatos | Revisión |
| R4.7 | El editor de mapas externo (`dw1fe`) lee y escribe el formato fuente | Tests de ida y vuelta de nuestro contrato ([herramientas externas](../architecture/herramientas-externas.md)) |

## Diseño

- Módulos: `game/story`, `game/modes` y la API de Lua para mods (solo desde el módulo `script`). No hay
  `apps/editor`: el editor de mapas es la herramienta externa `dw1fe`.
- Las capas del VFS (base, remaster, mods, usuario) de la fase 1 ya permiten que un mod gane a la base por ID.
- **Scripts de eventos:** `dwx` convierte los scripts de eventos del disco en Lua legible al extraer, en la máquina de
  cada usuario (ningún dato del juego en git). El motor solo ejecuta Lua. Tests de paridad contra el decomp y, con el
  disco del usuario, en los tests de disco. El YAML de scripts que edita `dw1fe` (`script/dg/NNN.yaml`,
  `script/maphead.yaml`) sigue siendo parte del contrato; el Lua es una salida más.
- **Dos caminos para los assets:**
  - *Desarrollo:* en local, el motor y las herramientas pueden usar todos los assets de la máquina sin restricción:
    la imagen del disco, las carpetas ya extraídas y los datos del decomp. Es explícito y opcional (una opción o un
    ajuste de desarrollo) y nunca es lo que usa un jugador. El trabajo de desarrollo no espera a la extracción ni al
    empaquetado.
  - *Producto:* `dwx` extrae, convierte y coloca los assets (paquete `base`, conversión a Lua) en la máquina del
    usuario. Ninguno de los dos caminos pone datos del juego en git.

## Fase 4a: tecnología

Decisión del responsable de diseño (2026-10-21): primero la tecnología; la historia reescrita y los personajes esperan
al GDD.

| Paquete | Qué | Requisito |
|---|---|---|
| Assets de desarrollo | Camino de desarrollo opcional a los assets locales | Decisión de los dos caminos |
| API de Lua para mods | API versionada (semver) y en sandbox, hooks, entorno por mod | R4.3 |
| Conversión de scripts | Ficha de referencia del intérprete original, desensamblador y conversión a Lua en `dwx` | R4.1 |
| Intérprete de historia | `game/story` ejecuta el Lua convertido (entrada al mapa, hablar, zonas, combates) | R4.1 |
| Mods por ID | Paquetes que añaden o sustituyen contenido por ID; Digimon y mapas nuevos sin código | R4.2, R4.4 |
| Modos | `game/modes`: cada modo declara su ruleset y sus sistemas; «Historia original» y un segundo modo sencillo aprobado por diseño | R4.5 |
| Contrato con el editor | Tests de ida y vuelta del formato que escribe `dw1fe` | R4.7 |
| Documentación de modding | `docs/modding/`: API, formatos de paquete, IDs, modos, aspectos, tutorial | R4.6 |
| Aspectos de la interfaz (9a) | Mecanismo de aspectos (skins) de la UI, ajuste global en vivo, aspecto moderno por defecto | Petición del responsable de diseño |
| Aspectos de la interfaz (9b) | Aspecto de estilo original en todas las pantallas, reloj y barras del campo, cuadro de texto; receta de escalado en `dwx` | Petición del responsable de diseño |

**Estructura prevista** (se revisa con quien lo implementa antes de empezar; cada módulo sigue las
[reglas de dependencias](../architecture/dependency-rules.md): API pública en `include/`, detalles en `src/`, una
responsabilidad por fichero y clase, tests junto al comportamiento y un README en español; sin ficheros «dios» ni
cajones de utilidades):

| Paquete | Dónde |
|---|---|
| Assets de desarrollo | `engine/assets`: fuentes de desarrollo (`dev_asset_sources.hpp`) y su montaje; `apps/content`: la opción `--dev-assets` y el fichero de configuración; avisos en `messages.hpp` y claves de localización |
| API de Lua para mods | `engine/script`: `api_version.hpp` (versión, un solo dueño), `mod_environment.hpp` (entorno por mod), `api_registry.hpp` (funciones publicadas por espacio de nombres), `hook_registry.hpp` (eventos), carga desde la VFS y patrones de texto con presupuesto en `src/` |
| Conversión de scripts | `formats/.../script/`: `control_flow` (grafo por sección), `structuring` (árbol estructurado sin `goto`), `lua_emitter` (Lua legible); `apps/dwx/src/`: el extractor que escribe el Lua en el paquete `base`; ficha `docs/reference/scripts.md` |
| Intérprete de historia | `game/story`: estado de la historia (banderas y variables), planificador de corrutinas, puntos de entrada; el pegamento con campo, compañero, inventario y combate en `apps/session` (funciones publicadas en la API de Lua) |
| Mods por ID | `engine/assets`: IDs con espacio de nombres y composición de tablas por entrada; paquete de ejemplo sintético en `tests/mods/` |
| Modos | `game/modes`: manifiesto y catálogo de modos; `content/packs/game/modes/<modo>/mode.yaml` con su esquema; selección en `apps/session` y `apps/screens` |
| Contrato con el editor | Solo tests, en `apps/dwx/tests/editor_*` (el único módulo que enlaza los escritores de `formats`, los lectores de `engine/assets` y `dwx validate`), y [herramientas externas](../architecture/herramientas-externas.md) |
| Documentación de modding | `docs/modding/`: índice, API de Lua, paquetes, IDs, modos, aspectos y un tutorial del primer mod |
| Aspectos de la interfaz | `engine/ui`: carga de un aspecto (`skin.hpp`), hojas de sprites y variantes de paleta generadas; `content/packs/game/ui/skins/<aspecto>/` (`skin.yaml` + RCSS por pantalla); el ajuste `ui_skin` en `apps/game` por el mismo camino que `text_font`; la receta de escalado en `apps/dwx` |

**Buenas prácticas (regla del usuario, 2026-10-21):** se aplica entera la [guía de código](../conventions/code.md):
C++26 moderno (RAII, `std::expected`, rangos, sin `new`/`delete` a mano, sin parámetros de salida cuando basta un
retorno, `const` correcto), funciones pequeñas, nombres por responsabilidad, sin valores mágicos y sin `goto`. El Lua,
escrito o generado, es idiomático y modular: `local`, funciones pequeñas y sin globales en el código de los mods. Cada
rama pasa una revisión independiente contra la guía de código y las reglas de dependencias antes de integrarse.

**Decisiones del responsable de diseño (2026-10-21):**

- Segundo modo de ejemplo: **Arena** (se elige un Digimon y su etapa y se lucha contra una escalera de enemigos de las
  tablas originales, con las reglas de combate de `original` y sin reloj de cuidados). Su diseño detallado se aprueba
  antes de construirlo.
- El Lua convertido es **totalmente estructurado**, al estilo de un decompilador (if/else, bucles, despachos), sin
  `goto`. Los tests de paridad son obligatorios. Regla del proyecto: no hay `goto` en ningún sitio (C++, Lua escrito
  y Lua generado), así que el conversor estructura todos los scripts (if/else, bucles, `break`, retornos tempranos,
  funciones auxiliares o bucles con estado) y un test falla si el Lua generado contiene `goto`.
- Assets de desarrollo: opción `--dev-assets` y un fichero de configuración de desarrollo, en todos los builds,
  apagado por defecto y con un aviso visible mientras está activo.
- La API de Lua tiene **una sola versión semver**; el mod la declara en `pack.yaml` (`api: "1.x"`) y el motor rechaza
  otra versión mayor.
- El contenido nuevo usa **IDs de texto con espacio de nombres** (`mimod:mi_digimon`); el contenido original recibe
  nombres estables y los números quedan como detalle interno.
- Los diálogos del Lua convertido usan **claves de texto** (`text/<idioma>/...`), así la traducción sigue funcionando.
- Para cambiar un evento original, un mod **edita el Lua**. El YAML de scripts que edita `dw1fe` sigue siendo la
  entrada del conversor, y `dwx` convierte también el YAML editado de un mod.

- **Aspectos de la interfaz** (2026-10-21): se puede elegir entre la UI actual y una de estilo original, mejorada y en
  alta calidad, en todas las pantallas (menú principal, barras de vida, reloj…).
  - Fidelidad: la disposición, los colores y las proporciones del original, redibujados nítidos a cualquier
    resolución y adaptados a pantalla ancha.
  - Arte: híbrido. Las primitivas (marcos, barras, cuadro de texto, degradados) se hacen en RCSS a resolución completa;
    los iconos y sprites salen de un escalador de pixel art determinista (tipo xBRZ) aplicado como receta de `dwx` al
    extraer, en la máquina del usuario. Sin IA y sin distribuir arte original.
  - Un solo ajuste global de aspecto para todas las pantallas; por defecto la UI moderna y el estilo original como
    opción.
  - Los mods pueden añadir aspectos: el contrato de un aspecto es estable y se documenta en `docs/modding/`.
  - Entra en la fase 4a: el mecanismo (9a) ya, en paralelo; el aspecto original (9b) justo después del intérprete de
    historia, que trae el cuadro de texto.

**Más decisiones del responsable de diseño (2026-10-21):**

- *Assets de desarrollo:* `--dev-assets` es un interruptor sin argumentos; el fichero de configuración se busca en la
  carpeta actual y luego en la carpeta de datos del usuario. Los paquetes de desarrollo quedan **siempre por debajo**
  de todos los paquetes normales (base, los del equipo y `--mods`): solo rellenan lo que falta, y su capa y prioridad
  se limitan al montarlos. Para leer el disco en tiempo de ejecución, `dwx` (en un proceso aparte) extrae bajo demanda
  a una caché de desarrollo identificada por el hash del disco y la versión de `dwx`, y esa carpeta se monta como una
  extraída más.
- *Contrato con el editor:* el paquete de conversión a Lua añade `formats::script::assembleScript`, con un test de que
  todos los scripts del disco se vuelven a ensamblar idénticos byte a byte. La rareza de las comillas del YAML se
  queda como está, documentada y fijada por un test.
- *API de Lua para mods:*
  - Leer o crear una global desconocida es un error.
  - En la fase 4a se acepta un paquete sin `api`; pasa a ser obligatorio en las capas `mods` y `user` cuando llegue
    `docs/modding/`. Solo se acepta la forma `MAYOR.x`.
  - Los argumentos de los hooks son solo escalares por ahora; las tablas llegarán con una versión menor cuando un
    sistema las necesite.
  - Los bytes que reserva el asignador cuentan contra el presupuesto de memoria del mod.
  - Un mod se suscribe con `hooks.on("espacio.evento", fn)`; cada entorno de mod tiene su propia vista de la API; la
    consola de depuración solo ve los espacios de nombres del motor.

- *Más sobre la API de Lua:* `api` admite también `MAYOR.MENOR` (`"1.3"` = API 1, al menos la versión menor 3);
  `1.x` sigue valiendo. Un mod roto o incompatible se salta, se registra en el log y aparece en la lista de mods; los
  demás cargan.
- *IDs de contenido:*
  - El contenido original lleva el prefijo `base:` (`base:agum`, `base:twna01`). La migración (contenido, partidas
    guardadas, rulesets, scripts, tests y documentación) y su efecto en las herramientas externas se planifican
    aparte; un alias sin prefijo durante la transición solo existiría si se declara explícitamente.
  - En la fase 4a los mods solo añaden o sustituyen entradas; no las borran.
  - Una entrada nueva recibe el índice interno siguiente (máximo + 1); las partidas guardan IDs, nunca índices, con su
    migración.
  - Los nombres de las entradas nuevas usan la clave `<paquete>.<bloque>.<nombre>` en el fichero de texto del propio
    mod (`text/<idioma>/<paquete>.yaml`).
  - Un modelo nuevo debe traer su fichero de contrato.
  - Objetos, técnicas, puertas y baños también pasan a IDs en la fase 4a.
  - El mod de ejemplo sustituye el mapa de inicio del campo como ejemplo de sustitución de un asset (solo en tests).
- *Aspectos de la interfaz:* la opción se llama «Aspecto de la interfaz». La opción de fuente pasa a llamarse «Texto con
  aspecto original» cuando llegue el aspecto original. La recarga en caliente de `skin.yaml` llega con la documentación
  de modding. La clase `.original-look` vive en la hoja base común. Un mod nombra su aspecto en su propio fichero de
  localización.
- *Reparto de los mods por ID:* tres ramas que se integran una tras otra: composición de tablas por entradas con IDs con
  espacio de nombres y el mod de ejemplo; referencias por ID en objetos, técnicas, puertas, baños y partidas guardadas;
  y la migración al prefijo `base:`.

**Criterio de cierre de la fase 4a:** en `main`, todos los scripts de eventos convertidos se ejecutan sin errores en
un test sin ventana y pasan los tests de paridad; el mod de ejemplo (un Digimon nuevo, un mapa nuevo y una sustitución)
carga sin cambiar código; pasan los tests del sandbox y de versión de la API; los dos modos se eligen desde el menú
con rutas de autotest en inglés y español; pasan los tests de ida y vuelta del editor; los dos aspectos de la
interfaz se cambian en vivo desde el menú y cubren todas las pantallas, con rutas de autotest en los dos aspectos;
`docs/modding/` está revisada.

## Criterio de cierre

Se publica la primera versión del mod. La fase sigue abierta después para nuevo contenido.

## Dependencias y riesgos

- Depende de la fase 3 y del GDD (historia y personajes).
- Riesgo: el alcance. Por eso la fase es continua y la primera versión se define en el GDD.
- Riesgo: Digimon de otros juegos. Se resuelve con un importador desde el disco del usuario o con modelos propios del
  equipo.

## Estado (2026-10-21)

Fase 4a empezada.
