# Commits y pull requests

## Commits

- **Un tema por commit.**
- El mensaje es una frase en **inglés**, en imperativo, que dice qué cambia: `Add the virtual file system`,
  `Load map collision from packs`.
- Cada commit lleva `Signed-off-by` (`git commit -s`) según el DCO (ver [CONTRIBUTING.md](../../CONTRIBUTING.md)).
- Sin nombres de la franquicia, de consolas ni de empresas en los mensajes de commit ni en los títulos de los PR. Se
  usan términos genéricos.

## Pull requests

- Ramas cortas desde `main`. Se hace squash merge, y el commit resultante es `Título (#N)`.
- **La descripción, lo más corta posible:** una o dos frases sobre qué hace y una línea sobre cómo se comprobó.
- La CI tiene que pasar y hace falta al menos una revisión aprobada.
- Si el PR cambia la arquitectura, una API, un formato o una regla, trae su cambio en `docs/` y, si hace falta, un ADR.
