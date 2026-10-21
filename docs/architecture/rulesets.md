# Rulesets

Las reglas del juego son datos ([ADR 0010](../adr/0010-reglas-propias-con-rulesets.md)): un ruleset reúne los números
y la elección de las estrategias de cada sistema, y vive en un paquete de contenido. El primero, `original`, reproduce
el juego original ([ADR 0022](../adr/0022-primer-ruleset-fiel-al-original.md)). Este documento describe el formato,
dónde está el código que lo lee y cómo lo usa cada sistema.

## Dónde está

| Qué | Dónde |
|---|---|
| Código | [`engine/ruleset`](../../engine/ruleset/README.md) (`dwe::ruleset`), capa de servicios |
| Ruleset `original` | [`content/packs/game/rules/original/`](../../content/packs/game/rules/original) |
| Esquemas | `content/schemas/ruleset.schema.json` (manifiesto) y `ruleset_<sección>.schema.json` (una por sección) |
| Reglas de esquema | `schemas:` de [`content/packs/game/pack.yaml`](../../content/packs/game/pack.yaml) |

**Por qué un módulo de servicios y no `game/ruleset`.** Todos los sistemas del juego (partner, batalla, inventario,
guardado) leen su sección del ruleset, y la regla 4 de las [reglas de dependencias](dependency-rules.md) prohíbe que
los módulos de `game/` se usen entre sí. El núcleo del ruleset no sabe nada de cuidado ni de batalla: solo lee YAML,
sigue `extends`, cuenta revisiones y crea estrategias por nombre. Eso es un servicio, como `script` o `ui`, y los
módulos de `game/` ya pueden usar servicios. No hace falta ninguna excepción a las reglas. El reloj de juego vive en
el mismo módulo porque lo comparten el partner, la batalla, el campo y el autotest, y su sección es parte del
ruleset.

## Formato

Un ruleset es una carpeta `rules/<id>/` de un paquete:

```
rules/original/
├─ ruleset.yaml      manifiesto
├─ clock.yaml        una sección por fichero
├─ care.yaml
└─ *.lua             estrategias en Lua (más adelante)
```

### Manifiesto

```yaml
id: original                 # el nombre de la carpeta
name: "Original"
version: 0.1.0
description: "..."
extends: original            # opcional: otro ruleset del que parte
tables:                      # las tablas que usa, por papel; ids de asset de los paquetes
  digimon: data/digimon
  raise: data/raise
sections: [clock, care]      # las secciones que define este ruleset
scripts: [evolution.lua]     # opcional: ficheros Lua de la carpeta
```

- **Ids y nombres:** letras minúsculas, dígitos y `_`. El id tiene que coincidir con la carpeta.
- **Tablas:** las tablas por Digimon, técnica u objeto no se copian en el ruleset. El ruleset dice qué tabla de los
  paquetes cumple cada papel; `original` usa las del paquete base que extrae `dwx`, y otro ruleset puede traer las
  suyas en su paquete. Cada tabla tiene que existir al cargar.
- **Secciones:** cada una es `rules/<id>/<sección>.yaml` y tiene su esquema `ruleset_<sección>`, con su regla en
  `schemas:` del `pack.yaml` (`ruleset_care: [rules/*/care]`). Quien añade una sección añade los dos.

### `extends`

Un ruleset puede partir de otro y cambiar solo una parte: sus secciones **sustituyen enteras** a las del otro, sus
tablas sustituyen a las del mismo papel, y sus scripts se suman. Lo que no define viene del otro. Así cada fichero de
sección está completo y se valida solo con su esquema. La cadena se sigue hasta 8 rulesets, y un ciclo es un error.

```yaml
# rules/dificil/ruleset.yaml
id: dificil
name: "Difícil"
version: 0.1.0
extends: original
sections: [care]             # su care.yaml sustituye al de original; el reloj y las tablas son los de original
```

### Rarezas: `quirks`

El ruleset `original` reproduce todas las rarezas y bugs del original (decisión del responsable de diseño,
2026-10-20), cada una detrás de una bandera con nombre que otro ruleset puede apagar. Cada sección las agrupa en su
mapping `quirks:`. `true` reproduce el original y `false` lo corrige. Todas son obligatorias, así que cada ruleset
decide cada rareza:

