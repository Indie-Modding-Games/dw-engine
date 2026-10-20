# Partidas guardadas

Cómo guarda el motor una partida, cómo se versiona y cómo un sistema añade lo suyo. El código está en
[`game/save`](../../game/save/README.md) y cumple el requisito R3.6 de la
[fase 3](../plan/fase-3-nucleo-jugable.md): una partida guardada se carga igual tras actualizar el juego, o se migra.

## El fichero

Una partida es un documento YAML legible, escrito con `core::emitYaml`: la misma partida da siempre el mismo texto, y
un fichero leído y escrito otra vez sale idéntico byte a byte.

```yaml
format: dwe-save
format_version: 1
game_version: "0.1.0"
ruleset: "original"
label: "File City"
created: "2026-10-01T08:00:00Z"
updated: "2026-10-02T21:30:15Z"
play_seconds: 5400
sections:
  inventory:
    version: 1
    state:
      ...
  partner:
    version: 2
    state:
      ...
complete: true
```

| Clave | Qué es |
|---|---|
| `format` | Siempre `dwe-save`: distingue una partida de cualquier otro YAML |
| `format_version` | La versión de esta estructura (las claves de arriba y cómo se guardan las secciones) |
| `game_version` | La versión del juego que la escribió, para las personas y los informes de errores |
| `ruleset` | El ruleset con el que se juega |
| `label` | El texto de la lista de ranuras (el mapa, el nombre del partner...) |
| `created`, `updated` | Cuándo empezó la partida y cuándo se guardó por última vez, en UTC (`YYYY-MM-DDTHH:MM:SSZ`) |
| `play_seconds` | Tiempo de juego en segundos |
| `sections` | El estado guardado de cada sistema, ordenados por nombre, cada uno con su `version` y su `state` |
| `complete` | Siempre `true` y siempre la última clave: un fichero cortado por cualquier sitio da error en vez de perder sus últimas secciones |

El módulo nunca lee el reloj: quien guarda pone `created` y `updated`, así los tests y las repeticiones son
deterministas. Una clave desconocida o que falta es un error, como en el resto de YAML del motor.

## Versiones y migraciones

Hay dos niveles de versión, y cada uno tiene su **cadena de migraciones** (`MigrationChain`): una lista de pasos
`vN → vN+1` que transforman el YAML. La versión actual es la más antigua que se lee más el número de pasos, así que no
se puede subir una versión sin escribir su paso.

- **El formato** (`format_version`): sus pasos están en `formatMigrations()` en `game/save` y se aplican al leer el
  fichero, sobre el documento entero. Hoy es la versión 1, sin pasos.
- **Cada sección** (`sections.<nombre>.version`): la cadena la registra el sistema dueño de la sección y se aplica al
  cargar, sobre el `state` de la sección. Cada sistema versiona lo suyo sin tocar a los demás.

Al cargar:

1. Una versión **más nueva** que la que conoce el juego es un error claro: «section 'partner' is version 3, newer than
   this game reads (2): it was saved by a newer version of the game». Lo mismo para el formato.
2. Una versión más antigua que la primera que se lee (si un sistema retira migraciones muy viejas) también es un error.
3. Se migran **todas** las secciones antes de que ningún sistema lea la suya, así que un fichero que no se puede migrar
   no cambia nada.
4. Una **sección desconocida** (de un mod, o de un juego más nuevo) se guarda tal cual en el `SaveFile` y se escribe
   de vuelta sin tocar al guardar. Por eso el juego guarda sobre el `SaveFile` que cargó, no sobre uno nuevo.
5. Si la partida no tiene la sección de un sistema (se guardó antes de que existiera), se llama a su `readMissing`,
   que por defecto es un error y que un sistema puede usar para empezar desde su estado de partida nueva.

Los pasos usan `YamlNode` directamente; `withoutKey` y `renamedKey` cubren lo que su API no tiene (quitar y renombrar
claves en su sitio).

