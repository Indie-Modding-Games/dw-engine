# Auditoría de #406: modo de combate Folder

Revisado en la rama `feat/battle-modes` (PR #410), oleada w135. La tabla completa con evidencias está en
[la versión en inglés](../../../architecture/audits/406-audit.md); la auditoría por mecánica, las decisiones y el plan
están en [Folder](../folder-battle.md).

| Bloque | Antes | Después |
|---|---|---|
| Criterios de aceptación (25) | 13 Done, 12 Partial | 23 Done, 2 Partial |
| Entregables (8) | 3 Done, 5 Partial | 8 Done |
| Huecos de esta oleada (4) | 4 Missing | 4 Done |
| Total (37) | 16 Done, 17 Partial, 4 Missing | 35 Done, 2 Partial |

Lo hecho en esta oleada:

- **Catálogo de técnicas:** funciones, área (en píxeles), fuego amigo, clase de alcance y coste por técnica, leídos de
  la tabla de movimientos y ajustables con `folder.catalog`. Las técnicas de área alcanzan a todos los que están dentro,
  aliados incluidos si son ofensivas.
- **Costes de MP** según costo-mp.md v0.2 (`folder.mp_costs`); la interfaz los lee con `Battle::partnerMoveCost` y
  `Battle::folderTechnique`. Se quita el descuento por cerebro (la INT solo procesa).
- **Bonus de bloqueo por distancia** (§7.4), provisional: 10 puntos a quemarropa, 0 a 800 px.
- **Checksum** con todo el estado de Folder.
- **Dificultad del co-op en Folder** documentada (folder-battle.md y coop-difficulty.md §9.4) y con test.

Queda parcial: Guardia no se interpone, y las skins aún no dibujan el área ni el fuego amigo. Todos los valores nuevos
son provisionales. Sin prueba en el juego ni contra el oráculo.
