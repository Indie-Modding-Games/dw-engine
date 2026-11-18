---
layout: home

hero:
  name: dwe
  text: Documentación técnica
  tagline: Motor nativo en C++26 que reimplementa un clásico RPG de crianza y sirve de base para mods.
  actions:
    - theme: brand
      text: Preparar el entorno
      link: /es/getting-started/setup
    - theme: alt
      text: Visión general
      link: /es/architecture/overview
    - theme: alt
      text: Cómo contribuir
      link: /es/contributing

features:
  - title: Arquitectura
    details: Capas, reglas de dependencias, paquetes y sistema de ficheros virtual, render, rulesets y partidas.
    link: /es/architecture/overview
  - title: Módulos
    details: Qué hace cada módulo del motor, del juego y de las aplicaciones, y cómo se usa su API.
    link: /es/modules/
  - title: Formatos del original
    details: Cómo lee dwx el disco del usuario y en qué formatos abiertos deja cada cosa.
    link: /es/formats/
  - title: Guías
    details: Tareas concretas, de las rutas de autotest y la comprobación del entrenamiento a la traducción.
    link: /es/guides/vertical-slice
  - title: Versiones y beta
    details: Una versión de prueba por cada fusión que cambia código, la estable promovida a mano y el lanzador con sus dos canales.
    link: /es/modules/apps/launcher#publicar-una-versión
---

Esta documentación es la fuente de verdad técnica del proyecto. Se trata como código: se revisa en los PR y se
actualiza en el mismo PR que cambia el código (ver [Cómo contribuir](contributing.md)).

El diseño del juego (GDD, historia, personajes) vive en el repositorio de diseño del equipo. Aquí solo va lo técnico.

Para empezar: [Ejecutar el juego](getting-started/play.md) · [Extensiones y archivos](formats/extensions.md).

## Por dónde empezar según tu rol

| Rol | Lee primero |
|---|---|
| Todo el equipo | [Visión general](architecture/overview.md) · [Política legal de assets](legal/assets.md) · [Reglas o tablas del original](legal/ruleset-audit.md) · [Plan](roadmap.md) · [Glosario](glossary.md) |
| Programación | [Preparar el entorno](getting-started/setup.md) · [Estructura del repositorio](architecture/repository-layout.md) · [Reglas de dependencias](architecture/dependency-rules.md) · [Módulos](modules/index.md) · [Formatos del original y `dwx`](architecture/formats.md) · [Paquetes y sistema de ficheros virtual](architecture/vfs.md) · [Alta definición](architecture/high-definition.md) · [Espacio del mundo](architecture/world-space.md) · [Compatibilidad con las herramientas del equipo](architecture/external-tools.md) · [JSON Schema](architecture/json-schema.md) · [Convenciones de código](conventions/code.md) · [Render](architecture/render.md) · [Modelos y animación](architecture/models.md) · [Rulesets](architecture/rulesets.md) · [Partidas guardadas](architecture/save.md) · [Controles](architecture/controls.md) |
| Arte y audio | [Tubería de assets](architecture/assets-pipeline.md) · [Audio en el motor](architecture/audio.md) · [Alta definición](architecture/high-definition.md) · [Herramientas del equipo](architecture/external-tools.md) · [Paquetes](architecture/vfs.md) · [Política legal de assets](legal/assets.md) |
| Diseño y guion | [Ciclo de diseño](conventions/design-cycle.md) · [Fichas de referencia](reference/index.md) ([cuidado](reference/care.md), [evolución](reference/evolution.md), [batalla](reference/battle.md)) · [Visión general](architecture/overview.md) · [Tubería de assets](architecture/assets-pipeline.md) |
| Probar la beta y publicar | [Lanzador](modules/apps/launcher.md) ([canales](modules/apps/launcher.md#canal-de-actualizaciones), [publicar una versión](modules/apps/launcher.md#publicar-una-versión)) · [Pasarela de la beta](modules/services/beta-gateway.md) · [Beta para Linux](guides/beta-linux.md) · [Beta para Windows y macOS](guides/beta-windows-macos.md) · [Imágenes personalizadas](guides/custom-images.md) |
| Quien quiera proponer un cambio grande | [Cómo funcionan los ADR](adr/index.md) |

## Cómo está organizada

Seguimos el marco [Diátaxis](https://diataxis.fr/), que separa los documentos según lo que necesita quien los lee:

| Tipo | Para qué | Carpeta |
|---|---|---|
| Tutorial | Aprender desde cero | [`getting-started/`](getting-started/setup.md) |
| Guía práctica | Hacer una tarea concreta | `guides/`: el [visor de assets](guides/asset-viewer.md), la [prueba vertical](guides/vertical-slice.md), [pasar el gate](guides/gate.md), las [simulaciones](guides/simulations.md), la [traza de estado](guides/state-trace.md) para comparar con el original, la [comprobación del entrenamiento](guides/check-training.md), el [control remoto](guides/remote-control.md) del juego en marcha, la [beta para Linux](guides/beta-linux.md), la [beta para Windows y macOS](guides/beta-windows-macos.md), los [complementos del lanzador](guides/launcher-addons.md), las [imágenes personalizadas](guides/custom-images.md), [cómo funcionan las evoluciones](guides/evolutions.md) y [traducir los textos del original](guides/translate.md), con su [glosario](guides/spanish-glossary.md) |
| Referencia | Consultar un detalle exacto | [`reference/`](reference/index.md) (el original y la [cobertura del port](reference/port-coverage.md)), [`packages/formats/`](formats/index.md) (los formatos del disco), [`modules/`](modules/index.md) (la API de cada módulo), `modding/` |
| Explicación | Entender por qué es así | [`architecture/`](architecture/overview.md), [`adr/`](adr/index.md) |

Además: `conventions/` (normas del equipo: [código](conventions/code.md), [commits](conventions/commits.md),
[ciclo de diseño](conventions/design-cycle.md)), `legal/` (qué se puede publicar: [assets](legal/assets.md)),
[`roadmap.md`](roadmap.md) y [`glossary.md`](glossary.md).

## Normas

- Toda la documentación vive en `docs/` y forma este sitio (VitePress). Cada módulo tiene su página en
  [`modules/`](modules/index.md); el README junto a su código es corto y enlaza a ella. Las herramientas que no son
  parte del motor (`apps/studio`, `apps/beta-gateway`, `apps/versus-signal`, `apps/mapper`) guardan un README más largo
  con los pasos para usarlas o desplegarlas.
- Markdown, una idea por sección y frases cortas, en inglés. `docs/es/` tiene la traducción al español, con los mismos
  nombres de fichero y títulos.
- Los diagramas van en [Mermaid](https://mermaid.js.org/), como texto: el sitio y GitHub los dibujan y se revisan en
  los PR.
- Un ADR aceptado no se reescribe: se sustituye con otro nuevo.
- `scripts/dev.sh docs` construye el sitio y falla con un enlace roto; `scripts/dev.sh check-full` lo pasa, y un pull
  request que cambia `docs/` lo construye también (sin publicarlo). `scripts/dev.sh docs-dev` lo sirve en local mientras
  escribes.