## Cómo añade un sistema su sección

El sistema implementa `save::SectionSerializer` en su propio módulo: `save` declara la interfaz y nunca incluye los
headers del sistema ([regla 4](dependency-rules.md)). La app une los dos.

```cpp
// game/partner (un ejemplo: los nombres son del partner).
class PartnerSave final : public dwe::game::save::SectionSerializer {
public:
    explicit PartnerSave(Partner& partner) : partner_{&partner} {}

    dwe::core::Result<dwe::core::YamlNode> write() const override;       // estado -> YAML, versión actual
    dwe::core::Result<void> read(const dwe::core::YamlNode& state) override; // YAML ya migrado -> estado

    // Versión 1: `name`; versión 2: `nickname`.
    static dwe::game::save::MigrationChain migrations() {
        dwe::game::save::MigrationChain chain;    // empieza en la versión 1
        chain.add([](dwe::core::YamlNode state) -> dwe::core::Result<dwe::core::YamlNode> {
            return dwe::game::save::renamedKey(state, "name", "nickname");
        });                                         // ahora la versión actual es 2
        return chain;
    }

private:
    Partner* partner_;
};

// En la app, una vez:
dwe::game::save::SaveSections sections;
sections.add("partner", partnerSave, PartnerSave::migrations());
sections.add("inventory", inventorySave);   // sin migraciones: versión 1
```

Para cambiar el formato de una sección:

1. Se cambia `write` y `read` para la versión nueva.
2. Se añade a la cadena el paso desde la versión anterior.
3. Se añade un test con un fichero de la versión anterior que se carga con el resultado esperado (como
   `game/save/tests/fixtures/partner_v1.yaml` y `partner_v2.yaml`).

En el juego esa unión es la partida, `session::GameSession` ([apps/session](../../apps/session/README.md)): registra
`clock`, `partner`, `tamer`, `field` y `random`, y cada sistema que se enchufa como adaptador añade las suyas. También
pide los autoguardados: el partner se duerme, día nuevo, cambio de mapa, fin de batalla; `AutosaveRequests` los junta
en uno y el ruleset dice cuáles cuentan.

Los nombres de sección son de 1 a 64 caracteres `a-z`, `0-9` y `_`. Un mod usa un prefijo propio (`mod_fishing`) para
no chocar con los del juego.

## Ranuras, autoguardado y copias

Cuántas ranuras hay lo dice `save/settings` del paquete `game`
([esquema](../../content/schemas/save_settings.schema.json)), y cuándo guarda el juego solo, la sección `save` del
ruleset ([esquema](../../content/schemas/ruleset_save.schema.json), [rulesets](rulesets.md)). Las ranuras son una
preferencia del juego y no cambian las reglas; el autoguardado sí es una regla, así que cada ruleset lo decide.

```yaml
# content/packs/game/save/settings.yaml
manual_slots: 15
autosave_slots: 3

# content/packs/game/rules/original/save.yaml
autosave:
  on_map_change: true
  on_battle_end: true
  on_partner_sleep: true
  on_new_day: false
```

- **Ranuras manuales**: `slot_01.yaml` a `slot_NN.yaml`. El original guardaba un fichero por ranura, hasta 15 por
  tarjeta de memoria (`main_menu.c: SAVEGAME_SLOT_INFO`).
- **Autoguardado**: `autosave_01.yaml`... Se escribe por turnos: la primera ranura vacía, si no una dañada, si no la
  guardada hace más tiempo (por `updated`). `autosave_slots: 0` lo apaga.
- **Cuándo**: el juego llama a `AutosaveRequests::requestAutosave(motivo)` en sus eventos, con un `AutosaveReason`:
  `MapChange` (cada cambio de mapa), `BattleEnd` (al terminar cada batalla, ganada o perdida), `PartnerSleep` (cuando
  el partner se duerme) y `NewDay` (cada día nuevo del reloj del juego). Solo cuentan los que el ruleset tiene a
  `true` (`AutosaveRules`, que se lee con `readAutosaveRules` y se comprueba con `saveSectionCheck`). Luego el bucle
  del juego recoge la petición con `takeRequest`, construye el `SaveFile` y llama a `SaveSlots::autosave`. Un ruleset
  sin sección `save` nunca autoguarda, como el original.
