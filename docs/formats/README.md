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
