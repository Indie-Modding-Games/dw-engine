# Formatos

Referencia de los formatos del original tal como los lee `formats/`. Cada página describe lo que está **implementado**:
la estructura, las comprobaciones que hace el lector y dónde lo lee el original en el decomp (`dw_decomp`). El plan
general está en [Formatos del original y `dwx`](../architecture/formats.md).

Ninguna página contiene datos del juego: solo estructuras, nombres de fichero y referencias al decomp
([política legal](../legal/assets.md)).

| Página | Qué cubre | Código |
|---|---|---|
| [Disco](disco.md) | Imagen BIN/CUE o ISO, sectores, ISO9660, ejecutable y tabla de ficheros, `dwx extract --list` | `formats/disc`, `formats/exe` |
| [TIM y packs](tim.md) | TIM, TIM apilados, packs de paso fijo y PNG del extractor | `formats/image` |
| [Modelos y animaciones](modelos.md) | TMD, MMD, esqueletos, simulación de animaciones y escritura en glTF | `formats/model` |
| [Tablas](tablas.md) | Tablas de datos del ejecutable por versión, overlays, escape de textos, YAML con ids de texto y esquemas | `formats/exe`, `content/schemas` |
| [Mapas](mapas.md) | `MAP_ENTRIES` y tablas de mapa, `.MAP`, `.TFS`, composición del fondo por paleta, YAML del mapa y colisión | `formats/map`, `content/schemas/map.schema.json` |
| [`dwx extract`](extract.md) | Paquete base, ids de asset, extractores e informe de cobertura | `apps/dwx` |
