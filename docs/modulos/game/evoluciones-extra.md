# Evoluciones extra: todas las especies como compañero

Mejora opcional, **encendida por defecto** (excepción a la regla de «el original por defecto», decidida el
2026-11-10): toda especie con modelo que el original nunca deja criar se alcanza evolucionando en una partida
normal, con sus clips de combate o con los que el motor le hace. Se elige en la [pantalla de reglas](../apps/game.md#contenido-oculto) de la partida nueva con la
fila «Todas las especies como compañero» (Sí por defecto); con No la partida usa las reglas del original sin cambiar
nada. La partida guardada recuerda el ruleset.

Este documento es el diseño: qué especies entran, de dónde salen, con qué requisitos y por qué. El formato de los
parches está en [rulesets](../../architecture/rulesets.md#parches-por-especie) y las reglas de la revisión en
[evolution](evolution.md).

## Alcance

El disco trae 178 especies con modelo: el jugador, las 61 formas de compañero, 3 que solo se crían con objetos
(`panj`, `ggdr`, `mtet`) y 113 más. De esas 113 entran **60**:

- Las 47 especies que solo existen como enemigo (números 67 a 113 de la tabla): 21 novatos, 23 campeones y 3 ultras.
- `vand` (66), que el disco solo trae como jefe, y `muge` (115), que trae como enemigo de nivel 5.
- Los 11 modelos de personaje (`anlg`, `jiji`, `tens`, `tono`, `jure`, `hagu`, `brik`, `tirs`, `brak`, `scud`,
  `puti`): traen solo entre 2 y 8 clips de campo, así que el motor les hace los demás al cargar el compañero
  ([clips de compañero](../engine/animation.md#clips-de-compañero)); ver [Modelos de personaje](#modelos-de-personaje).

Quedan fuera, con su razón:

- **Las 53 copias de enemigo** (`egob` y los números 128 a 179): son la misma especie, con el mismo nombre, que una
  forma de compañero, con un modelo más pequeño y menos clips. Esa especie ya se cría con su forma de compañero, así
  que la copia no añade nada y solo duplicaría celdas del árbol.
- `panj`, `ggdr` y `mtet` siguen como están: con objeto en el original y por evolución con el
  [contenido oculto](evolution.md#contenido-oculto).

Los jefes entran (`vand`, `muge`): sus modelos tienen los clips de combate.

## Rulesets

La opción son tres rulesets más, uno por cada combinación que ya ofrece la pantalla de reglas:

| Ruleset | Extiende | Para |
|---|---|---|
| `todas` | `original` | Todas las especies, sin contenido oculto |
| `todas_oculto` | `oculto` | Todas las especies y el contenido oculto |
| `todas_oculto_mega` | `oculto_mega` | Todas las especies, el contenido oculto y el paso Ultra → Mega |

Los tres leen la misma sección `all_species` (`rules/todas/all_species.yaml`): el manifiesto la toma de la carpeta de
`todas` con `sections_from`, así que los datos están una sola vez. La sección tiene el formato de `species_extra`
(los mismos parches por especie) y además una lista `wake_up` con el formato de `specials.wake_up` de
`evolution.yaml`, que se suma tras los especiales del original.

- Una especie que otra sección de la cadena ya parchea (`species_extra` o `mega_step`) conserva ese parche y su entrada
  de `all_species` no se lee: con el contenido oculto `vand` es la de `oculto` (con su puerta de noche), y con el paso
  Mega `muge` es el Mega de `oculto_mega`, que sale de `mgdr` y `ggdr` en vez de `tank` y `gard`.
- Los datos son de diseño nuestro y por id; las filas de crianza y de ganancias se **copian** de una forma del
  original (`copy`), no se escriben números del disco.

## Prioridad del original

Ninguna forma nueva le quita a nadie una evolución del original. Las formas de `all_species` son destinos **de
reserva** (`SpeciesPatch::yields`): la revisión natural (`OriginalSelection`) mira primero los destinos del original
con sus reglas de siempre y solo si ninguno pasa mira los de reserva, con las mismas reglas (puntos, empate, media de
stats). Después de los dos viene la reserva del original (`nume` a las 96 horas de novato).

- Desempate: si un destino del original pasa, gana aunque uno de reserva tenga más puntos o mejor media.
- Con la opción apagada no hay destinos de reserva y la selección es la del original paso por paso.
- Los especiales al despertar de `all_species` van **después** de los del original en la lista: el del original en
  `mayo02` sigue igual (ningún especial nuevo usa ese mapa).

## Novatos

En el original los entrenamientos casi nunca fallan: `koro`, `tuno` y `toko` tienen dos destinos que entre los dos
cubren los seis stats y uno de ellos da el bonus gratis (`techs: 0`), así que siempre pasa uno del original. Solo
`tane` falla, cuando su stat mayor es el ataque y su peso está fuera de 10 a 20. Por eso los novatos nuevos salen de
dos maneras:

- **Revisión natural desde `tane`** (8 novatos de planta, agua e insecto): ataque como stat mayor, como pide el hueco
  que deja el original, y cada uno con su banda de peso (5, 25, 35 o 45 ± 5) y su cuidado (0 errores o 2 o más). Dos
  formas comparten banda y se separan por los errores, como `agum` y `gabu` se separan por los stats.
- **Especial al despertar** (13 novatos de fuego, hielo, ogro, juguete y oscuridad): como el especial del original en
  `mayo02`, un entrenamiento (`koro`, `tuno`, `toko` o `tane`) que se despierta en el mapa de la especie evoluciona a
  ella con una tirada de 50 sobre 100. Cada especie tiene su mapa, de su zona: el túnel para el lagarto de fuego, la
  Ciudad de los Juguetes para los juguetes, Freezeland y el Santuario de Hielo para los de hielo, la Fortaleza Ogro
  para los ogros, la Mansión para el lagarto oscuro, el Gran Cañón para el de roca.

## Campeones y ultras

Los requisitos siguen el estilo del original (3 de 4 grupos: stats, errores de cuidado, peso ± 5 y un bonus de
batallas, técnicas, disciplina o felicidad). Las formas nuevas son más pesadas o más descuidadas que las del original
de su línea, con barras de stats algo más bajas (60 a 90 en campeón): como solo cuentan cuando ninguna del original
pasa, son el premio a una crianza que el original manda a `nume`. Cada campeón nuevo sale de al menos un novato nuevo
y, donde el tema lo pide, también de uno del original (`elec` a `sima`, `palm` a `redv`, `agum` a `varm`, `gabu` a
`grur`, `piyo` a `akat`, `penm` a `tuti` y `gole`, `beta` a `geco`, `pata` y `elec` a `pidd`). Las ultras nuevas salen
de campeones nuevos y del original (`nume` y `ogre` a `waru`, `dori` a `tkka`, `sead` a `wrse`, `devi` y `bake` a
`vand`).

Ningún novato ni campeón nuevo queda sin salida: cada uno tiene destinos (del original y nuevos) y la prueba de
alcance comprueba que cada camino nuevo lo elige la revisión con valores legales.

## Equilibrio

- **Ganancias y crianza.** Cada forma copia las filas de una forma del original de su nivel y tema (la columna de
  diseño `like`): ganancias de stats (`stats_gains: {copy: ...}`) y crianza (`raise: {copy: ...}`: hambre, sueño,
  caca, peso por defecto, energía y entrenamiento), así que entrenar y cuidar a una forma nueva cuesta lo mismo que a
  su hermana. Nunca se copian las ganancias de un destino que escala (`devi`, `nume`, `scum`, `nani`, que solo tienen
  cerebro): `pcsc` y `gere` copian las de `vegi`, `icdv` las de `garu`; `vand` usa las ganancias de diseño del
  contenido oculto.
- **Comida favorita.** Una de la tabla de objetos, de su tema (carne para los de fuego, setas de hielo para los de
  hielo, pescado para los de agua...).
- **Técnicas.** Cada especie pelea con su propia lista de ataques de enemigo. El remate es el suyo cuando lo tiene; si
  no, se le pone en una ranura vacía uno de su línea cuyos puntos de lanzamiento sirven para su modelo
  (`effect_type` toma los de una forma hermana cuando todos sus ataques siguen funcionando con ellos). Un novato que
  toma el remate de un campeón lo usa con potencia 95 (`partner_moves`). Seis formas no tienen ningún remate que
  funcione con su modelo (`sngb`, `dkrz`, `gobr`, `inse`, `yanm` y `gard`) y pelean sin él. `muge` usa el suyo de jefe
  con los valores de compañero del paso Mega (potencia 240), lucha con el tipo 3 y va a 0,6 de tamaño en el campo.
- **Técnica al evolucionar.** Una forma de reserva cuya primera especialidad no enseña ninguno de sus ataques (`clea`,
  `sabd`) aprende y equipa el primero de su lista, como la orden de evolucionar del desarrollador.
- **Animaciones.** Los modelos de enemigo no tienen los clips de cuidado ni de gimnasio: el campo pone en su lugar uno
  que sí tienen (`MissingClips::StandIn`), como ya hace con cualquier compañero. Los modelos de personaje reciben antes
  todos sus clips de compañero de su receta (ver abajo), así que no les queda ninguno por sustituir.

## Modelos de personaje

Los 11 modelos de personaje entran con su etapa (`level`, que la tabla deja en 0 o en otra etapa), su fila de técnicas
copiada de la especie de la que salen sus clips (`moves: {copy}`, así cada técnica toca el clip de su ranura y lanza
desde los huesos que esa especie usa, `effect_type`) y un tamaño para los gigantes (`field_scale`: `tirs` 0,7, `scud`
0,5, `tono` 0,45, `jure` 0,38, `brak` 0,32: unos 450 de alto, como los compañeros Ultra grandes; en combate luchan con su radio y su altura a ese tamaño). Sus clips de compañero salen de una receta de
`animations/partner_clips.yaml`:

| Especie | Clips | Donante | Clips flojos |
|---|---|---|---|
| `tirs` | reasignados, misma jerarquía | `tyra` | — |
| `scud` | reasignados, misma jerarquía | `scum` | — |
| `puti` | reasignados, misma jerarquía | `tuno` | sin clip de renacer (28) |
| `anlg` | reasignados con mapa de huesos | `andr` | — |
| `jiji` | reasignados con mapa de huesos | `mtma` | la barba y el bastón siguen a su padre; el bastón se levanta con el brazo |
| `tens` | reasignados con mapa de huesos | `mame` | — |
| `brak` | reasignados con mapa de huesos | `mono` | el cuello largo se mueve rígido con su base |
| `tono` | procedurales, tiempos del donante | `etem` | todos son movimientos sencillos del cuerpo; sin clip de renacer (28) |
| `jure` | procedurales, tiempos del donante | `vegi` | todos son movimientos sencillos del cuerpo |
| `hagu` | procedurales, tiempos del donante | `elec` | movimientos sencillos; el cuerpo no se inclina (`upright`), se desplaza; sin clip de renacer (28) |
| `brik` | procedurales, tiempos del donante | `ogre` | todos son movimientos sencillos del cuerpo; sin clip de renacer (28) |

El clip de renacer (28) no existe en `tuno`, `etem`, `elec` ni `ogre`, así que sus cuatro especies se quedan sin él,
igual que esos donantes. `puti` es pequeño y flota a la altura del pecho del jugador: justo tras la evolución queda
detrás de él, como cualquier compañero pequeño, y se ve en cuanto se mueven. Los personajes de las escenas no cambian:
solo el modelo del compañero recibe los clips nuevos.

## Árbol de evolución

Cada forma nueva tiene su celda en el árbol (`chart` con `y: 0`: fuera de la rejilla, toma el primer hueco libre de la
caja de su nivel, en páginas si no cabe) con el icono de su forma hermana y la paleta 2. Una forma que solo sale por un
especial al despertar (no hay camino que lleve a ella) también tiene celda. El detalle de una celda dibuja como el
original hasta 5 orígenes y 6 destinos; los caminos de más existen pero no se dibujan.

## Tabla

| Especie | Etapa | Desde | Requisito clave | Siguiente |
|---|---|---|---|---|
| `flar` | Novato | `koro`, `tuno`, `toko`, `tane` | despierta en `tunn01` (50 %) | `mera`, `bird`, `varm`, `blmr` |
| `toya` | Novato | `koro`, `tuno`, `toko`, `tane` | despierta en `omoc01` (50 %) | `mono`, `tank`, `gard`, `brik` |
| `clea` | Novato | `koro`, `tuno`, `toko`, `tane` | despierta en `omoc02` (50 %) | `tyra`, `gard`, `varm`, `brik` |
| `ykag` | Novato | `koro`, `tuno`, `toko`, `tane` | despierta en `frzl01` (50 %) | `yuki`, `blmr`, `gole` |
| `icem` | Novato | `koro`, `tuno`, `toko`, `tane` | despierta en `icsa01` (50 %) | `yuki`, `hyog`, `gole` |
| `sngb` | Novato | `koro`, `tuno`, `toko`, `tane` | despierta en `frzl05` (50 %) | `ogre`, `hyog`, `tuti` |
| `dkrz` | Novato | `koro`, `tuno`, `toko`, `tane` | despierta en `dgha01` (50 %) | `bird`, `sabd`, `icdv` |
| `sham` | Novato | `koro`, `tuno`, `toko`, `tane` | despierta en `ogre00` (50 %) | `ogre`, `fuga`, `hyog` |
| `gobr` | Novato | `koro`, `tuno`, `toko`, `tane` | despierta en `ogre01` (50 %) | `ogre`, `fuga`, `pcsc` |
| `psyc` | Novato | `koro`, `tuno`, `toko`, `tane` | despierta en `gomi01` (50 %) | `garu`, `grur`, `sima` |
| `gotu` | Novato | `koro`, `tuno`, `toko`, `tane` | despierta en `gcan01` (50 %) | `dori`, `nise`, `gole`, `tank` |
| `inse` | Novato | `koro`, `tuno`, `toko`, `tane` | despierta en `miha01` (50 %) | `mono`, `varm`, `gole` |
| `tuka` | Novato | `koro`, `tuno`, `toko`, `tane` | despierta en `saib01` (50 %) | `bake`, `soul`, `icdv`, `pidd` |
| `modo` | Novato | `tane` | ataque el mayor; 0 errores; peso 25 ± 5 | `sead`, `sira`, `geco` |
| `otam` | Novato | `tane` | ataque el mayor; 0 errores; peso 5 ± 5 | `geco`, `hoee`, `mris` |
| `arur` | Novato | `tane` | ataque el mayor; 2+ errores; peso 25 ± 5 | `vegi`, `redv`, `zass` |
| `doku` | Novato | `tane` | ataque el mayor; 2+ errores; peso 5 ± 5 | `kuwa`, `redv`, `gere` |
| `tent` | Novato | `tane` | ataque el mayor; 0 errores; peso 35 ± 5 | `kabu`, `kuwa`, `akat` |
| `yanm` | Novato | `tane` | ataque el mayor; 2+ errores; peso 35 ± 5 | `kuwa`, `akat`, `sabd` |
| `sndy` | Novato | `tane` | ataque el mayor; 2+ errores; peso 45 ± 5 | `akat`, `sabd`, `coca` |
| `mcho` | Novato | `tane` | ataque el mayor; 0 errores; peso 45 ± 5 | `coca`, `akat`, `bird` |
| `hyog` | Campeón | `icem`, `sngb`, `sham` | ataque 80, defensa 70; 6+ errores; peso 55 ± 5; batallas 15 | `skul`, `waru` |
| `pcsc` | Campeón | `gobr` | cerebro 80; 8+ errores; peso 50 ± 5; felicidad 60 | `etem`, `digi`, `scud` |
| `sima` | Campeón | `psyc`, `elec` | velocidad 80, cerebro 70; 0–3 errores; peso 50 ± 5; disciplina 70 | `giro`, `houo`, `tens` |
| `tank` | Campeón | `toya`, `gotu` | HP 800, defensa 80; 0–5 errores; peso 60 ± 5; batallas 15 | `andr`, `tkka`, `muge`, `brak`, `anlg` |
| `redv` | Campeón | `arur`, `doku`, `palm` | MP 700, ataque 70; 6+ errores; peso 50 ± 5; técnicas 21 | `picc`, `digi`, `jure` |
| `jmoj` | Campeón | `sngb` | HP 800, ataque 70; 0–5 errores; peso 55 ± 5; batallas 10 | `mame`, `skul` |
| `nise` | Campeón | `gotu` | ataque 80, velocidad 60; 4+ errores; peso 60 ± 5; felicidad 50 | `mtgr`, `tkka`, `tirs`, `anlg` |
| `tuti` | Campeón | `sngb`, `penm` | HP 700, cerebro 70; 4+ errores; peso 55 ± 5; felicidad 40 | `mame`, `mtma` |
| `pidd` | Campeón | `tuka`, `elec`, `pata` | MP 800, velocidad 70; 0–1 errores; peso 50 ± 5; disciplina 80 | `houo`, `andr` |
| `gere` | Campeón | `doku` | HP 700; 8+ errores; peso 60 ± 5; batallas 20 | `etem`, `monz`, `scud` |
| `varm` | Campeón | `flar`, `clea`, `inse`, `agum` | HP 700, ataque 80; 0–3 errores; peso 60 ± 5; batallas 15 | `mtgr`, `mtma`, `tirs` |
| `fuga` | Campeón | `sham`, `gobr` | MP 600, ataque 80; 6+ errores; peso 50 ± 5; batallas 20 | `andr`, `giro`, `waru` |
| `mris` | Campeón | `otam` | MP 700, defensa 80; 5+ errores; peso 60 ± 5; técnicas 21 | `hkab`, `mgsd` |
| `gard` | Campeón | `toya`, `clea` | defensa 90, cerebro 70; 0–3 errores; peso 60 ± 5; disciplina 70 | `andr`, `tkka`, `muge`, `anlg` |
| `akat` | Campeón | `tent`, `yanm`, `sndy`, `mcho`, `piyo` | ataque 60, velocidad 80; 4+ errores; peso 50 ± 5; técnicas 21 | `houo`, `picc` |
| `zass` | Campeón | `arur` | MP 700, defensa 60; 6+ errores; peso 55 ± 5; felicidad 30 | `picc`, `hkab`, `jure` |
| `icdv` | Campeón | `dkrz`, `tuka` | MP 700, ataque 80; 6+ errores; peso 55 ± 5; batallas 20 | `skul`, `mgdr`, `vand` |
| `blmr` | Campeón | `flar`, `ykag` | MP 800, cerebro 70; 0–3 errores; peso 50 ± 5; técnicas 25 | `andr`, `mgsd` |
| `grur` | Campeón | `psyc`, `gabu` | ataque 80, velocidad 70; 6+ errores; peso 60 ± 5; batallas 20 | `skul`, `mgsd`, `waru` |
| `sabd` | Campeón | `dkrz`, `yanm`, `sndy` | ataque 70, velocidad 80; 6+ errores; peso 55 ± 5; técnicas 21 | `houo`, `mgdr`, `vand` |
| `soul` | Campeón | `tuka` | MP 800, cerebro 60; 5+ errores; peso 50 ± 5; técnicas 21 | `skul`, `vand`, `waru` |
| `gole` | Campeón | `ykag`, `icem`, `gotu`, `inse`, `penm` | HP 700, defensa 80; 0–3 errores; peso 55 ± 5; batallas 10 | `mtma`, `mame`, `tkka`, `brak` |
| `geco` | Campeón | `modo`, `otam`, `beta` | HP 700, MP 700; 0–5 errores; peso 50 ± 5; felicidad 50 | `mgsd`, `wrse`, `tono` |
| `waru` | Ultra | `hyog`, `fuga`, `grur`, `soul`, `nume`, `ogre` | HP 3000, ataque 300, defensa 250; 10+ errores; peso 75 ± 5; batallas 50 | — |
| `tkka` | Ultra | `tank`, `gard`, `nise`, `gole`, `dori` | defensa 300, velocidad 250, cerebro 300; 0–5 errores; peso 45 ± 5; técnicas 35 | — |
| `wrse` | Ultra | `geco`, `sead`, `mris` | MP 3000, defensa 250, cerebro 250; 8+ errores; peso 75 ± 5; batallas 40 | — |
| `vand` | Ultra | `icdv`, `sabd`, `soul`, `devi`, `bake` | MP 5000, velocidad 400, cerebro 500; 0–5 errores; peso 30 ± 5; disciplina 80 | — |
| `muge` | Ultra | `tank`, `gard` | HP 5000, MP 4000, ataque 500, defensa 500, velocidad 300, cerebro 300; 0–5 errores; peso 80 ± 5; batallas 100 | — |
| `puti` | Entrenamiento | `bota`, `puni`, `poyo`, `yura` | despierta en `tunn02` (50 %) | `agum`, `piyo` |
| `hagu` | Novato | `koro`, `tuno`, `toko`, `tane` | despierta en `fact01` (50 %) | `tank`, `gard`, `brik` |
| `brik` | Campeón | `toya`, `clea`, `hagu` | defensa 80, cerebro 60; 0–3 errores; peso 40 ± 5; técnicas 21 | `andr`, `giro`, `tkka` |
| `scud` | Ultra | `scum`, `pcsc`, `gere` | HP 3000, MP 2500; 12+ errores; peso 70 ± 5; felicidad 30 | — |
| `tirs` | Ultra | `tyra`, `varm`, `nise` | HP 4000, ataque 350, defensa 250; 0–5 errores; peso 70 ± 5; batallas 50 | — |
| `brak` | Ultra | `mono`, `tank`, `gole` | HP 5000, defensa 350; 0–8 errores; peso 85 ± 5; batallas 40 | — |
| `jure` | Ultra | `vegi`, `redv`, `zass` | MP 3500, defensa 250, cerebro 300; 10+ errores; peso 70 ± 5; técnicas 35 | — |
| `tono` | Ultra | `geco`, `sira`, `hoee` | HP 3500, MP 3000, defensa 250; 8+ errores; peso 80 ± 5; felicidad 50 | — |
| `jiji` | Ultra | `unim`, `ange`, `yuki` | MP 4000, cerebro 400; 0–3 errores; peso 30 ± 5; disciplina 90 | — |
| `tens` | Ultra | `yuki`, `nani`, `sima` | MP 3000, velocidad 250, cerebro 300; 0–5 errores; peso 40 ± 5; felicidad 70 | — |
| `anlg` | Ultra | `gard`, `tank`, `nise` | HP 3000, defensa 350, cerebro 300; 0–5 errores; peso 60 ± 5; técnicas 40 | — |

## Pruebas

- `packages/game/evolution/tests/all_species_tests.cpp`: el orden de reserva (un destino del original gana aunque uno
  de reserva puntúe más; sin destino del original que pase, gana el de reserva; sin destinos de reserva la selección
  es la del original) y el aprendizaje de la técnica de reserva.
- `the user's disc, extracted: ...` (`extracted_tests.cpp`, con `DWE_CONTENT`): cada especie nueva se alcanza desde un
  huevo con valores legales (cada camino lo elige la revisión, cada especial al despertar evoluciona en su mapa),
  ningún novato o campeón nuevo queda sin salida, las tablas de `original` no cambian con la opción, y una cadena de
  evoluciones por formas nuevas de huevo a ultra.
- `packages/engine/ruleset/tests/species_extra_tests.cpp`: `sections_from`, la lectura de `all_species` (una especie
  ya parcheada se salta), `stats_gains: {copy}` y los caminos que crecen más allá de las ranuras del disco; los
  rulesets del pack siguen sus esquemas (`the game pack's optional rulesets follow their schemas and read`).
- `the user's disc, extracted: the partner clip book gives each species every clip`
  (`packages/engine/animation/tests/clip_synthesis_tests.cpp`, con `DWE_CONTENT`): cada modelo de personaje recibe
  todos los clips del compañero y del combate, con la duración y el movimiento raíz del clip de su donante (los
  fotogramas del golpe y de los efectos caen donde caen en el donante).
- `a copied move row replaces the species' whole row, and a level patch sets its level` (`species_extra_tests.cpp`).
- `the user's disc, extracted: each character model fights with its partner clips`
  (`apps/game/tests/partner_clip_battle_tests.cpp`, con `DWE_CONTENT`): cada uno lucha hasta el final, sus clips de
  combate avanzan paso a paso como los del donante y cada técnica se suelta en el mismo fotograma.
- `an upright body moves forward where it would lean` (`clip_synthesis_tests.cpp`) y `a species drawn at another size
  fights with its body at that size` (`packages/session/tests/battle_adapter_tests.cpp`).
- `a form only a ruleset's wake-up special leads to gets its cell` (`apps/game/tests/player_chart_tests.cpp`).
- Ruta `route-all-species.yaml` ([prueba vertical](../../guides/prueba-vertical.md)): la opción encendida por defecto,
  el árbol con 121 casillas y `tane` → `otam` → `geco` en el juego. Las rutas del contenido oculto, del paso Mega y de
  las etapas canónicas la apagan y siguen con sus 61, 65 y 66 casillas.
