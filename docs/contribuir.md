# Cómo contribuir

## Antes de empezar

1. Lee la [portada de la documentación](index.md) y la página de tu rol.
2. Lee la [política legal de assets](legal/assets.md). No es opcional: un solo fichero del juego original en el
   repositorio pone en riesgo todo el proyecto.

## Flujo de trabajo

- **Una sola rama, `main`.** Cada cambio sale de una rama corta desde `main` (un tema por rama) y entra por un pull
  request a `main`. La fusión es siempre *squash*: un commit por pull request, con el título del pull request como
  mensaje (escríbelo como un commit). La rama se borra sola al fusionar.
- **Comprobaciones obligatorias** (`.github/workflows/ci.yml`), solo en pull requests y solo del commit más nuevo:

  | Comprobación | Qué mira | Tiempo con cachés |
  |---|---|---|
  | `linux` | formato y lint de los archivos que cambia el pull request, compilación (debug, avisos como errores) y todas las pruebas | ~8–10 min |
  | `windows` | la misma compilación para Windows (compilación cruzada con clang-cl), pruebas incluidas pero sin pasarlas | ~5–7 min |
  | `launcher-version` | si el lanzador cambió desde la versión publicada más nueva, su versión sube ([La versión del lanzador](modulos/apps/launcher.md#publicar-una-version)) | segundos |

  Un pull request que solo toca documentación no compila nada: `linux` y `windows` salen como omitidas, que cuenta como
  pasadas. Cuando cambia el lanzador, la pasarela o los scripts de publicación corre además `launcher`, sus pruebas.
  Fuera de los pull requests quedan clang-tidy, el sitio de la documentación y las pruebas en un Windows de verdad
  (`platform-check.yml`, cada lunes): `scripts/dev.sh check-full` los pasa en local.
- **Versiones:** cada fusión en `main` publica sola una versión de prueba (canal «Pruebas» del lanzador); una ya
  probada se promueve a estable a mano, con los mismos archivos. Un arreglo urgente sigue el mismo camino. Detalles en
  [Publicar una versión](modulos/apps/launcher.md#publicar-una-version).
- **Commits:** una frase en imperativo que diga qué cambia (ver [convenciones de commits](conventions/commits.md)).
- **DCO:** cada commit lleva `Signed-off-by` (`git commit -s`). Con él certificas que el código o el asset es tuyo o
  que tienes derecho a aportarlo, según el [Developer Certificate of Origin](https://developercertificate.org/).

## Un PR está terminado cuando

- pasa las comprobaciones obligatorias; en local, `scripts/dev.sh check-commit` compila y pasa las pruebas y
  `scripts/dev.sh check-full` añade formato, lint, clang-tidy y el sitio de la documentación
  ([Preparar el entorno](getting-started/setup.md#comprobar-antes-de-un-pr));
- si cambia el lanzador (`apps/launcher/ui` o `apps/launcher/src-tauri`), sube su versión en `tauri.conf.json`,
  `Cargo.toml` y `Cargo.lock`;
- trae tests del comportamiento que cambia;
- **actualiza `docs/`** si cambia la arquitectura, una API, un formato o una regla;
- trae un ADR si toma una decisión de arquitectura (ver [cómo funcionan los ADR](adr/index.md));
- no añade ningún asset original. Cada asset del equipo lleva su entrada en `provenance.yaml`.

## Idioma

- Código, identificadores, comentarios y mensajes de commit: **inglés**.
- Documentación: **español**.
