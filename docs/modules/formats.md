# formats

Readers for the original's formats. Only `apps/dwx` uses it; the runtime never depends on this module
([dependency rules](../architecture/dependency-rules.md), [library plan](../architecture/formats.md)).

- `ByteReader`: little-endian reading with bounds checking, for all the readers.
- `disc`: identification of the user's copy by SHA-1 (`knownReleases`, `findKnownRelease`), BIN/CUE or ISO image
  by sectors, ISO9660, boot executable and the executable's file table ([Disc](../formats/disc.md)).
- `exe`: executable header and mapping of memory addresses to file offsets.
  `MemoryImage` reads executables and overlays by address; `ExecutableLayout` gives the tables of each version and
  `readExecutableTables` reads them; `tablesToYaml` writes them as YAML with text ids
  ([Tables](../formats/tables.md)).
- `image`: TIM (`readTim`, `decodeTim`), stacked TIMs (`readStackedTims`), fixed-step packs (`knownPacks`,
  `splitPack`) and deterministic PNGs (`encodePngRgba`, `encodePngIndexed`)
  ([TIM and packs](../formats/tim.md)).
- `model`: TMD models, MMD packages, skeletons, animation simulation and binary glTF (`.glb`) writing
  ([Models and animations](../formats/models.md)).
- `map`: `.MAP` (`readMapFile`), `.TFS` (`readTfsFile`), palette-based background composition (`composeBackgrounds`) and map
  YAML (`mapToYaml`) ([Maps](../formats/maps.md)).
- `script`: `.SCN` (`readScriptTable`, `readScriptHeader`), disassembly and YAML listing (`scriptToYaml`), listing reading for
  flow control (`readScriptListing`), block and routine graph (`buildScriptFlow`) and jump-free structuring
  (`structureScript`, with `measureStructure` for the figures) ([Scripts](../formats/scripts.md)).