```yaml
# rules/original/care.yaml
quirks:
  # partner_impl.c: handleSleeping quita el hambre antes de mirarla para bajar el peso.
  sleeping_hungry_keeps_weight: true
```

El sistema las lee con `readQuirks(reader, nodo, {"sleeping_hungry_keeps_weight", ...})` y pregunta
`quirks.on("sleeping_hungry_keeps_weight")`. El esquema de la sección las declara todas (`required` y
`additionalProperties: false`).

### Desviaciones aprobadas

Cuando el responsable de diseño aprueba que `original` se separe del juego original (por ejemplo, el autoguardado),
el valor lleva un comentario `# Desviación aprobada (AAAA-MM-DD): ...` con el motivo, y si se puede apagar, una
bandera en la sección (por ejemplo, `autosave: { on_map_change: true, ... }` en la sección del guardado).

### Citas

Cada constante de `original` lleva encima el fichero y la función del decomp que reproduce, como en
`field/rules.yaml`: `# partner_impl.c: tickHungerMechanics: ...`. Las unidades van en el comentario de la cabecera de
la sección.

### Decisiones del responsable de diseño (2026-10-20)

Estas cuatro reglas del formato están aprobadas y son definitivas:

1. `extends` sustituye secciones y tablas **enteras**; no hay mezcla profunda de valores.
2. `reload()` **descarta** los valores ajustados en vivo: los ficheros mandan.
3. Cada rareza que lee una sección se **declara en cada ruleset**; no hay valores por defecto.
4. La velocidad del reloj es un **multiplicador real** (`setSpeed(x)`), no una lista de pasos.

## Cómo lo usa un sistema

1. **Su sección.** El sistema define su struct y lo lee de la sección con `assets::YamlReader`, con los errores
   en la línea y columna del fichero (`reader.finish(valor, section.file)`).
2. **Su comprobación.** Da una `SectionCheck` que lee la sección y descarta el resultado. La app junta las de todos
   los sistemas en `SectionChecks` y se las da al `RulesetStore`: así una carga, una recarga o un valor ajustado que
   el sistema no aceptaría se rechaza antes de que lo vea nadie.
3. **Revisión.** Guarda la `revision()` con la que leyó su sección y la vuelve a leer cuando cambia.

```cpp
// game/partner (ejemplo)
core::Result<CareRules> readCareRules(const ruleset::Section& section);

ruleset::SectionChecks checks{{"care", [](const ruleset::Section& s) -> core::Result<void> {
    if (auto read = readCareRules(s); !read) return std::unexpected{read.error()};
    return {};
}}};
auto store = ruleset::RulesetStore::load(vfs, "original", checks);
auto care = readCareRules(*store->current().section("care"));
auto clock = ruleset::GameClock{*ruleset::readClockRules(store->current())};
std::uint64_t seen = store->revision();
// cada tick:
clock.advance(dt);
if (store->revision() != seen) { care = readCareRules(*store->current().section("care")); seen = store->revision(); }
```

### Estrategias

Una regla que es lógica y no un número (cómo se elige una evolución, cómo decide la IA de batalla) va detrás de una
interfaz. El sistema declara la interfaz y un `StrategyRegistry`, registra `original` y las demás, y la sección dice
cuál usar:

```cpp
struct EvolutionSelection { virtual ~EvolutionSelection() = default; virtual int pick(/*...*/) = 0; };
ruleset::StrategyRegistry<EvolutionSelection> selections{"evolution selection"};
selections.add("original", [] { return std::make_unique<OriginalSelection>(); });

// En la comprobación de la sección: un nombre desconocido es un error en su línea.
selections.check(section, "selection");
// Al leer la sección:
auto selection = selections.create(section, "selection");   // evolution.yaml: `selection: original`
```

El error dice el fichero, la línea, la columna y los nombres conocidos:
`.../rules/original/evolution.yaml: line 3, column 12: unknown evolution selection 'newest'; known: original`.
Las estrategias en Lua llegarán con su propia implementación del registro; el manifiesto ya puede nombrar los
scripts (`scripts:`).

### Aleatoriedad

