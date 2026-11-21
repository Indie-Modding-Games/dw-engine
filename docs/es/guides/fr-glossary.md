# Glosario de la traducción al francés

Los términos que se repiten en la traducción al francés (código `fr`) de los textos del motor
(`content/packs/game/localization/fr/`) y de los textos del original (`content/packs/fr-translation/`), para que digan
siempre lo mismo. Cómo se escribe y se mantiene al día una traducción está en
[Traducir los textos del original](translate.md); las decisiones del español de las que parte están en el
[glosario del español](spanish-glossary.md). Un término nuevo que se repite se añade aquí con la traducción que lo usa
por primera vez.

## Decisiones

1. **Tuteo (tutoiement)**, como eligió el español: todos los personajes dicen «tu» al jugador y entre ellos, con el
   tono cercano y simple del original.
2. **Los nombres de Digimon quedan en inglés**, letra por letra, erratas incluidas; no se escriben en el pack (la clave
   que falta muestra el inglés). Los dos títulos de ese bloque que son palabras inglesas se traducen:
   `Market Manager` («Gérant du marché») y `main character` («héros»).
3. **Los nombres de técnica se traducen** con palabras propias que describen el ataque (no nombres de ninguna serie);
   una errata del original se traduce por su intención.
4. **Los nombres de objeto se traducen** y caben en unos 12 caracteres, como los del original (abreviados
   cuando hace falta: «S.disque Atq», «Puce Int»; las abreviaturas de atributo van sin punto).
5. **`HP`, `MP` y `Bits` quedan como en el original**, igual que en español: los indicadores y las herramientas
   comunes (`kept_original_ids` en `scripts/translation.sh`) los tratan como símbolos, no como palabras. Son
   masculinos: se evita «tout le HP» y se usa «HP au max».
6. **Etapas:** Bébé, Apprenti, Novice, Champion, Ultime.
7. **Los lugares se traducen** salvo los nombres propios (`Overdell`, `Coela`, `Geko`, `Centar`, `Amida`, `Birdra`...).
8. **Puntuación:** un espacio normal antes de `!`, `?`, `:` y `;` (no un espacio de no separación: la fuente del
   aspecto original y todas las skins lo dibujan, y las líneas de la historia ya se cortan a mano), «comillas» con
   espacio por dentro, `...` como en el original, el apóstrofo recto `'`. Las mayúsculas llevan tilde (`É`, `À`).
9. **El texto que no cabe se acorta** como lo haría un localizador francés (abreviaturas con punto, un sinónimo más
   corto), nunca cortando una palabra.
10. **El inglés roto se traduce por su intención**, conservando todos los códigos de control (la regla del español).

## Reglas generales

- **Etiquetas intactas:** `{color:N}`, `{br}`, `{wait}`, `{stop}`, `{partner}`, `{tamer}`, `{shopvar}`, `{item:N}`,
  `{num:N}`, `{0}`, `{1}`... quedan iguales y en el mismo número (las numeradas pueden cambiar de orden).
- **Línea del hablante:** `{color:7}<nombre> {color:1}\n` se copia igual.
- **Líneas de la historia:** hasta 30 caracteres por línea sin contar etiquetas, 4 líneas por cuadro, cortadas con
  `\n` como el inglés cuando se puede.
- **Género:** «le Digimon» es masculino; el nombre del compañero seguido de un verbo evita la concordancia cuando se
  puede («{partner} est malade» mejor que un adjetivo que necesite género).
- **Mayúsculas:** como en el original; los títulos del menú inicial van en mayúsculas («NOUVELLE PARTIE»).
- **Nombres de mapa** centrados en 24 caracteres con espacios, como los del original.

## Juego y crianza

| Inglés | Francés | Nota |
|---|---|---|
| Digimon | Digimon | invariable en plural, masculino |
| Partner | partenaire | |
| Tamer | dresseur | «niveau de dresseur» |
| Digivolve | digivoluer | «Digivolue en Greymon !» |
| Fresh / In-Training / Rookie / Champion / Ultimate | Bébé / Apprenti / Novice / Champion / Ultime | etapas |
| Digi-Egg / Fresh / In-Training / Rookie / Champion / Ultimate / Mega / Ultra | Digiœuf / Bébé I / Bébé II / Enfant / Adulte / Parfait / Suprême / Super Suprême | etapas canónicas (solo con la opción `canonical_stages`) |
| Raise | élever, élevage | |
| Full (hunger) | rassasier, rassasié | «Rassasie un peu le Digimon» |
| Hungry | avoir faim | |
| Tiredness | fatigue | |
| Discipline (Disc.) | discipline (disc.) | |
| Happiness | bonheur (bonh.) | |
| Weight | poids | |
| Life / life span | vie, durée de vie | |
| Virus (Vir.) | virus (vir.) | |
| Sick, sickness | malade, maladie | |
| Wound, injured | blessure, blessé | |
| Status errors | altérations d'état | |
| Coma | coma | |
| Poison / Confusion / Stun / Flat | poison / confusion / étourdi / aplati | abreviados «pois», «conf», «étou», «apla» |
| Potty | toilettes, «faire ses besoins» | |
| Poop | caca | |
| Rest / sleep | repos, dormir | |
| Training | entraînement | |
| Medal | médaille | |
| Card | carte | las cartas Digimon |
| Bits | Bits | la moneda |

## Atributos

| Inglés | Francés | Abreviatura |
|---|---|---|
| HP | HP | |
| MP | MP | |
| Offense, Off. Power | Attaque | Atq |
| Defense, Def. Power | Défense | Déf |
| Speed | Vitesse | Vit |
| Brains | Intelligence | Int |
| Abilities, skills | capacités | |

## Combate y técnicas

