---
layout: home

hero:
  name: dwe
  text: Documentación técnica
  tagline: Motor nativo en C++26 que reimplementa un clásico RPG de crianza y sirve de base para mods.
  actions:
    - theme: brand
      text: Preparar el entorno
      link: /getting-started/setup
    - theme: alt
      text: Visión general
      link: /architecture/overview
    - theme: alt
      text: Cómo contribuir
      link: /contribuir

features:
  - title: Arquitectura
    details: Capas, reglas de dependencias, paquetes y sistema de ficheros virtual, render, rulesets y partidas.
    link: /architecture/overview
  - title: Módulos
    details: Qué hace cada módulo del motor, del juego y de las aplicaciones, y cómo se usa su API.
    link: /modulos/
  - title: Formatos del original
    details: Cómo lee dwx el disco del usuario y en qué formatos abiertos deja cada cosa.
    link: /formats/
  - title: Guías
    details: Tareas concretas, del gate de subida a las simulaciones de balance y la traducción.
    link: /guides/gate
---

Esta documentación es la fuente de verdad técnica del proyecto. Se trata como código: se revisa en los PR y se
actualiza en el mismo PR que cambia el código (ver [Cómo contribuir](contribuir.md)).

El diseño del juego (GDD, historia, personajes) vive en el repositorio de diseño del equipo. Aquí solo va lo técnico.

Para empezar: [Ejecutar el juego](getting-started/jugar.md) · [Extensiones y archivos](formats/extensiones.md).

## Por dónde empezar según tu rol

| Rol | Lee primero |
|---|---|
| Todo el equipo | [Visión general](architecture/overview.md) · [Política legal de assets](legal/assets.md) · [Reglas o tablas del original](legal/auditoria-rulesets.md) · [Plan](roadmap.md) · [Especificaciones por fase](plan/index.md) · [Glosario](glossary.md) |
| Programación | [Preparar el entorno](getting-started/setup.md) · [Estructura del repositorio](architecture/repository-layout.md) · [Reglas de dependencias](architecture/dependency-rules.md) · [Módulos](modulos/index.md) · [Formatos del original y `dwx`](architecture/formats.md) · [Paquetes y sistema de ficheros virtual](architecture/vfs.md) · [Tubería del remaster](architecture/remaster-pipeline.md) · [Espacio del mundo](architecture/world-space.md) · [Compatibilidad con las herramientas del equipo](architecture/herramientas-externas.md) · [JSON Schema](architecture/json-schema.md) · [Convenciones de código](conventions/code.md) · [Render](architecture/render.md) · [Modelos y animación](architecture/models.md) · [Rulesets](architecture/rulesets.md) · [Partidas guardadas](architecture/save.md) |
| Arte y audio | [Tubería de assets](architecture/assets-pipeline.md) · [Audio en el motor](architecture/audio.md) · [Tubería del remaster](architecture/remaster-pipeline.md) · [Herramientas del equipo](architecture/herramientas-externas.md) · [Paquetes](architecture/vfs.md) · [Política legal de assets](legal/assets.md) |
| Diseño y guion | [Ciclo de diseño](conventions/design-cycle.md) · [Fichas de referencia](reference/index.md) ([cuidado](reference/care.md), [evolución](reference/evolution.md), [batalla](reference/battle.md)) · [Visión general](architecture/overview.md) · [Tubería de assets](architecture/assets-pipeline.md) |
| Quien quiera proponer un cambio grande | [Cómo funcionan los ADR](adr/index.md) |

## Cómo está organizada

Seguimos el marco [Diátaxis](https://diataxis.fr/), que separa los documentos según lo que necesita quien los lee:

| Tipo | Para qué | Carpeta |
|---|---|---|
| Tutorial | Aprender desde cero | [`getting-started/`](getting-started/setup.md) |
| Guía práctica | Hacer una tarea concreta | `guides/`: el [visor de assets](guides/visor-de-assets.md), la [prueba vertical](guides/prueba-vertical.md), [pasar el gate](guides/gate.md), las [simulaciones](guides/simulaciones.md), la [traza de estado](guides/traza-de-estado.md) para comparar con el original, el [control remoto](guides/control-remoto.md) del juego en marcha, la [beta para Linux](guides/beta-linux.md) y [traducir los textos del original](guides/traducir.md), con su [glosario](guides/glosario-es.md) |
| Referencia | Consultar un detalle exacto | [`reference/`](reference/index.md) (el original y la [cobertura del port](reference/port-coverage.md)), [`formats/`](formats/index.md) (los formatos del disco), [`modulos/`](modulos/index.md) (la API de cada módulo), `modding/` |
| Explicación | Entender por qué es así | [`architecture/`](architecture/overview.md), [`adr/`](adr/index.md) |

Además: `conventions/` (normas del equipo: [código](conventions/code.md), [commits](conventions/commits.md),
[ciclo de diseño](conventions/design-cycle.md)), `legal/` (qué se puede publicar: [assets](legal/assets.md)),
[`roadmap.md`](roadmap.md) y [`glossary.md`](glossary.md).

## Normas

- Toda la documentación vive en `docs/` y forma este sitio (VitePress). No hay README por módulo: cada módulo tiene su
  página en [`modulos/`](modulos/index.md).
- Markdown, una idea por sección y frases cortas, en español.
- Los diagramas van en [Mermaid](https://mermaid.js.org/), como texto: el sitio y GitHub los dibujan y se revisan en
  los PR.
- Un ADR aceptado no se reescribe: se sustituye con otro nuevo.
- `scripts/dev.sh docs` construye el sitio y falla con un enlace roto; el check de la punta de cada rama lo pasa.
  `scripts/dev.sh docs-dev` lo sirve en local mientras escribes.