- **Escritura atómica**: se escribe `<fichero>.tmp`, se copia el fichero anterior a `<fichero>.bak` y se renombra el
  temporal sobre el fichero, que el sistema de ficheros hace en un paso. Si el juego se corta, la ranura tiene la
  partida anterior o la nueva, nunca media. La copia, y no un renombrado, hace que la partida nunca falte del disco.
- **Copia de seguridad**: `<fichero>.bak` es la partida anterior de la ranura. La lista de ranuras dice si existe, y
  `loadBackup` la carga cuando la partida está dañada.

**El autoguardado es una diferencia deliberada con el original**, que solo guardaba cuando el jugador lo pedía: al
dormir (`partner.c: partnerTickSleep`, «Save and go to sleep») o en la máquina de guardado de algunos mapas
(`main_menu.c: openSaveMachine`), y tras acabar el juego (`main_menu.c: gameClearSave`). Lo aprobó el responsable de
diseño el 2026-10-20 con los tres motivos de arriba, que `save.yaml` de `original` marca como «Desviación aprobada».
Un ruleset fiel al original las pone todas a `false`.

## Dónde están los ficheros

La app los guarda en la carpeta de datos del usuario que da el sistema (`platform::userDataFolder`, con
`SDL_GetPrefPath`), en `saves/`: `$XDG_DATA_HOME/indie-modding-games/dwe/saves/` en Linux (normalmente
`~/.local/share/...`) y `%APPDATA%\indie-modding-games\dwe\saves\` en Windows. `--saves <carpeta>` usa otra, y
`dwe-game --list-saves` escribe en el log el estado de cada ranura y sale.

`game/save` no usa `platform`: lee y escribe a través de `SaveStorage`, que la app crea (`DiskSaveStorage` con la
carpeta, `MemorySaveStorage` en los tests).

## Qué guardaba el original

Para los sistemas que añadan su sección: es la lista de lo que hay que poder guardar, no un formato que copiar. El
original copiaba su estado a un bloque al guardar (`main_menu.c: writeSavegame`), lo devolvía al cargar
(`main_menu.c: loadSavegame`) y tenía unos valores de partida nueva (`main_menu.c: initializeDefaultSavegame`).

| Grupo | Qué guarda |
|---|---|
| Progreso | El estado de la historia; las cartas; los disparadores de historia y de reclutas; en qué mapa está cada NPC; las cacas que hay en cada mapa (mapa, posición y tamaño) |
| Posición | El mapa y la salida actuales y los anteriores; dónde están el domador y el partner; el camino de clic del domador |
| Reloj | Año, día, hora y minuto del juego, y el tiempo jugado |
| Partner | Especie, técnicas aprendidas, estadísticas (ataque, defensa, velocidad, cerebro, PV, PM, técnicas y su prioridad), los parámetros de crianza (cuidado, hambre, energía, peso, vida, edad, errores de cuidado, batallas, temporizadores de evolución...), nombre y vidas |
| Domador | Nombre, nivel, Digimon criados, dinero, mérito, batallas empezadas y huidas, torneos ganados y perdidos |
| Inventario | Hasta 30 huecos de objeto con su cantidad, y cuántos huecos hay |
| Registro de batalla | Hasta 40 Digimon registrados para el modo versus (`main_menu.c: registerBattleData`) |

El fichero del original tenía la cabecera de la tarjeta de memoria (título e icono), dos copias de la partida (la
segunda como reserva) y una suma de bytes como comprobación (`main_menu.c: createSavegameChecksum`), sin número de
versión. Aquí la reserva es `<fichero>.bak` y la versión va en el fichero.
