# Entrenar en un mundo compartido (documento de diseño)

Estado: **hecho** (2026-11-20, ticket #365). Se apoya en el [cooperativo en línea](multiplayer.md) (§5.7 mundo
compartido, §6 decisiones 4 y 5), el [ADR 0024](../adr/0024-co-op-with-authoritative-host.md) y las pantallas del
gimnasio del módulo [partner](../modules/game/partner.md).

**Regla cero:** el juego de un jugador no cambia. A solas (o en versus), una sesión de entrenamiento adelanta el reloj
una hora exactamente como el original, y ningún camino de código difiere del de antes. Todo lo de abajo aplica solo
mientras corre una partida cooperativa.

## 1. Cómo entrena el original

`TRN_advanceTrainingTime` (`trn_reward.c`) corre una vez por sesión, después de la animación y la recompensa del curso:

- **El compañero paga:** cansancio + el coste del curso, energía − coste, felicidad − coste. Con cansancio de 70 o más
  (sobreentrenamiento) además felicidad − 3 y disciplina − 3.
- **El reloj salta una hora:** se pone en la hora de inicio de la sesión + 1200 frames (20 frames por minuto de juego).
  Mientras corre el curso el reloj está quieto, así que una sesión es siempre exactamente una hora de juego.
- **Los cuidados de esa hora se pagan de una vez:** la comprobación de vida infeliz cuando la nueva hora es múltiplo de
  4; el sueño (contador de enfermedad + 1, horas de sueño perdidas + 1); el hambre (temporizador de inanición − 120 y un
  error de cuidado si la energía es muy baja, si no nivel de comida − 60); energía − el consumo de la especie; el baño
  (temporizador − 1200, si no nivel − 6); vida − 1, temporizador de evolución + 1, temporizadores de heridas y
  enfermedad.
- **Una sesión que cruza la medianoche** suma un día: edad + 1, el contador de días y el disparador diario. Los cambios
  de momento del día (16:00, 20:00, 6:00) salen del reloj.

Un minuto de juego es un segundo real (20 frames por segundo), así que una hora de juego es un minuto real. Un jugador
que entrena diez sesiones seguidas adelanta el mundo diez horas en unos pocos minutos reales; es el ritmo del original,
y a solas está bien.

## 2. Qué hacía el cooperativo antes de este ticket

Medido en el código (`GameSession::advanceGameTime`, `Partner::passTrainingHour`, el puente cooperativo):

| Qué | Antes | Por qué se rompe |
|---|---|---|
| El reloj del mundo | Cada partida de una sala corría **su propio** `GameClock`, copiado del guardado del anfitrión al unirse. Nada lo enviaba después (el reloj de la instantánea de §5.5 nunca se hizo). | Dos jugadores ven distinta hora, tiendas y horarios de NPC a los pocos minutos. |
| El anfitrión entrena | El curso retiene el campo (`holdForScript`), lo que para su reloj; luego la sesión lo salta una hora. | El compañero del anfitrión y los invitados viven horas distintas; diez sesiones del anfitrión saltan diez horas del mundo compartido pero no las de los invitados. |
| Un invitado entrena | Solo salta su propio reloj. | El reloj del invitado se aleja del anfitrión y nada lo trae de vuelta. |
| Varios entrenan a la vez | Cada sesión salta su reloj. | Con cuatro jugadores el mundo saltaría cuatro horas por minuto si fuera compartido; con relojes propios simplemente se desfasa. |
| Los demás lo ven | Nada dice a los demás que un jugador entrena. | Un intercambio o una petición de aliado de combate llega a un jugador sentado en el gimnasio. |

## 3. Opciones

| | A. Sesiones en paralelo | B. Sesiones sincronizadas | C. Hora propia del compañero |
|---|---|---|---|
| Idea | Cada jugador entrena cuando quiere, solo, mientras el mundo sigue. | Las sesiones empiezan juntas; todos esperan un "listo" y el reloj salta una hora para todos. | La hora del compañero es suya: paga los costes de la hora y el reloj del mundo no se mueve. |
| Esperas | Ninguna. | Todos esperan al más lento; nadie puede entrenar solo. | Ninguna. |
| Reloj | Hay que decidir quién lo mueve. | Un salto por ronda, acordado. | Ninguna sesión lo salta. |
| Fidelidad | Igual por sesión si va con C. | Misma hora por sesión. | Mismos costes por sesión; el día lo pone el mundo. |
| Coste | Bajo. | Un estado de sala, "listos", tiempos límite y reglas de cancelación. | Bajo: un indicador en la petición de la sesión. |

**Elegida: A + C sobre un único reloj del mundo, el del anfitrión.** Los jugadores entrenan en paralelo sin esperar a
nadie (A); una sesión le cuesta a su compañero exactamente la hora que cobra el original, pero en cooperativo nunca
mueve el reloj del mundo (C). B se descartó: convierte el entrenamiento en una actividad de grupo, justo lo que el
ticket no quiere ("sin esperarlos"), y además salta el mundo para quienes no entrenan.

