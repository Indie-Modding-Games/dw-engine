# French translation glossary

The terms that repeat in the French translation (code `fr`) of the engine's texts
(`content/packs/game/localization/fr/`) and of the original's texts (`content/packs/fr-translation/`), so that they
always say the same thing. How a translation is written and kept up to date is in
[Translating the original's texts](translate.md); the Spanish choices this one started from are in the
[Spanish glossary](spanish-glossary.md). A new term that repeats is added here with the translation that first uses it.

## Decisions

1. **Informal "you" (tutoiement)**, as the Spanish chose tuteo: every character says «tu» to the player and to each
   other, in the friendly, simple tone of the original.
2. **Digimon names stay in English**, letter by letter, typos included; they are not written in the pack (the
   missing key shows the English). The two titles of that block that are English words are translated:
   `Market Manager` («Gérant du marché») and `main character` («héros»).
3. **Technique names are translated** with words of their own that describe the attack (not names from any series);
   a typo in the original is translated by its intent.
4. **Item names are translated** and fit in about 12 characters, like the original's (abbreviated when
   needed: «S.disque Atq», «Puce Int»; the stat abbreviations take no dot).
5. **`HP`, `MP` and `Bits` stay as in the original**, like in Spanish: the gauges and the shared tooling
   (`kept_original_ids` in `scripts/translation.sh`) treat them as symbols, not words. They are masculine:
   «tout le HP» is avoided, «HP au max» is used.
6. **Stages:** Bébé, Apprenti, Novice, Champion, Ultime.
7. **Places are translated** except proper names (`Overdell`, `Coela`, `Geko`, `Centar`, `Amida`, `Birdra`...).
8. **Punctuation:** a plain space before `!`, `?`, `:` and `;` (not a no-break space: the original look's font and
   every skin draw it, and the story lines are already broken by hand), «guillemets» with a space inside, `...` as in
   the original, the straight apostrophe `'`. Accents are kept on capitals (`É`, `À`).
9. **Text that does not fit is shortened** the way a French localizer would (abbreviations with a dot, a shorter
   synonym), never by cutting a word.
10. **Broken English is translated by its intent**, keeping every control code (the Spanish rule).
11. **Map signs** keep the sign's size, so a long name is shortened there only: «Mt Panorama» (no dot, as «Mme»)
    on the small arrow boards, and the fishing board's graffiti says «Chut~ On s'en fiche».

## General rules

- **Tags untouched:** `{color:N}`, `{br}`, `{wait}`, `{stop}`, `{partner}`, `{tamer}`, `{shopvar}`, `{item:N}`,
  `{num:N}`, `{0}`, `{1}`... stay the same and in the same number (the numbered ones can change order).
- **Speaker line:** `{color:7}<name> {color:1}\n` is copied the same.
- **Story lines:** up to 30 characters per line not counting tags, 4 lines per box, broken with `\n` like the English
  when possible.
- **Gender:** «le Digimon» is masculine; the partner's name followed by a verb avoids agreement when possible
  («{partner} est malade» rather than an adjective that would need a gender).
- **Capitalization:** as in the original; the opening menu's titles go in capitals («NOUVELLE PARTIE»).
- **Map names** are centered in 24 characters with spaces, like the original's.

## Game and raising

| English | French | Note |
|---|---|---|
| Digimon | Digimon | unchanged in the plural, masculine |
| Partner | partenaire | |
| Tamer | dresseur | «niveau de dresseur» |
| Digivolve | digivoluer | «Digivolue en Greymon !» |
| Fresh / In-Training / Rookie / Champion / Ultimate | Bébé / Apprenti / Novice / Champion / Ultime | stages |
| Digi-Egg / Fresh / In-Training / Rookie / Champion / Ultimate / Mega / Ultra | Digiœuf / Bébé I / Bébé II / Enfant / Adulte / Parfait / Suprême / Super Suprême | canonical stages (`canonical_stages` option only) |
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
| Poison / Confusion / Stun / Flat | poison / confusion / étourdi / aplati | abbreviated «pois», «conf», «étou», «apla» |
| Potty | toilettes, «faire ses besoins» | |
| Poop | caca | |
| Rest / sleep | repos, dormir | |
| Training | entraînement | |
| Medal | médaille | |
| Card | carte | the Digimon cards |
| Bits | Bits | the currency |

