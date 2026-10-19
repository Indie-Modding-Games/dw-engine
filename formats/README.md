# formats

Lectores de los formatos del original. Solo lo usa `apps/dwx`; el runtime nunca depende de este módulo
([reglas de dependencias](../docs/architecture/dependency-rules.md), [plan de la librería](../docs/architecture/formats.md)).

- `disc`: identificación de la copia del usuario por SHA-1 (`knownReleases`, `findKnownRelease`).