## 4. El modelo

### 4.1 Un reloj, el del anfitrión

- El reloj del anfitrión es el del mundo. Corre con el tiempo real (un minuto de juego por segundo real) y solo lo
  mueve lo que ya lo movía a solas: su propio sueño, los saltos de tiempo de sus scripts, los comandos de depuración.
  Una sesión de entrenamiento ya no lo mueve.
- El anfitrión dice su reloj a cada invitado con `WorldClock` (`0xC0`, anfitrión → invitados, fiable): frames
  transcurridos, si corre y su velocidad en milésimas. Lo dice enseguida al invitado que se une, luego más o menos cada
  segundo, y enseguida cuando el reloj se para, arranca, cambia de velocidad o salta más de un segundo
  (`session::WorldClockTeller`).
- La sesión de un invitado (`ClockShare::Guest`) lo sigue (`session::WorldClockFollower`): entre dos avisos hace correr
  el mundo a la velocidad del anfitrión durante dos segundos como mucho, y luego espera el siguiente aviso. En cada tick
  compara su reloj con el del mundo:
  - **una diferencia pequeña** (menos de una hora) se juega como frames, así que el compañero la vive como siempre;
  - **horas enteras** (el anfitrión durmió o saltó tiempo) son un salto: el compañero las paga con `skipHours`, como un
    salto de tiempo de script, y el reloj se pone en el del mundo;
  - **un invitado un poco adelantado** (menos de cinco minutos) espera al mundo;
  - **un invitado muy adelantado** (su propio sueño o salto) vuelve a la hora del mundo. Su compañero ya vivió esas
    horas, así que no las vive otra vez: la sesión recuerda hasta dónde vivió y no le da frames hasta que el mundo pase
    ese punto.
- Antes del primer aviso del anfitrión, un invitado corre su propio reloj como antes.

### 4.2 La hora del compañero

`TrainingRequest::sharedClock` (lo pone la sesión en cooperativo, anfitrión o invitado) hace que
`Partner::passTrainingHour` pague todos los costes de §1 **sin** el salto del reloj y sin el cambio de día:

- las ganancias, los costes, la penalización por sobreentrenamiento, el hambre, sueño, baño, vida y temporizador de
  evolución de la hora: sin cambios, así que diez sesiones dan el mismo compañero que diez sesiones a solas;
- la comprobación de vida infeliz y el sueño usan la hora a la que habría llegado la sesión (la hora del reloj + 1);
- la edad, el contador de días y el disparador diario salen solo de las medianoches del mundo, tal como el compañero
  las vive en el reloj compartido. Una sesión a las 23:30 no suma un día; la medianoche del mundo sí.

Mientras corre un curso en cooperativo el reloj del mundo sigue, pero los cuidados del compañero se retienen (ni hambre
ni energía), igual que el reloj del original se queda quieto para él. Al terminar el curso el compañero vuelve a vivir
el tiempo del mundo. La retención es `GameSession::holdForCourse`, que pone el puerto de campo del gimnasio junto a la
retención del campo.

### 4.3 Quién entrena

`TrainingStatus` (`0xC1`, en ambos sentidos, fiable) lleva un jugador y un id de curso (32 caracteres como mucho, vacío
cuando deja de entrenar). Un invitado dice al anfitrión su curso cuando cambia; el anfitrión toma el id de jugador del
enlace, nunca el del mensaje, y dice a cada invitado los cursos de los demás cuando cambian.

- La etiqueta cooperativa sobre un jugador muestra **Entrenando** (`coop_hud.tag.training`,
  `coop_hud.tag.<n>.training`, API de UI 0.25.0).
- Un jugador que entrena está ocupado para intercambios y no se ofrece como aliado de combate; a un jugador en el
  gimnasio no se le puede meter en un combate.
- El primer curso de un jugador en una partida cooperativa muestra un aviso una vez: el tiempo del mundo sigue
  corriendo y cada sesión cuenta su propia hora para el compañero (`online.coop.training_clock`).

### 4.4 Flujo

```mermaid
sequenceDiagram
  participant H as Sesión del anfitrión
  participant N as Enlace
  participant G as Sesión del invitado
  H->>N: WorldClock (al unirse, cada segundo, al parar/saltar)
  N->>G: WorldClock
  G->>G: seguir: frames / saltar horas / esperar / volver
  G->>N: TrainingStatus(curso)
  N->>H: TrainingStatus (jugador = el del enlace)
  H->>N: TrainingStatus a los demás invitados
  Note over H,G: cada sesión cobra una hora a su compañero; ningún reloj salta
```