| Inglés | Francés | Nota |
|---|---|---|
| Technique, Tech | technique, tech. | |
| Mastered / Unmastered | maîtrisée / non maîtrisée | |
| Set technique | technique choisie | |
| Battle | combat | |
| Run | Fuir / Fuis ! | orden / grito |
| Attack | Attaquer / Attaque ! | |
| Your Call, Auto | À ta guise, Auto | |
| Moderate | Modéré / Doucement ! | |
| Distance | Distance / Recule ! | |
| Defensive | Défense / En garde ! | |
| Change | Changer / Change ! | |
| Change target | Change de cible ! | |
| Go all the way, Give it all you got | À fond ! | |
| Hang in there, Put up with it | Tiens bon ! | |
| Technique mastered | technique maîtrisée | |
| Enemy | ennemi | |
| Finisher | coup final | |

## Objetos

| Inglés | Francés | Nota |
|---|---|---|
| Item | objet | |
| Use / Move / Sort / Drop | Utiliser / Déplacer / Trier / Jeter | menú del inventario |
| Recovery | soin | «Petit soin», «Super soin» |
| Floppy, Disk | disquette, disque | |
| Chip | puce | |
| Mushroom (mushrm) | champignon (champi) | |
| Berry | baie | |
| Banana | banane | |
| Meat | viande | |
| Bait | appât | pesca |
| Fishing rod | canne à pêche, canne | |
| Key | clé | |

## Lugares

| Inglés | Francés |
|---|---|
| File Island | île File |
| File City | Cité File |
| Mt. | Mont |
| Native Forest | Forêt Natale |
| Drill Tunnel | Tunnel Foreuse |
| Great Canyon | Grand Canyon |
| Freezeland | Terre Gelée |
| Gear Savanna | Savane Engrenage |
| Toy Town | Ville Jouet |
| Factorial Town | Ville Factorielle |
| Beetle Land | Terre Scarabée |
| Trash Mountain | Mont Ordures |
| Grey Lord's Mansion | Manoir du Seigneur Gris |
| Misty Trees | Bois Brumeux |
| Ancient Dino Region | Région Dino Antique |
| Tropical Jungle | Jungle Tropicale |
| Ogre Fortress | Forteresse Ogre |
| Secret Beach Cave | Grotte Plage Secrète |
| Mt. Infinity | Mont Infini |
| Mt. Panorama | Mont Panorama |
| Dragon Eye Lake | Lac Œil de Dragon |
| Geko Swamp | Marais Geko |
| Volume Villa | Villa Volume |
| Iceberg Beach | Plage Iceberg |
| Coela Point | Pointe Coela |
| Dust Kingdom | Royaume Poussière |
| Overdell | Overdell |

## Torneos

| Inglés | Francés |
|---|---|
| Tournament | tournoi |
| Grade D… | Classe D… |
| Cup | Coupe |
| Version 1 Cup | Coupe Version 1 |
| Fire / Grapple / Thunder Wind / Cool / Nature / Metalic / Filth | Feu / Lutte / Tonnerre-Vent / Glace / Nature / Métal / Crasse |
| Dino / Wing / Animal / Human / Beetle | Dino / Aile / Animal / Humain / Scarabée |

## Menús y sistema

| Inglés | Francés |
|---|---|
| New game / Continue | Nouvelle partie / Continuer |
| Save / Load / Delete / Copy | Sauvegarder / Charger / Effacer / Copier |
| Data file | fichier |
| Format | formater |
| Memory Card | carte mémoire |
| Slot | emplacement («CARTE 1» en el menú de título) |
| Register, registration | enregistrer, enregistrement |
| Select / Cancel / Back | Choisir / Annuler / Retour |
| Yes / No | Oui / Non |
| Pause | Pause |
| Name | Nom |
| Player | Joueur |
| Options, settings | Options |
| Directional Pad | touches directionnelles |
| Press X | Appuie sur X |
| Online, co-op, room | en ligne, coop, salon |
| Host / Guest | hôte / invité |

## Historia

| Inglés | Francés | Nota |
|---|---|---|
| Digivice | Digivice | |
| Merit, Merit points | mérite, points de mérite | la tienda de cartas |
| Card, Trading card | carte, carte à collectionner | |
| Tournament Schedule | Calendrier des tournois | |
| Enter (a tournament), Register | s'inscrire | |
| Qualifications | Conditions | |
| Battle Royal | mêlée générale | |
| X joins the city! | X rejoint la ville ! | |
| Begin / Explain / Quit | Commencer / Expliquer / Quitter | sesiones de entrenamiento |
| Explain again / Bonus Try | Réexpliquer / Essai bonus | |
| Slot game | Machine à sous | |
| Vending Machine | distributeur | precios en cifras: «Viande : 300 Bits» |
| Elevator | ascenseur | |
| Ogre mode | Mode Ogre | |
| Dimensional Key Chain | Porte-clés dimensionnel | |
| Lake Guardian | Gardien du Lac | |
| Glacial / Speedy Time Zone | Ère Glaciaire / Ère Rapide | |
| Market manager | Gérant du marché | |
| Come again. | Reviens vite. | las tiendas |
| See ya (option) | Au revoir | |
| Thanks. | Merci. | |
| Insectoid | Insecte | «tournoi des Guerriers Insectes» |
| Treasure Hunter | chasseur de trésors | |
| Item Keeper | gardien d'objets | |
| Save Machine for Memory Card Battle Data | machine de sauvegarde des données de combat | |
| Shell Newspaper | Journal Coquille | |
| Digitama | Digitama | el huevo que deja el compañero al desaparecer |
| Tamer Level went up / down | Niveau de dresseur en hausse / en baisse | |
| Total Profit / Profit | Gain total / Gain | el trabajo en la tienda |
