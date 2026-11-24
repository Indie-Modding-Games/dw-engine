# randomizer

El aleatorizador (`dwe::game::randomizer`, capa `game`): una partida nueva de un modo que lo ofrece puede ser una
**partida aleatorizada**, en la que cambian las evoluciones, el compañero inicial, los enemigos salvajes, los objetos
del mapa y el botín, y las técnicas se aprenden con la probabilidad que elige el jugador. Depende de `core`, `assets` y
`ruleset`; no dibuja nada ni tiene estado aleatorio propio.

Es un añadido: el original no tiene nada parecido. Pertenece al Modo Folder (las
[variantes del juego](../../architecture/game-variants.md): todo lo que cambia la jugabilidad va ahí), como una capa
sobre el conjunto de reglas que eligió la partida, no como un modo ni una variante aparte. Classic Enhanced no lo
ofrece.

## Las opciones

| Opción | Qué cambia | Dónde |
| --- | --- | --- |
| Evoluciones | Cada evolución natural da una forma al azar de la etapa del objetivo | `Evolution::mapNaturalTargets`, `EvolutionAdapter::mapEvolutions` |
| Aprender técnicas | Original, 25, 50, 75 o 100 % para cada técnica que el compañero vio y puede aprender | `BattleSetup::learnChance`, `learnedMoves` en `aftermath.cpp` |
| Compañero inicial | El primer compañero es un Novato al azar | `GameSession::startNewGame` |
| Enemigos salvajes | Cada enemigo salvaje de la lista es una especie al azar de su etapa | `randomizeMapEnemies`, `FieldScene::attachMapNpcs` |
| Objetos del mapa | Los objetos del suelo y de los cofres | `FieldItemsAdapter::spawnItem`, `spawnChest` |
| Botín | Cada especie suelta un objeto al azar en vez del de su tabla | `BattleAdapter::randomizeDrops` |

Los preajustes son **Apagado** (todo apagado) y **Caos** (todo encendido, técnicas siempre aprendidas).

### Evoluciones

La tabla sigue decidiendo: la selección puntúa los objetivos originales con sus requisitos, igual que sin
aleatorizador, y la forma elegida se cambia luego por una de la misma etapa, con la clave de la forma que evoluciona y
el objetivo (`hash(seed, "evolution", from, target)`). Así **los requisitos salen del objetivo original** y **los
stats de la forma nueva** (`evolve` carga los stats de referencia de la forma cambiada). La tabla de evolución muestra
las formas cambiadas con esos requisitos. Las reservas por etapa son las formas a las que lleva alguna tabla,
agrupadas por nivel, sin los Mega y sin formas sin stats de referencia: un bebé sigue pasando a una forma de
entrenamiento, esta a un novato, un novato a un campeón y un campeón a un definitivo. La forma de reserva del novato
(Numemon) también es una evolución natural y se cambia igual.

No se cambian: las evoluciones especiales (elogio, regaño, dormir, la hora del temporizador del campeón), los objetos,
los scripts, la barra de virus, el evolve del desarrollador ni el paso de Definitivo a Mega.

### Aprender técnicas

`learnedMoves` tira `random.below(100)` por cada técnica que vio el compañero, como el original, y la compara con la
probabilidad fija en vez de la de la tabla de técnicas. Con 100 % pasan todas las tiradas; con Original la
probabilidad es la de la tabla. Un combate que no tenía candidata y ahora sí hace una tirada más para elegirla: es el
generador del propio combate, que la sesión siembra una vez por combate, así que nada cambia después del combate.

### Compañero inicial

El compañero que eligió la apertura pasa a ser un Novato al azar de la tabla de especies del compañero, con la
semilla como única clave: cualquier respuesta de la entrevista da el mismo. Empieza con los stats de partida nueva
para las especies que no tienen los suyos.

### Enemigos salvajes

Los mapas no marcan qué enemigo es salvaje, así que la lista es un dato: `rules/original/randomizer.yaml` nombra, por
mapa, las ranuras de NPC que son combates salvajes simples. Se hizo una vez con los scripts de mapa de la historia
base: una sección de conversación cuyas llamadas son solo `set_dialog_owner`, `show_textbox`, `fadeout_hud`, `delay`,
`start_battle` y `set_trigger`, que lanza `start_battle(0)` y no activa un disparador de reclutamiento (200 a 262).
Eso deja fuera jefes, combates de historia y predefinidos, reclutas, personajes y los tres raros errantes. Cada ranura
de la lista recibe una especie del nivel de la original, de la plantilla de compañeros con modelo, con la clave del
mapa y la ranura; conserva los stats, el tipo, los scripts y el nivel del registro y sus técnicas pasan a ser las de
la especie nueva (rotadas con la misma clave). El documento del mapa se cambia al cargarse, antes de que el campo lo
coloque, así que el campo, el combate, el minimapa y el cooperativo ven el mismo enemigo.