### 4.5 Dormir en un mundo compartido (#424, #411)

Dormir salta la noche, y la noche es del mundo. Las reglas:

- **Solo el anfitrión mueve el reloj del mundo.** El sueño del anfitrión lleva el mundo a la mañana de su compañero;
  cada invitado salta esas horas (§4.1).
- **El sueño de un invitado no mueve el mundo, y el mundo no lo deshace.** El compañero duerme como a solas (el
  descanso de la noche, las horas de vida y de evolución, su siguiente hora de dormir), y luego la pantalla del
  invitado queda a oscuras hasta que el reloj del mundo llega a esa mañana (`SleepScreen` espera mientras
  `GameSession::aheadOfWorld`), con un aviso que lo dice (`online.coop.sleep_wait`). Si el anfitrión duerme entretanto
  el invitado despierta al momento. El reloj del invitado nunca vuelve a la noche y el compañero nunca vive esas horas
  dos veces.
- **Nadie está obligado a dormir.** Los demás siguen jugando; el mundo nunca se retiene por quien duerme.
- **Dormir a destiempo no da ventaja.** Un invitado no puede descansar y aun así usar la noche: su partida espera con
  su compañero. Solo puede dormir cuando su compañero tiene sueño, como a solas.
- **Los eventos del mundo y los cambios de día** siguen solo el reloj del mundo (§5): el sueño de un invitado no cambia
  el día de nadie.

## 5. Desconexiones y casos límite

- **Un invitado se va:** el anfitrión borra su curso (`CoopTraining::playerLeft`) y se lo dice a los demás; si vuelve,
  oye otra vez el reloj y todos los cursos.
- **El anfitrión se va:** el invitado vuelve al título como antes; nada de este ticket sobrevive a la partida.
- **Cambio de día y eventos:** solo la medianoche del mundo cambia el día, para todos a la vez (un invitado que la
  saltó la paga en `skipHours`). Los momentos del día siguen el reloj compartido.
- **Menús y scripts del anfitrión:** sus menús y la pausa de Start ya no retienen el reloj del mundo (#415); sus scripts
  sí (§7), así que el mundo se para para todos mientras el anfitrión lee un diálogo, y los invitados esperan con él.
- **Un invitado duerme:** su compañero duerme y su pantalla espera a oscuras la mañana del mundo (§4.5); ya no vuelve
  a la noche.
- **El anfitrión duerme:** los invitados saltan esas horas (§4.1), como un salto de tiempo de script.
- **Combates:** un combate sigue parando el reloj de su jugador mientras dura; un invitado se pone al día después con
  frames o un salto.

## 6. Código

| Pieza | Dónde |
|---|---|
| La hora del compañero sin salto | `packages/game/partner` (`TrainingRequest::sharedClock`, `Partner::passTrainingHour`) |
| Compartir, seguir y decir el reloj | `packages/session` (`shared_clock.hpp`, `GameSession::shareClock`, `hearWorldClock`, `worldTime`, `holdForCourse`) |
| Mensajes `0xC0`, `0xC1`, protocolo 11 | `packages/game/coop` (`protocol.hpp`, `link.cpp` `directionAllowed`) |
| Enrutado y bucle del juego | `apps/game` (`coop_gameplay_bridge`, `coop_training`, `game.cpp`, `play_session`) |
| La etiqueta | `content/packs/game/ui/coop_hud`, API de UI 0.25.0 |

## 7. Límites y pendientes

- **El sueño en cooperativo** (§4.5): un invitado que duerme mientras el anfitrión sigue despierto espera hasta una
  noche en tiempo real (unos diez minutos); al anfitrión no se le avisa de que un invitado duerme.
- **Los diálogos y scripts del anfitrión siguen reteniendo el reloj del mundo** para todos (sus menús y su pausa ya no,
  #415).
- **El progreso del invitado no se guarda de vuelta** en su propia partida (fase C4).
- **La animación de entrenamiento del compañero remoto** no se dibuja en las otras máquinas; solo lo dice la etiqueta.

## 8. Pruebas

- `packages/game/partner/tests/training_tests.cpp`: una sesión con reloj compartido no mueve el reloj y paga las mismas
  ganancias y costes que a solas; una a las 23:30 no suma edad.
- `packages/session/tests/shared_clock_tests.cpp`: seguir (diferencia, salto, esperar, volver), el aviso, a solas
  frente a cooperativo, cuatro jugadores entrenando cada segundo durante diez segundos, la retención del curso, el salto
  propio de un invitado sin vivirlo dos veces, el sueño del anfitrión.
- `packages/game/coop/tests/training_tests.cpp`: ida y vuelta, rechazos y las reglas de sentido con un `CoopLink` real.
- `apps/game/tests/coop_training_tests.cpp`: un anfitrión y tres invitados en enlaces loopback entrenando a la vez.
