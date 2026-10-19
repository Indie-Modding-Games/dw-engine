# Documentación

Esta carpeta es la fuente de verdad técnica del proyecto. La documentación se trata como código: se revisa en los PR
y se actualiza en el mismo PR que cambia el código (ver [CONTRIBUTING.md](../CONTRIBUTING.md)).

El diseño del juego (GDD, historia, personajes) vive en el repositorio de diseño `Digimon-World-Project-Folder`.
Aquí solo va lo técnico.

## Por dónde empezar según tu rol

| Rol | Lee primero |
|---|---|
| Todo el equipo | [Visión general](architecture/overview.md) · [Política legal de assets](legal/assets.md) · [Plan](roadmap.md) · [Glosario](glossary.md) |
| Programación | [Preparar el entorno](getting-started/setup.md) · [Estructura del repositorio](architecture/repository-layout.md) · [Reglas de dependencias](architecture/dependency-rules.md) · [Formatos del original y `dwx`](architecture/formats.md) · [Paquetes y sistema de ficheros virtual](architecture/vfs.md) · [Convenciones de código](conventions/code.md) |
| Arte y audio | [Tubería de assets](architecture/assets-pipeline.md) · [Paquetes](architecture/vfs.md) · [Política legal de assets](legal/assets.md) |
| Diseño y guion | [Ciclo de diseño](conventions/design-cycle.md) · [Fichas de referencia](reference/README.md) · [Visión general](architecture/overview.md) · [Tubería de assets](architecture/assets-pipeline.md) |
| Quien quiera proponer un cambio grande | [Cómo funcionan los ADR](adr/README.md) |

## Cómo está organizada

Seguimos el marco [Diátaxis](https://diataxis.fr/), que separa los documentos según lo que necesita quien los lee:

| Tipo | Para qué | Carpeta |
|---|---|---|
| Tutorial | Aprender desde cero | [`getting-started/`](getting-started/setup.md) |
| Guía práctica | Hacer una tarea concreta | `guides/` (llega con cada sistema) |
| Referencia | Consultar un detalle exacto | [`reference/`](reference/README.md) (el original), `formats/`, `modding/` (llegan en las fases 1 y 4) |
| Explicación | Entender por qué es así | [`architecture/`](architecture/), [`adr/`](adr/README.md) |

Además: [`conventions/`](conventions/) (normas del equipo), [`legal/`](legal/) (qué se puede publicar),
[`roadmap.md`](roadmap.md) y [`glossary.md`](glossary.md).

## Normas

- Markdown, una idea por sección y frases cortas.
- Los diagramas van en [Mermaid](https://mermaid.js.org/), como texto, para que GitHub los dibuje y se puedan revisar
  en los PR.
- Un ADR aceptado no se reescribe: se sustituye con otro nuevo.
- La CI comprobará enlaces rotos (lychee) y formato (markdownlint).
