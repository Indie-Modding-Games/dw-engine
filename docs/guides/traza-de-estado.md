# Traza de estado y comparación con el original

Para encontrar diferencias de comportamiento con el juego original sin mirarlas a ojo, los dos juegan la misma
entrada desde el mismo sitio, cada uno escribe una traza de su estado tick a tick y un script las compara y dice
dónde se separan por primera vez.

- `dwe-game --trace <fichero>` escribe la traza del motor.
- La traza del original la escribe una herramienta de fuera del repositorio que lee la memoria del juego
  original mientras corre. Escribe el mismo formato, a 59,94 frames por segundo.
- `scripts/oracle_compare.py` compara las dos trazas.

Para leer el mismo estado mientras el juego corre, sin escribir la traza entera, está el
[control remoto](control-remoto.md).

## La traza del motor

Con `--trace <fichero>`, el juego escribe una línea JSON por tick de lógica (60 por segundo). La escribe después
del tick, con lo que el tick dejó. Sin la opción no se escribe nada. Se usa con un autotest, para que la entrada
sea siempre la misma:

```sh
scripts/dev.sh run --content <contenido> --start twna01 --seed 1 \
  --autotest ruta.yaml --trace traza.jsonl
```

La primera línea es la cabecera: `{"src":"dwe","meta":true,"hz":60}`. Cada línea siguiente es un tick:

| Clave | Qué es |
|---|---|
| `f`, `time` | El tick y sus segundos desde el arranque |
| `in` | Los botones pulsados ese tick, con los bits del mando del original: arriba `0x1000`, derecha `0x2000`, abajo `0x4000`, izquierda `0x8000`, menú `0x10`, cancelar `0x20`, confirmar `0x40`, andar `0x80` |
| `map`, `mode` | El mapa y si se está en el campo (`field`) o en un combate (`battle`) |
| `player`, `partner` | `{x, y, z, yaw}` del jugador y del compañero |
| `npcs` | Los NPC del mapa: `{x, y, z, yaw, id}`, con `id` el id de script del NPC |
| `battle` | Solo durante un combate: `phase` (`intro`, `fight`, `end`...), `frame`, `rand` (dónde van las tiradas: con `--original-rolls`, el estado del `rand()` del original) y `fighters` |

Cada combatiente de `fighters` es `{x, z, yaw, hp, mp, max_hp, max_mp, off, def, spd, brain, pose, pending, move}`.
`pending` es el daño recibido que aún no se ha restado de `hp` (el `hpDamageBuffer` del original: un golpe baja
`hp - pending` de una vez) y `move`, el número del original de la técnica que está usando, o −1. El compañero va
primero y luego los enemigos, en su orden.

Las posiciones y los giros están en los ejes y unidades del original, las mismas que guarda su memoria:

- x, -y, -z del mundo del motor;
- el giro va de 0 a 4095 por vuelta, con 0 mirando hacia -z del original.

Así las dos trazas se comparan sin convertir nada.

## Empezar en el mismo sitio

Una partida nueva del motor no empieza donde está el original al cargar su estado. Para igualarlas, la ruta
coloca a los personajes con el comando de script `place`, en unidades del original:

```yaml
steps:
  - wait: {map: twna01, seconds: 1, timeout: 60}
  - script: place("player 256 -1285 1024")
  - script: place("partner 256 -1085 0")
  - script: place("npc:6 243 2565 2109")
  - wait: {ticks: 30}
  - hold: {keys: [right], ticks: 60}
  - hold: {keys: [], ticks: 30}
```

`place` deja al personaje exactamente donde se pide: no aparta a nadie ni pide la regla de suelta, para que la
distancia entre el jugador y el compañero sea la del original desde el primer fotograma.
Al compañero se le puede dar además la animación en la que la traza lo pilla: `place("partner 256 -1093 0 5 8 136
5272")` es el id de animación del original, el fotograma cuyo paso viene a continuación y la fracción de unidad que
lleva su posición en x y z (1/32768, `anim.c: locX`, `locZ`). En el oráculo, `place_clip`, `place_after_settle` y
`place_lead_ticks` del escenario lo generan justo antes de las entradas.

`hold` mantiene las teclas exactamente esos ticks de lógica, aunque los frames vayan lentos. Así una entrada de
«derecha 60 frames» dura lo mismo en los dos juegos. `battle("story:2")` empieza un combate de la historia contra
el NPC 2 del mapa, como los de los guiones: no se puede huir y los enemigos no gastan MP.

## Comparar

```sh
scripts/oracle_compare.py original.jsonl motor.jsonl --align input --json informe.json
```

`--align` elige el instante 0 de cada traza:

- `input`: la primera entrada;
- `move`: el primer movimiento del jugador;
- `battle`: la entrada en combate;
- `fight`: el principio de la lucha, tras la presentación.

Desde ahí se emparejan los registros por tiempo. Con `fight`, si la traza del original guarda el frame
del combate (`battle.frame`, el `BATTLE_FRAME_COUNT` del original), se emparejan por frame: el motor cuenta
los frames ya jugados y el original el que juega, así que el `n` del motor es el `n - 1` del original (se
toma la última muestra de cada frame del original). Un frame del original dura 3 vblanks o más, y por
tiempo los dos se van separando. Para cada campo dice:

- la primera vez que la diferencia pasa de su tolerancia, con los dos valores;
- la diferencia máxima;
- la parte de los frames que quedan fuera.

Los campos son estos:

- `mode`, `map`;
- `player.pos` y `player.yaw`;
- `partner.pos` y `partner.yaw`;
- `partner.gap`: la distancia del compañero al jugador;
- `npc<id>.pos`;
- `fighter<n>.pos`, `.hp` y `.mp`.

Además resume los arranques y paradas, el recorrido y la velocidad media, y los cambios de HP y MP de cada
combatiente.

Otras opciones:

- `--tolerance campo=unidades` cambia una tolerancia;
- `--max-seconds S` limita el tramo comparado;
- `--timeline S` imprime una tabla de los dos estados cada S segundos.

Sale con 1 si algo diverge.

## Límites

- El original mueve a los personajes a 20 Hz, en pasos de unos 50 unidades. El motor los mueve a 60 Hz. Por eso
  las tolerancias de posición son de medio paso o más.
- La partida del original y la del motor no tienen las mismas banderas de historia: un guion que corre en una y
  no en la otra cambia la ruta. Los escenarios evitan esas celdas.
- Los números aleatorios no se comparan. El motor usa `--seed` y un combate repite su resultado con la misma
  semilla, pero no el del original. Para un combate, `--original-rolls <estado>` hace que el motor tire con el
  generador del original desde el estado que el original tenía al empezar la pelea (la traza del original lo guarda
  en `rand`, de `0x80009010`). Con esa opción el motor también gasta las tiradas de las chispas y partículas del
  original y sus efectos tiran del mismo generador, así que las decisiones siguen en el mismo orden hasta que la
  pelea se separa por otra causa (tiempos de vuelo, movimiento; `docs/modulos/game/battle.md`). `battle.rand` da el
  estado del generador del motor tras cada fotograma.
