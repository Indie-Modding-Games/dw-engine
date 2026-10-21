# Documentación

Esta carpeta es la fuente de verdad técnica del proyecto. La documentación se trata como código: se revisa en los PR
y se actualiza en el mismo PR que cambia el código (ver [CONTRIBUTING.md](../CONTRIBUTING.md)).

El diseño del juego (GDD, historia, personajes) vive en el repositorio de diseño `Digimon-World-Project-Folder`.
Aquí solo va lo técnico.

## Por dónde empezar según tu rol

| Rol | Lee primero |
|---|---|
| Todo el equipo | [Visión general](architecture/overview.md) · [Política legal de assets](legal/assets.md) · [Reglas o tablas del original](legal/auditoria-rulesets.md) · [Plan](roadmap.md) · [Especificaciones por fase](plan/README.md) · [Glosario](glossary.md) |
| Programación | [Preparar el entorno](getting-started/setup.md) · [Estructura del repositorio](architecture/repository-layout.md) · [Reglas de dependencias](architecture/dependency-rules.md) · [Formatos del original y `dwx`](architecture/formats.md) · [Paquetes y sistema de ficheros virtual](architecture/vfs.md) · [Tubería del remaster](architecture/remaster-pipeline.md) · [Espacio del mundo](architecture/world-space.md) · [Compatibilidad con las herramientas del equipo](architecture/herramientas-externas.md) · [JSON Schema](architecture/json-schema.md) · [Convenciones de código](conventions/code.md) · [Render](architecture/render.md) · [Modelos y animación](architecture/models.md) · [Rulesets](architecture/rulesets.md) · [Partidas guardadas](architecture/save.md) |
| Arte y audio | [Tubería de assets](architecture/assets-pipeline.md) · [Tubería del remaster](architecture/remaster-pipeline.md) · [Herramientas del equipo](architecture/herramientas-externas.md) · [Paquetes](architecture/vfs.md) · [Política legal de assets](legal/assets.md) |
| Diseño y guion | [Ciclo de diseño](conventions/design-cycle.md) · [Fichas de referencia](reference/README.md) ([cuidado](reference/care.md), [evolución](reference/evolution.md), [batalla](reference/battle.md)) · [Visión general](architecture/overview.md) · [Tubería de assets](architecture/assets-pipeline.md) |
| Quien quiera proponer un cambio grande | [Cómo funcionan los ADR](adr/README.md) |

## Cómo está organizada

Seguimos el marco [Diátaxis](https://diataxis.fr/), que separa los documentos según lo que necesita quien los lee:

| Tipo | Para qué | Carpeta |
|---|---|---|
| Tutorial | Aprender desde cero | [`getting-started/`](getting-started/setup.md) |
| Guía práctica | Hacer una tarea concreta | `guides/` (llega con cada sistema; ya están el [visor de assets](guides/visor-de-assets.md), la [prueba vertical](guides/prueba-vertical.md), las [simulaciones](guides/simulaciones.md) y [traducir los textos del original](guides/traducir.md), con su [glosario](guides/glosario-es.md)) |
| Referencia | Consultar un detalle exacto | [`reference/`](reference/README.md) (el original), [`formats/`](formats/README.md) (los formatos del disco, fase 1), `modding/` (fase 4) |
| Explicación | Entender por qué es así | [`architecture/`](architecture/), [`adr/`](adr/README.md) |

Además: [`conventions/`](conventions/) (normas del equipo), [`legal/`](legal/) (qué se puede publicar),
[`roadmap.md`](roadmap.md) y [`glossary.md`](glossary.md).

## Normas

- Markdown, una idea por sección y frases cortas.
- Los diagramas van en [Mermaid](https://mermaid.js.org/), como texto, para que GitHub los dibuje y se puedan revisar
  en los PR.
- Un ADR aceptado no se reescribe: se sustituye con otro nuevo.
- La CI comprobará enlaces rotos (lychee) y formato (markdownlint).