### Objetos

Los objetos del suelo usan la clave de mapa, celda y objeto original; los cofres, de mapa, disparador y objeto
original. La reserva son los objetos del original sin los objetos clave y sin los que la partida conserva o excluye.
**Los objetos conservados** nunca se mueven ni aparecen en otro sitio: los objetos clave y todo objeto que la historia
busca en la bolsa (la flauta, las cañas, la Leomonstone, las llaves de la mansión y del frigorífico, el engranaje, el
decodificador, la carne y la medicina que piden las misiones...). Excluidos: los objetos de evolución raros de la
opción de objetos raros y las tres entradas sin uso de la tabla del original. Los regalos de los NPC no se cambian: sus
líneas nombran el objeto. El guardaobjetos empieza vacío, así que no hay nada que colocar en él.

### Botín

Cada especie con objeto de botín suelta el mismo objeto al azar, con la especie como única clave; la probabilidad
sigue siendo la de la tabla y la tirada es la misma.

## Semillas y códigos de partida

Una semilla son 40 bits, escritos como 8 caracteres de base 32 de Crockford (`K7QH2M9X`). El **código de partida**
añade un guion y dos caracteres con las opciones (`K7QH2M9X-3Z`): los bits 0 a 4 son evoluciones, compañero inicial,
enemigos, objetos y botín; los bits 5 a 7, la probabilidad de aprender. `parseCode` acepta cualquiera de los dos,
ignora mayúsculas, espacios y guiones y lee I, L y O como 1, 1 y 0. Un código pegado o escrito en la configuración
trae también sus opciones, así que compartir una partida es compartir su código.

## Determinismo

Cada elección es `hash % tamaño de la reserva`, donde el hash es FNV-1a sobre un dominio y sus claves, empezando por
splitmix64 de la semilla y terminando con splitmix64, y las reservas son ids ordenados. Nunca toca los números
aleatorios del juego (`ruleset::Random`): con el aleatorizador apagado, o encendido, la sesión, el compañero y cada
combate sacan exactamente lo mismo que sin él, así que la comparación con el oráculo sigue cuadrando. La misma semilla
y opciones dan la misma partida en todas las plataformas; un test fija un valor del hash.

## Guardado

Las opciones se fijan al empezar la partida y se guardan en la sección `randomizer` (`seed`, `evolutions`,
`learn_chance`, `starter`, `enemies`, `items`, `drops`); un guardado sin ella no es una partida aleatorizada. El
resumen de la cabecera (formato 5) lleva el código de partida, vacío en cualquier otra partida, y la lista de ranuras
lo muestra junto al modo. Un guardado de formato 4 se carga con uno vacío.

## La pantalla de configuración

`ui/new_game_randomizer`, después del modo y sus reglas (`TitleScreen::finishNewGame`, `NewGameRandomizer`): los dos
preajustes, cada opción, la probabilidad de aprender (pasa por sus valores), la semilla (al azar por defecto;
selecciónala y escribe, Enter la guarda, Esc borra), **Pegar** (la semilla o el código del portapapeles), **Nueva
semilla**, el código de partida y Empezar. El juego publica `new_game_randomizer.*` y el documento envía
`new_game_randomizer` con una acción (`toggle:<option>`, `learn`, `preset:off`, `preset:chaos`, `edit`, `erase`,
`commit`, `paste`, `reseed`, `start`, `back`). Una ruta de prueba la pide con `new_game_randomizer: true` (junto con
`new_game_rules: true`); las demás no empiezan partidas aleatorizadas. La skin pixel dibuja los preajustes con su
cursor compartido de cuatro esquinas. El aspecto original usa la caja de las reglas, más ancha, más alta y con las
filas más juntas (`skins/original/new_game_randomizer`), para que sus once filas quepan en una ventana de 720dp.

## Datos del conjunto de reglas

`rules/<id>/randomizer.yaml` (esquema `ruleset_randomizer.schema.json`), una sección del conjunto de reglas original
que los demás extienden:

```yaml
enemies:            # id de mapa -> ranuras de NPC salvajes
  trop02: [1, 3]
kept_items: [mansion_key, gear]
excluded_items: ["game:steel_heart"]
excluded_species: []
```

Un mod que añade mapas lista sus ranuras salvajes; uno que añade especies puede excluirlas.

## Multijugador

Cooperativo: el invitado juega el guardado del anfitrión, que lleva la sección `randomizer`, así que cada par recibe
las opciones y la semilla del anfitrión y cambia cada mapa igual; de todos modos, los objetos del anfitrión mandan.
El versus no se aleatoriza.

## Pendiente

- No hay registro de spoilers (cada elección de una partida, escrito junto al guardado).
- Con mando no se puede escribir la semilla letra a letra: pégala o saca una nueva.