El ruleset `original` usa las probabilidades y distribuciones del original, pero no su generador: no se reproducen las
tiradas bit a bit (decisión del responsable de diseño, 2026-10-20). Las reglas tiran con `ruleset::Random`, un PCG32
con semilla: con la misma semilla, un test o una simulación juega igual en todas las plataformas. `below(n)` da de 0
a n − 1, como `randomLimit(n)` del original (`math.c`), y `chance(p)` acierta p veces de cada 100.

## Reloj de juego

La sección `clock` describe el reloj del original (`clock.c: tickGameClock`): 20 fotogramas por segundo, un minuto
cada 20 fotogramas, 30 días por año, el año vuelve a 0 en 100, y una partida empieza a las 8:00.
`ruleset::GameClock` lo hace avanzar con el tiempo real:

- **Fotogramas.** Cuenta en fotogramas del original, porque las reglas de cuidado van "cada 200 fotogramas":
  `frameOfDay()` es el `CURRENT_FRAME` del original.
- **Avance.** `advance(segundos)` devuelve cuántos minutos, horas, días y años ha cruzado. Un sistema que hace algo
  cada hora lo repite `hours` veces, así que avanzar días de golpe da lo mismo que jugarlos.
- **Velocidad.** `setSpeed(x)` es el avance rápido: 1 es la velocidad del original (un minuto de juego por segundo
  real), 0 lo para, 60 hace una hora de juego por segundo y 3600, 60 horas de juego por segundo. Lo usan los tests, el autotest, las simulaciones y el panel de depuración.
- **Pausa y saltos.** `setRunning(false)` es el `stopGameTime` del original. `jumpTo(hora, minuto)` es el salto al
  dormir (`advanceToTime`): solo cuenta el cambio de día, no las horas saltadas.
- **Uno por partida.** El reloj lo tiene la partida (`apps/session`), que lo avanza solo en los ticks en que el campo
  corre tiempo de juego (`Field::gameTimeRan`) y lo guarda con sus fracciones (`restore(fotogramas, pendientes)`),
  así que una partida cargada sigue igual que la guardada.

## Recarga en caliente y ajuste (R3.4)

`RulesetStore` tiene el ruleset activo:

- `reload()` lo vuelve a leer de los paquetes tras editar sus ficheros, y `select(id)` cambia a otro ruleset.
- `setValue(sección, ruta, yaml)` cambia un valor (`"care"`, `"hunger.window_minutes"`, `"45"`) para el panel de
  ajuste. La ruta va por claves separadas por puntos, y un número elige un elemento de una lista.
- Cada cambio vuelve a pasar las comprobaciones. Si falla, el ruleset anterior sigue activo y el error dice por qué.
  Si se acepta, la revisión sube.
- Una recarga descarta los valores ajustados: los ficheros mandan.

La VFS lee los ficheros cada vez, así que editar una sección y recargar basta. Un fichero nuevo necesita volver a
montar los paquetes.

El [panel de ajuste](../../engine/debug/README.md#panel-de-ajuste-r34) del overlay de depuración usa todo esto: elige
el ruleset, edita sus valores, enseña qué cambiaría una recarga, controla el reloj y guarda una sección en su fichero.
Guardar no reescribe el documento con `emitYaml`, que perdería los comentarios (el árbol de YAML no los guarda):
`saveSection` cambia solo el texto de cada valor que difiere del fichero, en su línea, así que las citas del decomp y
el orden se quedan. Un cambio de forma (un valor que pasa a lista, una clave nueva) no se guarda: se edita el fichero.

Las [simulaciones](../guides/simulaciones.md) (`dwe-sim`) cargan un ruleset igual que el juego y aceptan los mismos
cambios por la línea de órdenes (`--set care.hunger.window_minutes=45`) para comparar variantes sin tocar los
ficheros.

## Validación

- **Al cargar** (`loadRuleset`): el manifiesto, que cada tabla, sección y script exista, `extends`, la sección `clock`
  y las comprobaciones de los sistemas. Los errores dicen el fichero, la línea y la columna.
- **`dwx validate --with <content>/base content/packs/game`**: el manifiesto y cada sección contra su esquema, y las
  referencias del manifiesto (tablas, secciones, scripts y `extends`) con `assetReference`, así que una tabla que
  falta en el paquete base es un problema.
- **Test del disco** (`engine/ruleset`, con `DWE_CONTENT`): cada tabla de `original` está en el paquete base
  extraído y cumple su esquema (R3.2).
