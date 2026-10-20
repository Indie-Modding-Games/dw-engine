# save

Las partidas guardadas (`dwe::game::save`, capa `game`): el fichero YAML versionado, las secciones de cada sistema
con sus migraciones, las ranuras, el autoguardado y la escritura atómica con copia de seguridad. El formato, las
versiones y cómo añade un sistema su sección están en [Partidas guardadas](../../docs/architecture/save.md).

No conoce a los sistemas que guarda: cada uno implementa `SectionSerializer` en su módulo y la app lo registra. No
lee el reloj ni usa `platform`: la app le da las horas y un `SaveStorage` con la carpeta de las partidas.

## Uso

```cpp
namespace save = dwe::game::save;

// Al arrancar.
save::SaveSections sections;
sections.add("partner", partnerSave, PartnerSave::migrations());
auto settings = save::loadSaveSettings(vfs);                    // save/settings
save::DiskSaveStorage storage{savesFolder};
save::SaveSlots slots{storage, *settings};
auto autosaveRules = save::readAutosaveRules(store->current());   // sección `save` del ruleset
save::AutosaveRequests autosaves{*autosaveRules, settings->autosaveSlots};

// Cargar: la cabecera ya viene migrada; read migra las secciones y se las da a cada sistema.
auto file = slots.load({.kind = save::SlotKind::Manual, .index = 1});
if (file) {
    if (auto loaded = sections.read(*file); !loaded) { /* loaded.error().message */ }
    current = std::move(*file);              // se guarda encima: así se conservan las secciones desconocidas
}

// Guardar.
current.header.updated = now;                // y label, playSeconds...
sections.write(current);
slots.save({.kind = save::SlotKind::Manual, .index = 1}, current);

// Autoguardado: los sistemas piden, el ruleset decide y las peticiones cercanas dan un solo guardado.
autosaves.requestAutosave(save::AutosaveReason::MapChange);
autosaves.advance(tickSeconds);              // segundos reales de cada tick
if (auto reasons = autosaves.takeRequest()) {
    current.header.autosaveReasons = save::autosaveReasonNames(*reasons);
    sections.write(current);
    slots.autosave(current);
}
```

- **`SaveFile`**: la cabecera (`SaveHeader`) y las secciones por nombre (`SavedSection`: versión y estado en YAML).
  `parseSaveFile` y `writeSaveFile` leen y escriben el texto; una partida leída y escrita sale idéntica.
- **`MigrationChain`**: los pasos `vN → vN+1` de una sección o del formato. `withoutKey` y `renamedKey` ayudan a
  escribirlos.
- **`SaveSections`**: registra las secciones, las escribe en un `SaveFile` y las migra y reparte al cargar.
- **`SaveSlots`**: lista las ranuras (vacía, guardada o dañada, y si tiene copia), guarda, carga, carga la copia y
  hace el autoguardado por turnos.
- **`AutosaveRules`** y **`AutosaveRequests`**: qué motivos (`AutosaveReason`) autoguardan según la sección `save` del
  ruleset (`readAutosaveRules`, `saveSectionCheck` para el `RulesetStore`), y las peticiones pendientes, agrupadas en
  una ventana de `coalesce_seconds`. Qué evento de qué sistema pide cada motivo está en
  [save.md](../../docs/architecture/save.md#cuándo-autoguarda).
- **`text_key`** (`text_keys.hpp`): las claves de localización de los avisos de guardado y de los motivos.
- **`SaveSettings`**: `loadSaveSettings` y `parseSaveSettings` leen `save/settings`.
- **`SaveStorage`**: `DiskSaveStorage` (una carpeta) y `MemorySaveStorage` (tests, con `failNextWrite`).

## Datos

- `save/settings` (`content/packs/game/save/settings.yaml`,
  [esquema](../../content/schemas/save_settings.schema.json)): ranuras manuales y de autoguardado.
- La sección `save` del ruleset (`content/packs/game/rules/original/save.yaml`,
  [esquema](../../content/schemas/ruleset_save.schema.json)): cuándo autoguarda el juego.

## Tests

`save_file_tests.cpp`: ida y vuelta idéntica byte a byte, migración de `tests/fixtures/partner_v1.yaml` a
`partner_v2.yaml`, secciones desconocidas conservadas, versiones más nuevas o demasiado viejas, secciones que faltan,
migración del formato y ficheros corruptos o cortados por cualquier byte. `slots_tests.cpp`: escritura atómica y copia
en disco, ajustes (también los del paquete `game`), ranuras, guardado fallido y rotación del autoguardado.
`autosave_tests.cpp`: la sección `save` (también la de `original`), las peticiones de autoguardado, su agrupación, los
motivos en la cabecera y la rotación de las tres ranuras.
