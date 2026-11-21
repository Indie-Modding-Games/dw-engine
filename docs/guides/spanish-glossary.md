# Spanish translation glossary

The terms that repeat in the translation of the original's texts, so that they always say the same thing. How it is
translated and how it is kept up to date is in [Translating the original's texts](translate.md). A new term that
repeats is added here with the translation that first uses it.

## Design lead decisions

Design lead decision, 2026-10-20:

1. Item names are translated.
2. The player is addressed informally (tuteo).
3. `HP` and `MP` stay as in the original.
4. The stages go in Spanish: Bebé, Entrenamiento, Novato, Campeón, Ultra.
5. The screens that the original splits into columns by pixel use their own localization keys in the engine's UI; the
   translation of those slots, column by column, is left only as a fallback.
6. The Spanish letters (`ñ`, vowels with an accent) on the name screen come later, with the engine's name UI; the
   original's slots stay with a–z.
7. When the original's English is broken (the battle notice for a new order), the intent is translated, not the
   literal, and all the control codes are kept.
8. The technique names are translated (2026-10-21), with words of their own that describe the attack, not with the
   names from any series; a typo in the original is translated by its intent.

## General rules

- **Neutral Latin American Spanish.** No voseo or marked regionalisms; when in doubt, the word that is understood
  throughout the region («carnada», «banana», «refrigerador»).
- **The player is addressed informally (tuteo)**, as English does («¿Quieres guardar?»).
- **Digimon names stay in English**, as the original writes them, typos included (some with typos from the
  original). They are not written in the pack: they are seen in English because the key is missing. When they
  appear inside another text they are copied letter by letter («¡Digievoluciona a `species_i`!»).
- **They are left as in the original:** the abbreviations `HP` and `MP`, the currency `Bits`, the tournament grade
  letters, the proper name `Overdell` and the curling's `GOOD` and `BAD` marks, which are drawn on the rink.
- **Opening marks** `¿` and `¡` always, except when the original attaches the text after another and the opening
  would end up in the middle (`" se enfermó."` goes after the partner's name).
- **Tags untouched:** the `{color:N}`, `{br}`, `{wait}` and the numbers `{0}`, `{1}` of each string stay the same
  (the numbers can change order; see [Translating](translate.md#text-rules)).
- **Capitalization:** as in the original. The titles of the opening menu go in capitals («NUEVA PARTIDA»).
- **Neutral gender when the subject is any Digimon:** «se lastimó», «se enfermó» instead of «está herido».

## Game and raising

| English | Spanish | Note |
|---|---|---|
| Digimon | Digimon | unchanged in the plural |
| Partner | compañero | |
| Tamer | domador | «nivel de domador» |
| Digivolve | digievolucionar | «¡Digievoluciona a `species_i`!» |
| Fresh / In-Training / Rookie / Champion / Ultimate | Bebé / Entrenamiento / Novato / Campeón / Ultra | stages |
| Digi-Egg / Fresh / In-Training / Rookie / Champion / Ultimate / Mega / Ultra | Digihuevo / Bebé I / Bebé II / Infantil / Maduro / Perfecto / Supremo / Súper Supremo | canonical stages: only with the `canonical_stages` option (off by default; `docs/modules/apps/game.md`, "Canonical stages"); without it, those of decision 4 apply, also in the tree (before it said «Definitivo») |
| Raise | criar, crianza | |
| Full (hunger) | saciar, lleno | «Sacia un poco al Digimon» |
| Tiredness | cansancio | |
| Discipline (Disc.) | disciplina (disc.) | |
| Happiness | felicidad | |
| Weight | peso | |
| Life / life span | vida | |
| Virus (Vir.) | virus (vir.) | |
| Sick, sickness | enfermo, enfermedad | |
| Wound | herida | |
| Status errors | estados alterados | |
| Coma | coma | |
| Poison / Confusion / Stun / Flat | veneno / confusión / aturdido / aplastado | abbreviated «vene», «conf», «atur», «apla» |
| Potty | baño | «ir al baño» |
| Rest / sleep | descanso, dormir | |
| Training | entrenamiento | |
| Medal | medalla | |
| Card | carta | the Digimon cards |
| Bits | Bits | the currency |

## Attributes

| English | Spanish | Abbreviation |
|---|---|---|
| HP | HP | |
| MP | MP | |
| Offense, Off. Power | Ataque | Atq |
| Defense, Def. Power | Defensa | Def |
| Speed | Velocidad | Vel |
| Brains | Inteligencia | Int |
| Abilities, skills | habilidades | |

## Battle and techniques

| English | Spanish | Note |
|---|---|---|
| Technique, Tech | técnica, téc. | |
| Mastered / Unmastered | dominada / sin dominar | |
| Set technique | fijar técnica | |
| Battle | batalla | |
| Run | Huir / ¡Huye! | order / shout |
| Attack | Atacar / ¡Ataca! | |
| Your Call, Auto | A tu modo, Automático | |
| Moderate | Moderado / ¡Con calma! | |
| Distance | Distancia | |
| Defensive | Defensiva / ¡Defiende! | |
| Change | Cambiar / ¡Cambia! | |
| Change target | ¡Cambia de blanco! | «¡Otro blanco!» if it does not fit |
| Go all the way, Give it all you got | ¡Con todo! | |
| Hang in there, Put up with it | ¡Aguanta! | |
| Technique mastered | técnica dominada | |

## Items

| English | Spanish | Note |
|---|---|---|
| Item | objeto | |
| Use / Move / Sort / Drop | Usar / Mover / Orden / Tirar | inventory menu |
| Recovery | recuperación, recup. | |
| Floppy, Disk | disquete, disco | |
| Chip | chip | |
| Mushroom (mushrm) | hongo | |
| Berry | baya | |
| Banana | banana | |
| Meat | carne | |
| Bait | carnada | fishing |
| Fishing rod | caña | |
| Key | llave | |

Item names are translated and fit in about 12 characters, like the original's.

## Places

| English | Spanish |
|---|---|
| File City | Ciudad File |
| Mt. | Monte (Mte. if it does not fit) |
| Drill Tunnel | Túnel Taladro |
| Great Canyon | Gran Cañón |
| Freezeland | Tierra Helada |
| Gear Savanna | Sabana Engranaje |
| Toy Town | Pueblo Juguete |
| Factorial Town | Pueblo Factorial |
| Beetle Land | Tierra Escarabajo |
| Trash Mountain | Montaña de Basura |
| Grey Lord's Mansion | Mansión del Señor Gris |
| Misty Trees | Árboles Brumosos |
| Ancient Dino Region | Región Dino Antigua |
| Overdell | Overdell |

Map names are centered in 24 characters with spaces, like the original's.

## Tournaments

| English | Spanish |
|---|---|
| Tournament | torneo |
| Grade D… | Clase D… |
| Cup | Copa |
| Version 1 Cup | Copa Versión 1 |
| Fire / Grapple / Thunder Wind / Cool / Nature / Metalic / Filth | Fuego / Lucha / Trueno-Viento / Frío / Naturaleza / Metal / Mugre |
| Dino / Wing / Animal / Human / Beetle | Dino / Ala / Animal / Humana / Escarabajo |

## Menus and system

| English | Spanish |
|---|---|
| New game / Continue | Nueva partida / Continuar |
| Save / Load / Delete / Copy | Guardar / Cargar / Borrar / Copiar |
| Data file | archivo |
| Format | formatear |
| Memory Card | tarjeta de memoria (or «la tarjeta») |
| Slot | ranura |
| Register, registration | registrar, registro |
| Select / Cancel | Elegir / Cancelar |
| Yes / No | Sí / No |
| Pause | Pausa |
| Name | Nombre |
| Player | Jugador |
| Directional Pad | botones de dirección |
| Press X | Presiona X |
| Game mode / variant | Modo de juego / variante |
| Classic Enhanced / Folder Mode | Clásico Mejorado / Modo Folder |
| Fixed in {variant} (locked option) | Fija en {variant} |
| Open to new players (co-op) | Abierto a nuevos jugadores |
| Player (default online name) | Jugador |
| Bits (currency label in menus) | Bits |
| Naming page of accented letters | ÁÉÍ (Á É Í Ó Ú Ñ Ü) |

## Story

The dialogue of the event scripts (`text/es/story_<n>.yaml`). The speaker's line
(`{color:7}<name> {color:1}\n`) is copied the same and the characters' names stay in English.

| English | Spanish | Note |
|---|---|---|
| Digivice | Digivice | |
| Merit, Merit points | méritos, puntos de mérito | the card shop |
| Card, Trading card | carta, carta coleccionable | |
| Tournament Schedule | Calendario de torneos | |
| Enter (a tournament), Register | inscribirse | |
| Qualifications | Requisitos | |
| Battle Royal | batalla campal | |
| X joins the city! | ¡X se une a la ciudad! | on two lines if it does not fit |
| Begin / Explain / Quit | Empezar / Explicar / Salir | the training sessions |
| Explain again / Bonus Try | Explicar otra vez / Intento extra | |
| Slot game | Tragamonedas | |
| Vending Machine | máquina expendedora | prices in figures: «Carne: 300 Bits» |
| Elevator | elevador | |
| Ogre mode | Modo Ogro | |
| Dimensional Key Chain | Llavero Dimensional | |
| Lake Guardian | Guardián del Lago | |
| Glacial / Speedy Time Zone | Zona de Tiempo Glacial / Veloz | |
| Market manager | Encargado del mercado | |
| Come again. | Vuelve pronto. | the shops |
| See ya (option) | Adiós | |
