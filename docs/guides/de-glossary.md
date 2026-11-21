# German translation glossary

The terms that repeat in the German translation (code `de`), so that they always say the same thing. It covers both
the engine's texts (`content/packs/game/localization/de/`) and the original's texts
(`content/packs/de-translation/text/de/`, the same flow as Spanish: [Translating the original's texts](translate.md)).
A new term that repeats is added here with the translation that first uses it. The Spanish decisions are in the
[Spanish glossary](spanish-glossary.md); German follows them unless this page says otherwise.

## Decisions

1. Standard German (Germany), modern and friendly, like a children's adventure game: short sentences, everyday words.
2. The player is addressed informally with a lowercase **du** («Willst du speichern?»); characters talk to each other
   with *du* as well, except where the English is clearly formal (a shop clerk greeting a customer still says *du*:
   the island is a small, friendly place).
3. Item names, technique names, places, medals, tournaments and music titles are translated, with words of our own.
   No name is taken from any other translation, dub or game.
4. Digimon names and the names of the characters stay in English, letter by letter, typos included. German declines
   them only with the genitive *s* where a sentence needs it («Jijimons Haus», «Leomons Ahnen»).
5. `HP`, `MP` and `Bits` stay. Numbers keep the original's figures.
6. The stages: **Baby, Training, Neuling, Champion, Ultra** (Fresh, In-Training, Rookie, Champion, Ultimate). With the
   `canonical_stages` option: **Digi-Ei, Baby I, Baby II, Kind, Erwachsen, Perfekt, Ultimativ, Super-Ultimativ**.
7. When the English is broken, the intent is translated; every control code and placeholder stays.
8. Typography: German quotation marks „…“ where the text quotes, `…` as three dots like the original, `-` for hyphens.
   Nouns are capitalised as German requires; menu titles that the original writes in capitals stay in capitals
   («NEUES SPIEL»).
9. Fit: German runs about a third longer than English. Item names fit in about 13 characters, story lines in 30, map
   names are centred in 24. When a phrase does not fit, it is shortened the way a German localiser would (a shorter
   synonym, a compound, a common abbreviation with a dot: «Ang.», «Vert.»), never cut mid-word.

## Game and raising

| English | German | Note |
|---|---|---|
| Digimon | Digimon | unchanged in the plural |
| Partner | Partner | |
| Tamer | Bändiger | «Bändigerstufe» (tamer level) |
| Digivolve, digivolution | digievolvieren, Digievolution | «Digievolviere zu `species_i`!» |
| Digi-Egg | Digi-Ei | |
| Raise | aufziehen, Aufzucht | |
| Full (hunger) | satt | «Macht das Digimon etwas satt» |
| Hunger | Hunger | |
| Tiredness | Müdigkeit | |
| Discipline (Disc.) | Disziplin (Diszi.) | |
| Happiness | Glück | |
| Weight | Gewicht | short: «Gew.» (partner page) |
| Life, life span | Lebensdauer | short: «Lebensd.» (partner page) |
| Virus (Vir.) | Virus (Vir.) | |
| Sick, sickness | krank, Krankheit | |
| Wound, injured | Wunde, verletzt | |
| Status errors | Statusprobleme | |
| Coma | Koma | |
| Poison / Confusion / Stun / Flat | Gift / Verwirrung / Betäubung / Platt | short: «Gift», «Verw.», «Betäub.», «Platt» |
| Potty | Klo | «aufs Klo gehen» |
| Rest, sleep | ausruhen, schlafen | |
| Training | Training | |
| Praise / Scold | Loben / Schimpfen | |
| Medal | Medaille | |
| Card | Karte | the Digimon cards |
| Bits | Bits | the currency |

## Attributes

| English | German | Short |
|---|---|---|
| HP | HP | |
| MP | MP | |
| Offense, Off. Power | Angriff | Ang. |
| Defense, Def. Power | Verteidigung | Vert. |
| Speed | Tempo | Tmp. |
| Brains | Intelligenz | Int. |
| Abilities, skills | Fähigkeiten, Werte | |

## Battle and techniques

| English | German | Note |
|---|---|---|
| Technique, Tech | Technik, Tech. | |
| Power / Range / Effect (technique columns) | KRAFT / RW / EFF | |
| Mastered / Unmastered | gemeistert / nicht gemeistert | |
| Set technique | Technik festlegen | |
| Battle | Kampf | |
| Run | Fliehen / Lauf weg! | order / shout |
| Attack | Angreifen / Greif an! | |
| Your Call, Auto | Nach Belieben, Automatisch | |
| Moderate | Gemäßigt / Ganz ruhig! | |
| Distance | Abstand | |
| Defensive | Defensiv / Verteidige! | |
| Change | Wechseln / Wechsel! | |
| Change target | Ziel wechseln! | «Anderes Ziel!» |
| Go all the way, Give it all you got | Gib alles! | |
| Hang in there | Halte durch! | |
| Finishing move | Spezialtechnik | |
| Enemy | Gegner | |

Technique names are our own German words that describe the attack («Feuerturm», «Eisnadel»), in a compound or two
words, up to about 16 characters.

## Items

| English | German | Note |
|---|---|---|
| Item | Item | «Items» in menus; in dialogue also «Gegenstand» |
| Use / Move / Sort / Drop | Benutzen / Verschieben / Sortieren / Wegwerfen | inventory menu |
| Recovery | Heilung | «Heilung kl./mit./gr.» |
| Floppy, Disk | Diskette, Disk | |
| Chip | Chip | |
| Mushroom | Pilz | |
| Berry | Beere | |
| Banana | Banane | |
| Meat | Fleisch | |
| Bait | Köder | fishing |
| Fishing rod | Angel | |
| Key | Schlüssel | |
| Item bank, Item Keeper | Itemlager | |

The full list of item names is `main.item_names` in `text/de/main.yaml`; every other text that names an item uses
that same name.

## Places

| English | German |
|---|---|
| File City | File-Stadt |
| Mt. Panorama | Panoramaberg |
| Native Forest | Heimatwald |
| Drill Tunnel | Bohrtunnel |
| Great Canyon | Großer Canyon |
| Freezeland | Frostland |
| Gear Savanna | Zahnradsavanne |
| Toy Town | Spielzeugstadt |
| Factorial Town | Fabrikstadt |
| Beetle Land | Käferland |
| Trash Mountain | Müllberg |
| Grey Lord's Mansion | Villa des Grauen Lords |
| Misty Trees | Nebelwald |
| Ancient Dino Region | Urzeit-Dinoregion |
| Mt. Infinity | Unendlichkeitsberg |
| Volume Villa | Dorf Volumen |
| Geko Swamp | Geko-Sumpf |
| Ogre Fortress | Ogerfestung |
| Overdell | Overdell |
| Back Dimension | Rückdimension |
| Green Gym | Grüner Trainingsplatz («Trainingsplatz» when short) |
| Item Keeper, item bank | Itemlager |
| File Island | File-Insel |
| Digimon World (the place) | Digimon-Welt |
| Net World | Netzwelt |
| Iceberg Beach | Eisbergstrand |
| Training Camp | Trainingslager |
| Ancient Dino Tomb | Urzeit-Dinograb |

The full list is `main.map_names` in `text/de/main.yaml`, centred in 24 characters with spaces like the original.

## Tournaments

| English | German |
|---|---|
| Tournament | Turnier |
| Grade D… | Klasse D… |
| Cup | Pokal |
| Version 1 Cup | Version-1-Pokal |
| Fire / Grapple / Thunder Wind / Cool / Nature / Metalic / Filth | Feuer / Ringen / Donner-Wind / Kälte / Natur / Metall / Dreck |
| Dino / Wing / Animal / Human / Beetle | Dino / Flügel / Tier / Mensch / Käfer |
| Battle Royal | Massenkampf |
| Grade / Special tournament | Klassenturnier / Sonderturnier |
| Insectoid Warrior Tournament | Insektenkrieger-Turnier |

Cup names join with hyphens: «Feuer-Pokal», «Version-1-Pokal». Tamer level titles: Amateur, Lehrling, Anfänger,
Erfahren, Profi, Veteran, Elite, Super, Meister, Genie, Legende («Neuling» is kept for the stage).

## Menus and system

| English | German |
|---|---|
| New game / Continue | Neues Spiel / Fortsetzen |
| Save / Load / Delete / Copy | Speichern / Laden / Löschen / Kopieren |
| Data file | Spielstand |
| Format | formatieren |
| Memory Card | Memory Card |
| Slot | Steckplatz (menus), Platz (saves) |
| Select / Cancel / Back | Auswählen / Abbrechen / Zurück |
| Yes / No | Ja / Nein |
| Pause | Pause |
| Options | Optionen |
| Name | Name |
| Player | Spieler |
| Level | Stufe |
| Press X | Drück X |
| Skin (UI look) | Design |
| Co-op / Versus / Room / Host / Guest | Koop / Versus / Raum / Gastgeber / Gast |
| Game mode / variant | Spielmodus / Variante |
| Classic Enhanced / Folder Mode | Verbesserter Klassiker / Folder-Modus |
| Fixed in {variant} (locked option) | Fest: {variant} |
| Open to new players (co-op) | Offen für neue Spieler |
| Player (default online name) | Spieler |
| Bits (currency label in menus) | Bits |
| Naming page of accented letters | ÄÖÜ (Ä Ö Ü ß) |
| The game's log (where an error's details go) | Spielprotokoll |
| Unknown variant (a save's mode that is not installed) | Unbekannte Variante |
| Key caps of the classic prompts (`prompt.cap.*`) | ESC, @tab, ENTF, EINFG, POS1, ENDE, BILDAUF, BILDAB, @caps_lock, @shift, STRG, ALT, ALTGR |

## Story

The dialogue of the event scripts (`text/de/story_<n>.yaml`). The speaker's line (`{color:7}<name> {color:1}\n`) is
copied the same and the characters' names stay in English. Lines are at most 30 characters (tags do not count) and a
text has at most 4 lines per box, like the English.

| English | German | Note |
|---|---|---|
| Digivice | Digivice | |
| Merit, Merit points | Verdienste, Verdienstpunkte | the card shop |
| Card, Trading card | Karte, Sammelkarte | |
| Tournament Schedule | Turnierplan | |
| Enter (a tournament), Register | sich anmelden | |
| Qualifications | Bedingungen | |
| X joins the city! | X zieht in die Stadt! | on two lines if it does not fit |
| Begin / Explain / Quit | Starten / Erklären / Beenden | the training sessions |
| Explain again / Bonus Try | Nochmal erklären / Bonusversuch | |
| Slot game | Spielautomat | |
| Vending Machine | Automat | prices in figures: «Fleisch: 300 Bits» |
| Elevator | Aufzug | |
| Ogre mode | Oger-Modus | |
| Dimensional Key Chain | Dimensionsschlüsselbund | |
| Lake Guardian | Wächter des Sees | |
| Glacial / Speedy Time Zone | Eiszeit- / Tempo-Zeitzone | |
| Market manager | Marktleiter | |
| Come again. | Komm bald wieder. | the shops |
| See ya (option) | Tschüss | |
| main character (unnamed player) | Hauptfigur | `main.digimon_names.0000` |

## Terms chosen while translating

| English | German | Note |
|---|---|---|
| X joins the city! | X zieht in die Stadt! | two lines when the name is long |
| I mastered 'X'! | Ich kann jetzt „X“! | X is the technique's name in `main.move_names` |
| Tamer level went up / down | Die Bändigerstufe ist gestiegen / gesunken! | |
| Care mistakes | Pflegefehler | |
| You have too much stuff | Du hast zu viele Sachen dabei. | |
| Too many items | Ich habe zu viele Items. | |
| Got X! (prize) | Heraus kam: X! | avoids the article's gender |
| Data / Vaccine / Virus Digimon | Daten- / Vakzin- / Virus-Digimon | |
| Finish gauge, finisher | Spezialleiste, Spezialtechnik («Spezial») | |
| Shop job | Ladenjob | profit: Gewinn, total profit: Gesamtgewinn, regular price: Normalpreis |
| Customer / mood / patience | Kunde / Laune / Geduld | the shop job |
| Card pack | Päckchen (UI), Pack (story) | |
| Trade | Tausch, tauschen | |
| Recycle shop | Recyclingladen | |
| Clinic / fortune teller / treasure hunter | Klinik / Wahrsager / Schatzsucher | |
| Training: X | `{color:5}Training: X {color:1}` | the training titles; «, verbessert» for the revised ones |
| Minimap / island map | Minikarte / Inselkarte | |
| Interface / tab | Oberfläche / Reiter | the options tab says «Anzeige»: «Oberfläche» does not fit it |
| Improvement / as the original | Verbesserung / Wie das Original | the options |
| Save slot / save file | Platz / Spielstand | |
| Room code / rival / series | Raumcode / Rivale / Serie | co-op and versus |
| Stat letters over fighters | ANG / VER / TMP | |
| Discount | Rabatt | |
| Fishing float / line / hook | Pose / Schnur / Haken | |
| Curling stone / power gauge | Curlingstein / Kraftanzeige | |
| Keyboard keys | Eingabe, Rücktaste, Leertaste, Strg, Umschalt, Entf, Pos1, Bild auf / ab | the prompts |
| Pad buttons | Kreuz, Kreis, Quadrat, Dreieck; Menü, Ansicht | |

Prices in menus are figures («Verband: 400 Bits»); the amounts the characters say are words, like the original
(«fünfzehnhundert Bits»), unless a line would not fit.
