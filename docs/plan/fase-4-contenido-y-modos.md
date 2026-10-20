# Fase 4: Contenido y modos

## Objetivo

El juego completo con su historia reescrita, personajes con más profundidad, modos de juego nuevos y una API para que
cualquiera pueda hacer mods. Termina con la publicación de la primera versión del mod, y a partir de ahí sigue
abierta.

## Por qué

Es lo que justifica el proyecto: cambiar la historia, dar profundidad a los personajes y crear modos nuevos. Va al
final porque se construye sobre todos los sistemas anteriores.

## Alcance

- **Historia:** los scripts de eventos originales se traducen a Lua como punto de partida y luego se reescriben.
- **Personajes:** árboles de diálogo, afinidad y misiones.
- **Modos de juego:** un framework donde cada modo combina los sistemas con su propio ruleset.
- **API de mods:** paquetes que añaden o sustituyen contenido por ID, y hooks de Lua versionados y en sandbox.
- **Más contenido:** más Digimon y mapas, fondos en alta resolución (capa `remaster`).
- **Editor de mapas**, adaptado al formato fuente.

## Fuera de alcance

- Distribuir contenido original del juego: cada usuario extrae su disco
  ([política legal](../legal/assets.md)). Las mejoras de arte hechas a partir del original se distribuyen como
  receta, no como imágenes.

## Requisitos

| ID | Requisito | Cómo se comprueba |
|---|---|---|
| R4.1 | Los scripts de eventos del original se convierten a Lua legible y el juego los ejecuta | Test que recorre los eventos convertidos |
| R4.2 | Un mod es un paquete que añade o sustituye contenido por ID, sin tocar el motor | Mod de ejemplo en los tests |
| R4.3 | Los mods solo ven la API de Lua, versionada y en sandbox ([ADR 0004](../adr/0004-lua-y-sol2.md)) | Tests del sandbox |
| R4.4 | Añadir un Digimon o un mapa nuevo no exige cambiar código | Paquete de ejemplo con un Digimon nuevo |
| R4.5 | Cada modo de juego declara su ruleset y los sistemas que usa | Dos modos de ejemplo |
| R4.6 | La documentación de modding (`docs/modding/`) explica la API y los formatos | Revisión |
| R4.7 | El editor de mapas lee y escribe el formato fuente | Tests de ida y vuelta |

## Diseño

- Módulos: `game/story`, `game/modes`, la API de Lua para mods y `apps/editor`.
- Las capas del VFS (base, remaster, mods, usuario) de la fase 1 ya permiten que un mod gane a la base por ID.

## Criterio de cierre

Se publica la primera versión del mod. La fase sigue abierta después para nuevo contenido.

## Dependencias y riesgos

- Depende de la fase 3 y del GDD (historia y personajes).
- Riesgo: el alcance. Por eso la fase es continua y la primera versión se define en el GDD.
- Riesgo: Digimon de otros juegos. Se resuelve con un importador desde el disco del usuario o con modelos propios del
  equipo.

## Estado (2026-10-19)

Pendiente.
