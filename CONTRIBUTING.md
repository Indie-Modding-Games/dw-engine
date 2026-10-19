# Cómo contribuir

## Antes de empezar

1. Lee [docs/README.md](docs/README.md) y la página de tu rol.
2. Lee la [política legal de assets](docs/legal/assets.md). No es opcional: un solo fichero del juego original en el
   repositorio pone en riesgo todo el proyecto.

## Flujo de trabajo

- **Trunk-based:** ramas cortas desde `main`, un tema por rama, PR con revisión y CI obligatoria, y squash merge.
- **Commits:** una frase en imperativo que diga qué cambia (ver [convenciones de commits](docs/conventions/commits.md)).
- **DCO:** cada commit lleva `Signed-off-by` (`git commit -s`). Con él certificas que el código o el asset es tuyo o
  que tienes derecho a aportarlo, según el [Developer Certificate of Origin](https://developercertificate.org/).

## Un PR está terminado cuando

- compila y pasa la CI (formato, clang-tidy, tests);
- trae tests del comportamiento que cambia;
- **actualiza `docs/`** si cambia la arquitectura, una API, un formato o una regla;
- trae un ADR si toma una decisión de arquitectura (ver [docs/adr/README.md](docs/adr/README.md));
- no añade ningún asset original. Cada asset del equipo lleva su entrada en `provenance.yaml`.

## Idioma

- Código, identificadores, comentarios y mensajes de commit: **inglés**.
- Documentación: **español**.
