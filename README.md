# dwe

Motor nativo en C++26 que reimplementa un clásico RPG de crianza de finales de los noventa, sin las limitaciones del
hardware original, y que sirve de base para mods con lógica, menús, historia y modos de juego nuevos. `dwe` es un
nombre provisional.

- **No contiene** ningún asset, código ni dato del juego original. Cada usuario aporta su propia copia del disco y el
  motor extrae los datos en su máquina ([política legal de assets](docs/legal/assets.md)).
- **No está afiliado** con los titulares de la licencia original ni cuenta con su respaldo. Todas las marcas
  pertenecen a sus dueños.

## Compilar y jugar

Solo necesitas Docker y una GPU con Vulkan 1.3; todo lo demás está en la imagen de desarrollo
([Preparar el entorno](docs/getting-started/setup.md)).

```bash
scripts/dev.sh image && scripts/dev.sh configure && scripts/dev.sh build && scripts/dev.sh run
```

## Documentación

Toda la documentación, en español, es un sitio de VitePress hecho desde [`docs/`](docs/index.md): arquitectura,
módulos, formatos, guías, convenciones y cómo contribuir.

```bash
scripts/dev.sh docs-dev   # el sitio en http://localhost:5173, recargando al editar
scripts/dev.sh docs       # el sitio en build/docs-site; falla con un enlace roto
```

## Licencia

Por definir (ver [ADR 0011](docs/adr/0011-politica-legal-de-assets.md)).
