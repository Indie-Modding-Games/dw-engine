# Auditoría de #351: combates compartidos según la presencia

Auditado el 2026-11-27 sobre `feat/battle-modes` (protocolo 14), después de la oleada de bugs (#411, #413, #418, #419,
#420). La tabla completa, con la evidencia, está en la [versión en inglés](../../../architecture/audits/351-audit.md).

## Escenarios

| Escenario | Estado |
|---|---|
| A. Partida individual: el combate original | Hecho |
| B. Los dos jugadores presentes: un solo combate compartido | Hecho |
| C. El compañero está en otro mapa, desconectado u ocupado: combate individual, sin esperas | Hecho |

## Criterios de aceptación

Los diez criterios estaban Hechos antes de esta auditoría: detección del jugador elegible, modo individual intacto,
una instancia compartida, sin combates duplicados, actualizaciones para todos, los compañeros de ambos luchan, el
resultado llega a ambos, experiencia y recompensas sin duplicar, una desconexión no bloquea a nadie, cambios de mapa
durante un encuentro.

## Encontrado y corregido

| Punto | Antes | Después | Prueba |
|---|---|---|---|
| Una batalla colocaba al compañero de un invitado donde lo ponía el seguimiento propio del anfitrión, no donde lo veía el invitado, y saltaba al empezar (#411 §6) | Parcial | Hecho | `co-op partners: the host draws a guest's partner where the guest's game has it (#411)` |
| En un combate propio del invitado en el mapa del anfitrión, el espejo de NPC movía a sus enemigos a los sitios del anfitrión en cada cuadro | Falta | Hecho | `co-op NPCs: a battle of the guest's own keeps its NPCs where it put them` |
| El objeto lanzado por un invitado salía de su bolsa antes de que el anfitrión aceptara el lanzamiento, y se perdía si lo rechazaba | Parcial | Hecho | `a guest pays for its thrown item only once the host's frames show the throw` |

## Pendiente

- Un combate individual del invitado que empezó lejos del anfitrión, en un mapa al que luego llega el anfitrión, es
  solo del invitado: la copia del anfitrión de ese mapa tiene sus propios NPC y podría luchar contra el mismo salvaje.
  Un combate compartido por mapa es la tarea 12 de [combate co-op](../coop-combat.md) y depende de mapas separados
  (#356/#357).
- Los NPC del mapa reaccionan solo al jugador del anfitrión (#419): un salvaje nunca persigue a un invitado.
- Prueba de extremo a extremo con dos máquinas reales: no hecha; el enlace y el flujo del combate se cubren con
  pruebas en un solo proceso.

## Resumen

Antes: 10 Hechos, 0 Parciales, 0 Faltan (10 criterios), más tres huecos encontrados (dos Parciales, uno Falta).
Después: 13 Hechos.