## Attributes

| English | French | Abbreviation |
|---|---|---|
| HP | HP | |
| MP | MP | |
| Offense, Off. Power | Attaque | Atq |
| Defense, Def. Power | Défense | Déf |
| Speed | Vitesse | Vit |
| Brains | Intelligence | Int |
| Abilities, skills | capacités | |

## Battle and techniques

| English | French | Note |
|---|---|---|
| Technique, Tech | technique, tech. | |
| Mastered / Unmastered | maîtrisée / non maîtrisée | |
| Set technique | technique choisie | |
| Battle | combat | |
| Run | Fuir / Fuis ! | order / shout |
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

## Items

| English | French | Note |
|---|---|---|
| Item | objet | |
| Use / Move / Sort / Drop | Utiliser / Déplacer / Trier / Jeter | inventory menu |
| Recovery | soin | «Petit soin», «Super soin» |
| Floppy, Disk | disquette, disque | |
| Chip | puce | |
| Mushroom (mushrm) | champignon (champi) | |
| Berry | baie | |
| Banana | banane | |
| Meat | viande | |
| Bait | appât | fishing |
| Fishing rod | canne à pêche, canne | |
| Key | clé | |

## Places

| English | French |
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

## Tournaments

| English | French |
|---|---|
| Tournament | tournoi |
| Grade D… | Classe D… |
| Cup | Coupe |
| Version 1 Cup | Coupe Version 1 |
| Fire / Grapple / Thunder Wind / Cool / Nature / Metalic / Filth | Feu / Lutte / Tonnerre-Vent / Glace / Nature / Métal / Crasse |
| Dino / Wing / Animal / Human / Beetle | Dino / Aile / Animal / Humain / Scarabée |

## Menus and system

| English | French |
|---|---|
| New game / Continue | Nouvelle partie / Continuer |
| Save / Load / Delete / Copy | Sauvegarder / Charger / Effacer / Copier |
| Data file | fichier |
| Format | formater |
| Memory Card | carte mémoire |
| Slot | emplacement («CARTE 1» in the title menu) |
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
| Game mode / variant | Mode de jeu / variante |
| Classic Enhanced / Folder Mode | Classique Amélioré / Mode Folder |
| Fixed in {variant} (locked option) | Fixe en {variant} |
| Open to new players (co-op) | Ouvert aux nouveaux joueurs |
| Player (default online name) | Joueur |
| Bits (currency label in menus) | Bits |
| Naming page of accented letters | ÀÂÇ (À Â Ç É È Ê Ë Î Ï Ô Œ Ù Û Ü Ÿ Æ) |

## Story

| English | French | Note |
|---|---|---|
| Digivice | Digivice | |
| Merit, Merit points | mérite, points de mérite | the card shop |
| Card, Trading card | carte, carte à collectionner | |
| Tournament Schedule | Calendrier des tournois | |
| Enter (a tournament), Register | s'inscrire | |
| Qualifications | Conditions | |
| Battle Royal | mêlée générale | |
| X joins the city! | X rejoint la ville ! | |
| Begin / Explain / Quit | Commencer / Expliquer / Quitter | training sessions |
| Explain again / Bonus Try | Réexpliquer / Essai bonus | |
| Slot game | Machine à sous | |
| Vending Machine | distributeur | prices in figures: «Viande : 300 Bits» |
| Elevator | ascenseur | |
| Ogre mode | Mode Ogre | |
| Dimensional Key Chain | Porte-clés dimensionnel | |
| Lake Guardian | Gardien du Lac | |
| Glacial / Speedy Time Zone | Ère Glaciaire / Ère Rapide | |
| Market manager | Gérant du marché | |
| Come again. | Reviens vite. | the shops |
| See ya (option) | Au revoir | |
| Thanks. | Merci. | |
| Insectoid | Insecte | «tournoi des Guerriers Insectes» |
| Treasure Hunter | chasseur de trésors | |
| Item Keeper | gardien d'objets | |
| Save Machine for Memory Card Battle Data | machine de sauvegarde des données de combat | |
| Shell Newspaper | Journal Coquille | |
| Digitama | Digitama | the egg a partner leaves when it fades away |
| Tamer Level went up / down | Niveau de dresseur en hausse / en baisse | |
| Total Profit / Profit | Gain total / Gain | the shop job |
