# dwe

Motor nativo en C++ que reimplementa un clásico RPG de crianza de PS1, sin las limitaciones del hardware original, y
que sirve de base para un mod con lógica, menús, historia y modos de juego nuevos.

> **Estado:** fase 0. Por ahora el repositorio solo contiene la arquitectura, las decisiones y el plan.
> `dwe` es un nombre provisional.

## Qué es y qué no es

- **Es** un motor propio: render con Vulkan, scripting con Lua y contenido en formatos abiertos.
- **No contiene** ningún asset, código ni dato del juego original. Cada usuario aporta su propia copia del disco y el
  motor extrae los datos en su máquina (ver [docs/legal/assets.md](docs/legal/assets.md)).
- **No está afiliado** con los titulares de la licencia original ni cuenta con su respaldo. Todas las marcas
  pertenecen a sus dueños.

## Compilar y jugar

Solo necesitas Docker y una GPU con Vulkan 1.3. Los detalles están en
[Preparar el entorno](docs/getting-started/setup.md).

```bash
scripts/dev.sh image && scripts/dev.sh configure && scripts/dev.sh build && scripts/dev.sh run
```

## Documentación

Empieza por [docs/README.md](docs/README.md). Ahí se explica qué leer según tu rol.

- Arquitectura: [docs/architecture/overview.md](docs/architecture/overview.md)
- Decisiones: [docs/adr/](docs/adr/README.md)
- Plan por fases: [docs/roadmap.md](docs/roadmap.md)
- Cómo contribuir: [CONTRIBUTING.md](CONTRIBUTING.md)

## Licencia

Por definir (ver [ADR 0011](docs/adr/0011-politica-legal-de-assets.md)).
