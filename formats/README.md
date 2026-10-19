# formats

Lectores de los formatos del original. Solo lo usa `apps/dwx`; el runtime nunca depende de este módulo
([reglas de dependencias](../docs/architecture/dependency-rules.md), [plan de la librería](../docs/architecture/formats.md)).

- `ByteReader`: lectura little-endian con comprobación de límites, para todos los lectores.
- `disc`: identificación de la copia del usuario por SHA-1 (`knownReleases`, `findKnownRelease`), imagen BIN/CUE o ISO
  por sectores, ISO9660, ejecutable de arranque y tabla de ficheros del ejecutable ([Disco](../docs/formats/disco.md)).
- `exe`: cabecera del ejecutable y paso de direcciones de memoria a offsets del fichero.
- `image`: TIM (`readTim`, `decodeTim`), TIM apilados (`readStackedTims`), packs de paso fijo (`knownPacks`,
  `splitPack`) y PNG deterministas (`encodePngRgba`, `encodePngIndexed`)
  ([TIM y packs](../docs/formats/tim.md)).
