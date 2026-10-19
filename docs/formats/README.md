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
| [Tablas](tablas.md) | Tablas de datos del ejecutable por versión, direcciones de los overlays, escape de textos, YAML con ids de texto y esquemas | `formats/exe`, `content/schemas` |
| [Overlays](overlays.md) | `*_REL.BIN`: cabecera, partes, ficheros incrustados y tablas de pesca, torneo, versus y entrenamiento | `formats/exe/overlay*`, `content/schemas` |
| [Mapas](mapas.md) | `MAP_ENTRIES` y tablas de mapa, `.MAP`, `.TFS`, composición del fondo por paleta, YAML del mapa y colisión | `formats/map`, `content/schemas/map.schema.json` |
| [Efectos](efectos.md) | `ETCHI/EFEDAT.EFE`: ranuras por técnica, cabecera, scripts y sus 97 subopcodes, área de datos, texturas en VRAM y modelos | `formats/effect` |
| [Audio](audio.md) | VHB, VAB, ADPCM, SEQ, bancos empaquetados y escritura en SF2, WAV y MIDI | `formats/audio` |
| [`dwx extract`](extract.md) | Paquete base, ids de asset, extractores e informe de cobertura | `apps/dwx` |
| [Textos y fuente](textos.md) | Juego de caracteres, cadenas de bytes y texto de script a UTF-8 con etiquetas, fuente a atlas PNG y manifiesto de cadenas | `formats/text` |
| [Scripts de eventos](scripts.md) | `MAPHEAD.SCN`, `DG.SCN`, cabecera, desensamblador, barrido y listado YAML | `formats/script` |

Los formatos propios del motor, los que escribe `dwx` para el estado cocinado:

| Página | Qué cubre | Código |
|---|---|---|
| [Archivo `.pak`](pak.md) | Un paquete en un solo fichero: cabecera, tabla de contenidos, SHA-1 por entrada, cómo se monta | `engine/assets` |
| [Texturas KTX2](ktx2.md) | Los campos de KTX2 que escribe `dwx cook` y cómo calcula los mipmaps | `apps/dwx` |
