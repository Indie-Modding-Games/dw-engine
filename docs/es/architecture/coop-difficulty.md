# Dificultad cooperativa según el número de jugadores (documento de diseño)

Estado: **hecho** (2026-11-20, ticket #361) para las estadísticas y el remate de los enemigos en combates normales y
fijos; §9 dice qué entró y en qué difiere de esta propuesta. Se apoya en el
[cooperativo en línea](multiplayer.md), el [ADR 0024](../adr/0024-co-op-with-authoritative-host.md), la primera versión
de los combates cooperativos (#351, PR #363) y el diseño de combate con ambos compañeros de
[combate cooperativo](coop-combat.md). Las sesiones de hasta cuatro jugadores son el ticket #352
([navegador de salas y cuatro jugadores](coop-browser-4p.md)).

**Base de diseño:** el rediseño del combate se define en el [Diseño del sistema de combate](combat-design.md), base de
toda la documentación de combate; su capítulo 8 marca el objetivo de escalado cooperativo del nuevo sistema.

**Regla cero:** el juego de un jugador no cambia. Con un solo compañero peleando y nadie ayudando, todos los factores de
este documento valen exactamente 100 % y ningún camino de código difiere del actual. El escalado cooperativo es una
mejora, documentada y opcional, que elige el anfitrión.

## 1. Cómo fija la dificultad el original

El original no tiene ajuste de dificultad ni escalado. Cada número de abajo sale del decomp (nombres de funciones) o de
los rulesets de dwe (`content/packs/game/rules/original/battle.yaml`), que lo portan.

### 1.1 Enemigos y encuentros

- **Sin encuentros aleatorios.** Cada pantalla tiene hasta 8 colocaciones fijas de enemigos en su `.MAP`; el script de la
  pantalla decide cuáles aparecen (`SET_DIGIMON`, `STORE_RANDOM` y el reloj). Tocar uno ejecuta su sección de script,
  que llama a `START_BATTLE` → `startBattle` → `handleBattleStart` → `BTL_battleMain`.
- **Las estadísticas del enemigo salen de la colocación, no de la especie.** HP, MP, Atq, Def, Vel, Int, técnicas, pesos
  de las técnicas (`movesPrio`) y la recompensa de bits son por colocación; solo el objeto que suelta y su probabilidad
  salen de la tabla de especies. dwe los lee igual (`enemyFromMap` en `packages/game/battle/src/combatant.cpp`).
- **La curva es regional, no adaptativa.** Nada escala con el nivel ni el progreso del compañero. Mediana del Atq enemigo
  por región: Bosque Nativo 150, Sabana Engranaje 340, Pueblo Factorial 530, Mt. Infinity 680, Back Dimension 700
  (HP 3400–6100).
- **Tamaño del grupo por tirada de unión.** El enemigo tocado siempre pelea; cada otro enemigo de la pantalla se une con
  una probabilidad de una tabla 3×3 según el tipo del compañero y del enemigo (30–100 %, `FLEE_CHANCES`, dwe
  `battle.yaml` `joining`). Enemy bell da −50 a la tirada, Enemy repel +20. Como máximo 3 enemigos, exactamente 1
  compañero.
- **Combates fijos (trigger 1).** Jefes de la historia: se une todo el que está en pantalla, Huir está desactivado y los
  enemigos no gastan MP. Los combates predefinidos (variables de script 0xFA–0xFD) son 45 de las 499 llamadas a
  `START_BATTLE`.
- **Sin muertes permanentes.** Los enemigos se curan del todo después de cada combate y vuelven al entrar de nuevo en la
  pantalla.

### 1.2 Presión del combate

- Tiempo real a 20 ticks por segundo: `BTL_tickPartnerAI`, `BTL_tickEnemyAI`, golpes, estados.
- **Los enemigos solo apuntan al compañero.** El domador nunca pelea. Un enemigo solo se vuelve contra otro enemigo que
  lo golpeó con una técnica de área.
- Daño: `m·(P + clamp(Off−Def, ±500)·P/500)/30` × 90–110 %, 1–9999 por golpe, donde `m` es el factor de tipo (0,2, 1 o
  2 según la especialidad del defensor). No hay golpes críticos. Los remates ignoran la Def; los remates enemigos hacen
  ×1–2 al azar.
- La elección de técnica del enemigo es un sorteo ponderado sobre `movesPrio` (+20 a las técnicas de área mientras
  vivan aliados). Los enemigos lanzan su remate por su cuenta cuando la barra (`3000 − Speed`) está llena.
- El lado del compañero lo moldean la Int (menú, elección de blanco, mejoras), la disciplina (desobediencia por debajo de
  301 de Int) y su condición (herido o enfermo: −20 % a Atq, Def, Vel e Int).

### 1.3 Recompensas y progresión

- **Bits:** la suma de los `bits` de la colocación de cada enemigo.
- **Objetos soltados:** por enemigo, `dropChance > rand(100)` da su objeto.
- **Subidas de estadísticas en lugar de experiencia** (`battleStatsGainsAndDrops`): para cada estadística, con E el
  valor del enemigo más fuerte, P el del compañero y F = 10/12/16 para 1/2/3 enemigos (`rewards.enemy_factors`):
  E ≥ P da ⌈E·F/(P·10)⌉; si no, +1 con probabilidad E·F·10/P %. Una estadística que subió 0 recibe otra oportunidad
  por golpes, bloqueos y HP perdido.
- **Aprendizaje en combate:** una técnica de entre las que usaron los enemigos, según especialidad y una tabla de
  probabilidades.
- **Contadores y cuidado:** una victoria da felicidad +2, cansancio +5 y `battles + 1` (los requisitos de evolución lo
  leen); huir, −10 de felicidad y −6 de disciplina; una derrota, −30 de felicidad, −20 de disciplina, una vida y el 30 %
  de las ranuras ocupadas de la bolsa. Cada combate cuesta 20 minutos de reloj y −15 de comida.

### 1.4 Torneos

Cuadro de 8, tres combates, 1 contra 1, sin Huir, temporizador de 99 s. Las estadísticas del rival salen de una base por
copa (D 100 … S 300) ±30 % y ×1/×2/×2,5 según el nivel. Sin bits, objetos soltados ni subidas de estadísticas; se
recupera el 20 % de HP y MP entre combates.

## 2. Qué cambia el cooperativo

| Sistema | Un jugador | Qué rompe el cooperativo | Dónde se resuelve |
|---|---|---|---|
| Daño infligido | Un compañero | De dos a cuatro compañeros golpean a los mismos enemigos: el tiempo para derrotarlos cae a cerca de 1/N | §5 escalado de HP |
| Presión enemiga | Todos los ataques enemigos sobre un compañero | Los ataques se reparten entre N compañeros; cada uno recibe cerca de 1/N | §5 Atq, MP y remate |
| Objetos | Una bolsa | Cada jugador lanza de su propia bolsa: N veces la curación | §5 factor de ayudante |
| Derrota | El compañero a 0 HP termina el combate | Los otros compañeros pueden reanimarlo o acabarlo | [combate cooperativo](coop-combat.md) §4.8 |
| Tamaño del grupo | Tirada de unión según el tipo del compañero | ¿El tipo de quién? Con más cuerpos es justo que se unan más | §5 bonificación de unión |
| Subidas de estadísticas | Enemigo más fuerte contra el compañero | Un compañero débil sobrevive a enemigos tardíos llevado por uno fuerte y sube mucho | §7 riesgos |
| Bits y objetos soltados | Los de un jugador | Duplicarlos crea dinero y objetos | §6 |
| Contadores de evolución | `battles` de un compañero | Una victoria por compañero participante | §6 |
| Combates fijos y de reclutamiento | Un compañero, con guion | Los jefes se deshacen; las condiciones de reclutamiento leen un compañero | §8 |
| Torneos | 1 contra 1 por diseño | No es un formato por equipos | §8: quedan en solitario |
| Reloj y mundo | Una partida | El mundo del anfitrión, una vez por combate | §6 |

La primera versión cooperativa (PR #363) ya cubre parte de esto: el combate corre en el anfitrión, solo pelea el
compañero de quien lo inicia, el otro jugador lanza objetos de su propia bolsa y anima, y los bits y objetos soltados se
quedan en el mundo del anfitrión (decisión 4 de [multijugador](multiplayer.md) §6).

## 3. Estrategias comparadas

| Estrategia | A favor | En contra | Veredicto |
|---|---|---|---|
| **HP enemigo según el número de jugadores** | Un solo número, predecible, mantiene cada golpe enemigo tan peligroso como en solitario; lo habitual en la industria (Diablo III suma vida por cada jugador extra, Monster Hunter World ~150 % con dos cazadores y ~220–260 % con tres o cuatro) | Combates más largos si se exagera | **Palanca principal** |
| Atq enemigo | Devuelve la presión por compañero que se pierde al repartirse | El término `Off − Def` se limita a ±500 y los factores de tipo lo multiplican: un cambio pequeño de Atq mueve mucho el daño y castiga a los compañeros débiles | Pequeño, solo con 3–4 jugadores, tope 999 |
| Def enemiga | Alarga los combates | Divide primero el daño del compañero más débil (un Novato puede dejar de hacer daño del todo); rompe la curva regional | **No** |
| MP enemigo | Más técnicas antes de agotarse; importa cuando los combates duran más | Ninguno si sigue al HP | Sí, con el HP |
| Más enemigos (bonificación de unión) | Usa la mecánica del propio original; más blancos para más compañeros; sube el factor F de las subidas de estadísticas como lo haría el original | El original tiene tope de 3; más de 3 necesita colocaciones nuevas | Sí, como bonificación a la tirada; nunca más de 3 |
| Enemigos extra por encima de 3 | Más espectáculo | Necesita colocaciones, IA y espacio en el HUD que el original no tiene; fuera del alcance (#361 excluye un cambio general de la IA) | No (abierto a un mod) |
| Agresividad de la IA (esperas más cortas, más remates) | Devuelve presión sin tocar estadísticas | Las esperas son el ritmo del combate; cambiarlas altera su identidad | Solo la velocidad de llenado del remate enemigo |
| Repartir recompensas | No crea dinero ni objetos nuevos | Recompensas más pequeñas por jugador se sienten mal | Bits: se los queda el anfitrión (decisión 4), reparto equitativo opcional |
| Duplicar recompensas | Generoso | Infla el dinero y los objetos | Solo lo que pertenece a cada compañero (subidas de estadísticas, técnicas aprendidas, contadores) |
| Escalar según la fuerza del compañero (escalado por nivel) | Justo con grupos mixtos | El original no lo tiene; oculta la curva regional y premia a los compañeros débiles | No; ver §7 para el riesgo de arrastre |

Fuentes de otros juegos: [multijugador de Monster Hunter World](https://monsterhunterworld.wiki.fextralife.com/Multiplayer)
(cerca de 150 % de HP con 2 jugadores, 220–260 % con 3 o más; baja de nuevo cuando salen jugadores), [multijugador de
Diablo III](https://di.diablowiki.net/Multiplayer) (vida de los monstruos por cada jugador extra, fijada en +50 % en el
parche 1.0.8) y [PC Gamer sobre la corrección de Diablo III](https://pcgamer.com/diablo-3-hotfix-removes-monster-damage-boost-in-co-op-inferno-teams-breathe-sigh-of-relief)
(se quitó el escalado de daño por jugador: hacía que los grupos fueran peor que en solitario).

## 4. Qué dirige el escalado

| Criterio | A favor | En contra | Veredicto |
|---|---|---|---|
| Jugadores conectados | Simple | Castiga a un jugador que pelea solo mientras los otros están en otra parte | No |
| Jugadores en el mismo mapa | Coincide con la regla de reparto de #351 | Un jugador en el mapa que no pelea cuenta igual por completo | Solo para ayudantes, con poco peso |
| **Compañeros peleando en este combate** | Mide el daño real infligido | Cambia cuando alguien entra o sale a mitad del combate | **Sí** |
| Estadísticas y progreso del compañero | Justo con grupos desiguales | Escalado por nivel; oculta la curva | No |

**Decisión:** `N` = compañeros que pelean del lado de los jugadores, contados al empezar el combate y de nuevo cada vez
que un compañero entra o sale del combate (desconexión, Huir, una llegada tardía). Cada **ayudante** (un jugador
presente que no pelea, como en el PR #363) suma un factor pequeño, porque su bolsa y sus ánimos son una ayuda real. Un
compañero noqueado sigue contando: puede ser reanimado. Cuando `N` cambia a mitad del combate, el HP y el MP enemigos se
reescalan conservando la **fracción** que tenían (un enemigo al 40 % sigue al 40 %), como hace Monster Hunter World desde
Iceborne.

## 5. Parámetros propuestos

Porcentaje del valor original. `N` cuenta los compañeros que pelean.

### 5.1 Combates normales

| Valor | N = 1 | N = 2 | N = 3 | N = 4 |
|---|---|---|---|---|
| HP máximo enemigo | 100 | 180 | 250 | 320 |
| MP máximo enemigo | 100 | 150 | 200 | 250 |
| Atq enemigo | 100 | 100 | 105 | 110 |
| Def, Vel e Int enemigas | 100 | 100 | 100 | 100 |
| Velocidad de llenado del remate enemigo | 100 | 125 | 150 | 175 |
| Bonificación a la tirada de unión (tirada más baja = se unen más; Enemy bell es −50) | 0 | −15 | −25 | −35 |
| Cada ayudante suma al HP enemigo | +15 | +15 | +15 | — |

Por qué estos números: con compañeros iguales, el daño infligido crece cerca de ×N mientras cada compañero recibe cerca
de 1/N de los golpes. El HP crece algo más despacio que N (1,8, 2,5, 3,2), así que los combates terminan algo antes que
en solitario, lo que compensan los enemigos extra de la bonificación de unión. La presión perdida vuelve por el remate
enemigo (más frecuente) y, con 3 y 4 compañeros, por un aumento del 5–10 % del Atq; el Atq nunca pasa de 999 (el tope de
la estadística). La Def nunca se escala. En la primera versión (un luchador más ayudantes) solo se aplica la fila de
ayudante: un ayudante = 115 % de HP.

### 5.2 Combates fijos y jefes (trigger 1)

| Valor | N = 1 | N = 2 | N = 3 | N = 4 |
|---|---|---|---|---|
| HP máximo enemigo | 100 | 200 | 290 | 370 |
| Atq enemigo | 100 | 105 | 110 | 115 |
| Velocidad de llenado del remate enemigo | 100 | 125 | 150 | 175 |
| Bonificación de unión | — (ya se unen todos) | — | — | — |

Los jefes suelen ir solos o en pocos, y no se puede huir de ellos: reciben algo más de HP que los combates normales para
que un jefe siga durando. Su MP no necesita escalado (no gastan nada en los combates fijos).

### 5.3 Recompensas

| Recompensa | Propuesta |
|---|---|
| Bits | Suma de los bits de los enemigos **una vez**, al mundo del anfitrión (decisión 4). Opción: repartirlos a partes iguales entre los jugadores, con el total sin cambios. |
| Objetos soltados | Una tirada por enemigo, como en el original. El objeto va al jugador cuyo compañero dio el último golpe a ese enemigo (quien inició el combate si no se sabe). Un invitado se lo lleva a casa. |
| Subidas de estadísticas | Por compañero, con la fórmula del original contra las estadísticas **sin escalar** del enemigo (el HP escalado no debe inflar las subidas de HP). Solo los compañeros que dieron al menos un golpe o seguían en pie al final. |
| Técnicas aprendidas | Por compañero, con su propia tirada, como en solitario. |
| Cuidado y contadores | Por compañero (felicidad, cansancio, comida, `battles + 1` al ganar). Reloj +20 minutos una vez, para el mundo. |
| Derrota | Ver [combate cooperativo](coop-combat.md) §4.8: el combate solo se pierde cuando todos los compañeros han caído; entonces cada compañero pierde como en solitario (vida, cuidado) y la bolsa de cada jugador pierde su 30 %. |

### 5.4 Ejemplos

Una colocación de Sabana Engranaje (ilustrativa: HP 1200, MP 300, Atq 340, Def 300), combate normal:

| | N = 1 | N = 2 | N = 3 | N = 4 |
|---|---|---|---|---|
| HP | 1200 | 2160 | 3000 | 3840 |
| MP | 300 | 450 | 600 | 750 |
| Atq | 340 | 340 | 357 | 374 |
| Meta del remate (Vel 200) | 2800 ticks de llenado | ~2240 | ~1870 | ~1600 |
| Probabilidad de que se una un segundo enemigo del mismo tipo (base 60 %) | 60 % | 75 % | 85 % | 95 % |

Machinedramon en la cima (184:51; 9400 HP, Atq 999, Def 650), combate fijo:

| | N = 1 | N = 2 | N = 3 | N = 4 |
|---|---|---|---|---|
| HP | 9400 | 18800 | 27260 | 34780 |
| Atq | 999 | 999 (tope) | 999 | 999 |
| Subidas de estadísticas de cada compañero | desde 9400 HP, 999 Atq | igual | igual | igual |

La emboscada del cañón (43:51, tres rivales, fijo) es un trío de la historia, así que cada rival usa la tabla de jefe
(HP ×2,0/2,9/3,7) en lugar de la normal (×1,8/2,5/3,2).

## 6. Progresión y economía

- **El mundo del anfitrión.** El dinero, la historia, los flags y el mapa pertenecen al anfitrión (decisión 4). El
  escalado nunca cambia los bits; la única opción de bits es cómo se reparte el mismo total.
- **Cada compañero tiene su propia progresión.** Las subidas de estadísticas, las técnicas aprendidas, el cuidado y el
  contador de evolución se aplican por compañero y viajan a casa con el invitado (el PR #363 ya envía el botín del
  compañero que pelea en `BattleEnd`).
- **Sin objetos nuevos.** Los objetos soltados se tiran una vez por enemigo. Los objetos lanzados salen de la propia
  bolsa de quien los lanza.
- **Ritmo de evolución.** Un compañero recibe `battles + 1` por cada combate ganado en el que peleó, lo mismo que una
  victoria en solitario; el cooperativo no cuenta más victorias que combates.

## 7. Riesgos

| Riesgo | Efecto | Mitigación |
|---|---|---|
| Arrastre (power leveling) | Un Novato recién criado junto a un compañero fuerte sobrevive a los combates de Mt. Infinity y sube ⌈E·F/(P·10)⌉ por estadística (E 700, P 100 → +7 por estadística y combate) | Las subidas exigen participar (§5.3). Tope opcional `coop.rewards.max_gain_per_stat` (desactivado por defecto: el original también lo permite si el compañero sobrevive) |
| HP por encima de 9999 | Los jefes con 3–4 jugadores pasan de 9999 HP | dwe guarda el HP como `int32` y el tope de 9999 es por golpe; las barras del HUD van por fracción; comprobar que ninguna cifra en pantalla supone 4 dígitos |
| Tope de Atq | Atq por encima de 999 en los jefes | Limitar a 999 |
| Más enemigos → F mayor | Con 4 jugadores son habituales los combates de 3 rivales y F = 16 | Aceptado: es la recompensa del propio original por combates más grandes; se mide en §10 |
| Combates más rápidos se sienten triviales | Cooperativo demasiado fácil | Niveles `standard` y `hard` (§9) |
| Combates más lentos se sienten largos | Los combates contra jefes se alargan con 4 | Objetivo medido en §10 (tiempo para ganar 0,9–1,3× el de solitario) |
| Economía de objetos | Cuatro bolsas de curación | Factor de ayudante; los objetos son de cada jugador, así que se gastan, no se crean |
| Manda el ruleset del anfitrión | El mod o el ajuste Fácil de un invitado se ignora en un combate compartido | Se muestra en la sala; documentado |
| Reescalar a mitad del combate | Una desconexión vuelve fácil al jefe de golpe | Conservar la fracción de HP; nunca curar |

## 8. Encuentros que necesitan un trato especial

Los ids son `script:section` (o el mapa), como en [el módulo de combate](../modules/game/battle.md) y la referencia de
la historia. «Fijo» = trigger 1 (sin Huir, se unen todos, los enemigos no gastan MP).

| Encuentro | Dónde | Id | Clase | Tratamiento |
|---|---|---|---|---|
| Agumon (primer reclutamiento) | Bosque Nativo / Ogre Fortress | 105:6, 48:5/6 | Reclutamiento | Tabla de jefe. El resultado del reclutamiento va al mundo del anfitrión |
| Bandidos del cañón | Gran Cañón | 43:51, 123:51 | Fijo, 3 rivales | Tabla de jefe, sin bonificación de unión |
| Trío de Ogre Fortress | ogre03 (pantalla 48) | 48:52 | Fijo, 3 rivales | Tabla de jefe |
| Guarida de Ogremon | pantalla 137 | 137:51 (luego reclutamiento 26:51) | Fijo, varios rivales | Tabla de jefe |
| Combates repetidos con Ogremon | gcan08_1/2, ogre03 | — | Fijo | Tabla de jefe |
| Birdramon | gcan10 | script 124, sección 0x33 | Reclutamiento fijo | Tabla de jefe |
| Etemon | Digimon Bridge, mayo08a | — | Fijo | Tabla de jefe |
| Seadramon | koda06 | — | Fijo | Tabla de jefe |
| Kokatorimon | Árboles Brumosos, mist03 | — | Fijo, según el reloj | Tabla de jefe |
| Emboscada de Greymon | Ciudad File | 162:57 (rival 9) | Emboscada | Tabla de jefe |
| Segunda emboscada (abre Mt. Infinity) | Ciudad File | 162:57 (rival 10) | Emboscada | Tabla de jefe |
| SkullGreymon | Underground Lab | 181:51 | Fijo | Tabla de jefe |
| Giromon | Pueblo Factorial | 153:5 | Reclutamiento | Tabla de jefe |
| Andromon | Pueblo Factorial | 151:5 | Cadena de reclutamiento | Tabla de jefe |
| Guardromon | fact02 | — | Fijo | Tabla de jefe |
| WaruMonzaemon | Toy Mansion | 145:5 | Jefe, necesita un compañero Monzaemon | La condición lee el compañero de **quien inicia** el combate; tabla de jefe |
| Leomon | cueva 109:5 → Sabana Engranaje | 75:51 | Reclutamiento | Tabla de jefe |
| Megadramon | Mt. Infinity | 198:53 | Jefe de la subida | Tabla de jefe |
| Machinedramon (final) | Cima de Mt. Infinity | 184:51 (rival 5) | Jefe final | Tabla de jefe; Atq ya en el tope |
| Machinedramon ×3 (posjuego) | Salidas de Back Dimension | 100:60, 63:60, 47:60 | Jefe repetible | Tabla de jefe |
| Digitamamon (posjuego) | Mt. Infinity | 184:51 con T50 | Jefe | Tabla de jefe |
| Pasillos de Back Dimension | pantallas 226–228 | scripts 185–187 | Normal con mucho HP | Tabla normal |
| Torneos y arena | Coliseo, arena | `startTournament` | 1 contra 1 | **Sin cooperativo**: solo pelea el inscrito, los demás miran; sin escalado |
| Versus | — | — | JcJ | No es cooperativo; sin cambios |

Por verificar antes de ajustar (no aparecen en las fuentes leídas): Myotismon (Mansión del Señor Gris), Devimon (51:57,
mgen06), Mamemon y Piximon. Cada fila se puede sobrescribir por id en los datos (§9).

## 9. Implementación

**Lo que entró (2026-11-20).** La opción es `coop_difficulty` (pestaña Juego: desactivada, estándar por defecto,
difícil; guardada como `UserSettings::coopDifficulty`); solo cuenta la del anfitrión. Las tablas viven en las reglas de
combate (`content/packs/game/rules/original/battle.yaml`, sección `coop_scaling`: `normal` y `fixed` con `hp_percent`,
`mp_percent`, `offense_percent` y `finisher_fill_percent` por número de compañeros, más `helper_hp_percent` y
`hard_hp_bonus_percent`), no en un `coop.yaml` nuevo, así que un mod las ajusta con el resto del combate. La función
pura es `game::battle::coopScalingFactors`; el `CoopBattles` del anfitrión pone el nivel y los ayudantes (jugadores del
mapa cuyo compañero no pelea) en `BattleSetup::coopScaling`, y `Battle::start` escala los PV y PM máximos, el ataque y la
meta del remate de los enemigos desde sus estadísticas sin escalar. `startStats` se queda sin escalar, así que las
subidas de estadísticas son las del original. Cuando un compañero huye, los enemigos se vuelven a escalar para los que
quedan, cada uno conservando su proporción de PV y PM; un compañero fuera de combate sigue contando. No se construyó de
la propuesta de abajo: el extra en la tirada de unión (un combate que empieza un invitado no tiene tirada en el
anfitrión a la que sumar), los `overrides` por combate, las opciones de recompensa de §5.3 y mostrarlo en la sala.

### 9.1 Ajuste

- `UserSettings::coopScaling` en `apps/game/src/user_settings.hpp` con los valores `off`, `standard` (valor por defecto
  recomendado para las sesiones cooperativas) y `hard` (HP +25 puntos sobre cada tabla). Se muestra en las opciones de
  juego y en la sala. Solo cuenta el valor del anfitrión; los invitados lo ven.
- Se aplica igual que la dificultad existente (`RuleOptions` en `apps/game/src/rule_options.hpp`, cambios a través de
  `RulesetStore`), pero el escalado no es un cambio fijo: depende de `N`, así que se calcula en cada combate.
- Con `N = 1` y sin ayudantes, todos los factores valen 100 sea cual sea el ajuste: el juego de un jugador nunca se toca.

### 9.2 Datos

Una nueva sección de ruleset `content/packs/game/rules/original/coop.yaml` con el esquema
`content/schemas/ruleset_coop.schema.json`:

```yaml
scaling:
  levels:
    standard:
      normal:
        hp_percent: [100, 180, 250, 320]
        mp_percent: [100, 150, 200, 250]
        off_percent: [100, 100, 105, 110]
        enemy_finisher_fill_percent: [100, 125, 150, 175]
        join_roll: [0, -15, -25, -35]
        helper_hp_percent: 15
      fixed:
        hp_percent: [100, 200, 290, 370]
        off_percent: [100, 105, 110, 115]
        enemy_finisher_fill_percent: [100, 125, 150, 175]
    hard: { extends: standard, hp_bonus_percent: 25 }
  overrides:
    "184:51": { kind: fixed }
rewards:
  bits_split: false
  max_gain_per_stat: 0   # 0 = fórmula del original, sin tope
```

Un mod lo ajusta como cualquier otra sección; `overrides` permite que un pack trate de otra forma un combate concreto.

### 9.3 Dónde se engancha

| Qué | Dónde | Cambio |
|---|---|---|
| Factores | `packages/game/battle` | Función pura `coopScaling(const CoopRules&, CoopLevel, int fighters, int helpers, bool fixed)` → `ScalingFactors`; `readCoopRules` |
| Estadísticas enemigas | `BattleAdapter::start` (`packages/session/src/battle_adapter.cpp`) | Después de construir los enemigos y antes de `Battle::start`: escalar `hp`, `mp` y `off` de `request.enemies`. Un solo sitio cubre los combates del mapa, de la historia y con guion. Los torneos se saltan |
| Copia sin escalar | `Combatant` (`combatant.hpp`) | `rewardStats`: las estadísticas originales del enemigo, que lee `statGains` en `settleBattle` (`aftermath.cpp`) |
| Tirada de unión | La tirada de unión de `mapBattleRequest` (`apps/game/src/battle_trigger.cpp`, reglas `joining`) | Sumar `join_roll[N]`; nunca más de `limits.max_enemies` |
| Remate enemigo | `FinisherRules`, llenado enemigo en el tick del combate | `enemy_fill_percent`, 100 por defecto |
| Cambio a mitad del combate | `Battle` | `Battle::rescaleEnemies(ScalingFactors)`, conservando la fracción de HP y MP de cada enemigo |
| Recompensas | `settleBattle`, `BattleAdapter::giveRewards` / `finish` | Resultados por compañero (ver [combate cooperativo](coop-combat.md) §6); bits una vez |
| Replicación | `BattleFrame` (`packages/game/coop/protocol.hpp`) | Ya lleva el HP y el MP de cada luchador; añadir el HP máximo para que las barras de los invitados sean correctas |

Todo corre en el anfitrión: los invitados nunca calculan factores, así que no hay nada que desincronizar.

## 10. Escenarios de prueba

### 10.1 Unitarias (doctest, `packages/game/battle/tests`)

- Con `N = 1` y sin ayudantes todos los factores valen 100, en todos los niveles (el juego de un jugador no cambia).
- Las tablas dan los valores de §5 para `N` 1–4, normal y fijo, con la bonificación de `hard` y con sobrescrituras.
- El Atq nunca pasa de 999; el HP por encima de 9999 se guarda y se muestra.
- Las subidas de estadísticas usan `rewardStats`: las mismas subidas con y sin escalado.
- `rescaleEnemies` conserva la fracción de HP cuando `N` sube o baja.
- La bonificación de unión nunca produce más de 3 enemigos.

### 10.2 Equilibrio (`dwe-sim`, [simulaciones](../guides/simulations.md))

Ampliar el escenario `battle` con `--param partners=<species>,<species>...` (de uno a cuatro) y el ajuste. Medir, en
1000 ejecuciones por caso: tasa de victorias, tiempo para ganar, noqueos por compañero, daño recibido por compañero,
objetos usados.

| Caso | Grupo | Enemigo | Objetivo frente al mismo compañero solo |
|---|---|---|---|
| Salvaje temprano | 2–4 Novatos | excavador de tunn04 | Tasa de victorias dentro de ±10 puntos; tiempo para ganar 0,9–1,3× |
| Grupo salvaje intermedio | 2–4 Campeones | trío de gcan08_1 | Igual, con la bonificación de unión activa |
| Salvaje tardío | 2–4 compañeros perfectos | salvaje de mgen01 | Igual |
| Jefe | 2–4 compañeros de final de juego | Machinedramon 184:51 | Tiempo para ganar 0,9–1,3×; al menos un remate del jefe por combate |
| Back Dimension | 2–4 compañeros de final de juego | colocación de pasillo (scripts 185–187) | Daño recibido por compañero 0,6–1,0× |
| Grupo mixto (arrastre) | uno perfecto + un Novato recién criado | salvaje de mgen01 | Informar de las subidas del Novato por combate con y sin el tope |

### 10.3 Juego (de dos a cuatro PCs o la configuración local de dos instancias de [multijugador](multiplayer.md) §7.1)

1. En solitario con el ajuste activo: un combate salvaje y un jefe dan exactamente los números del original (comparar con
   la traza del oráculo).
2. Dos jugadores, trío de Ogre Fortress 48:52: HP de la tabla de jefe, sin Huir, recompensas para ambos compañeros.
3. Un combate de reclutamiento (Leomon 75:51) y WaruMonzaemon 145:5, con el compañero de quien inicia el combate
   decidiendo la condición.
4. Un invitado se desconecta a mitad de un jefe: se conserva la fracción de HP y el combate continúa.
5. Una derrota con todos los compañeros caídos: la bolsa de cada jugador pierde el 30 %, cada compañero una vida.
6. Un torneo con un invitado presente: en solitario, sin escalar, el invitado mira.

## 11. Decisiones abiertas

1. Valor por defecto de `coopScaling` en las sesiones cooperativas: `standard` (recomendado) u `off`.
2. Bits: se los queda todos el anfitrión (recomendado, decisión 4) o reparto a partes iguales.
3. Tope de subidas de estadísticas para compañeros arrastrados: desactivado (recomendado, comportamiento del original) o
   activado.

## 12. Documentos relacionados

- [Diseño del sistema de combate](combat-design.md) —la base: capítulo 8, cooperativo y escalado.
- [Multijugador cooperativo en línea](multiplayer.md) y [ADR 0024](../adr/0024-co-op-with-authoritative-host.md).
- [Combate cooperativo](coop-combat.md): ambos compañeros en un combate, elección de blanco, órdenes y derrota.
- [Rulesets](rulesets.md), [módulo de combate](../modules/game/battle.md), [referencia de combate](../reference/battle.md).
- [Simulaciones](../guides/simulations.md).
