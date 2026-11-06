# Commits y pull requests

## Commits

- **Un pull request, un commit en `main`.** La fusión *squash* junta los commits de la rama, así que dentro de ella
  se hacen tantos como haga falta. El stash se comparte entre worktrees, así que no se usa para guardar el progreso.
- **Antes de abrir el pull request pasa `scripts/dev.sh check-commit`**; `check-full` es opcional y mira más que la
  CI.
- El mensaje es una frase en **inglés**, en imperativo, que dice qué cambia: `Add the virtual file system`,
  `Load map collision from packs`.
- Cada commit lleva `Signed-off-by` (`git commit -s`) según el DCO (ver [Cómo contribuir](../contribuir.md)).
- Sin nombres de la franquicia, de consolas ni de empresas en los mensajes de commit ni en los títulos de los PR. Se
  usan términos genéricos.

## Pull requests

- Ramas cortas desde `main`. Se hace squash merge, y el commit resultante lleva el título del PR.
- **La descripción, lo más corta posible:** una o dos frases sobre qué hace y una línea sobre cómo se comprobó.
- Las comprobaciones de la CI (`linux`, `windows`, `launcher-version`) tienen que pasar; la aprobación de una
  revisión no es obligatoria.
- Si el PR cambia la arquitectura, una API, un formato o una regla, trae su cambio en `docs/` y, si hace falta, un ADR.
