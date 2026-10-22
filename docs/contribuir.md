# Cómo contribuir

## Antes de empezar

1. Lee la [portada de la documentación](index.md) y la página de tu rol.
2. Lee la [política legal de assets](legal/assets.md). No es opcional: un solo fichero del juego original en el
   repositorio pone en riesgo todo el proyecto.

## Flujo de trabajo

- **Trunk-based:** ramas cortas desde `main`, un tema por rama, PR con revisión y CI obligatoria, y squash merge.
- **Commits:** una frase en imperativo que diga qué cambia (ver [convenciones de commits](conventions/commits.md)).
- **DCO:** cada commit lleva `Signed-off-by` (`git commit -s`). Con él certificas que el código o el asset es tuyo o
  que tienes derecho a aportarlo, según el [Developer Certificate of Origin](https://developercertificate.org/).

## Un PR está terminado cuando

- compila y pasa la CI (formato, clang-tidy, tests): `scripts/dev.sh check-full` pasa lo mismo en local, y
  `scripts/dev.sh check-commit` lo que cada commit debe cumplir
  ([Preparar el entorno](getting-started/setup.md#comprobar-antes-de-un-pr));
- trae tests del comportamiento que cambia;
- **actualiza `docs/`** si cambia la arquitectura, una API, un formato o una regla;
- trae un ADR si toma una decisión de arquitectura (ver [cómo funcionan los ADR](adr/index.md));
- no añade ningún asset original. Cada asset del equipo lleva su entrada en `provenance.yaml`.

## Idioma

- Código, identificadores, comentarios y mensajes de commit: **inglés**.
- Documentación: **español**.
