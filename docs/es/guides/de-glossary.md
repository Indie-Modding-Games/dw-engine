# Glosario de la traducción al alemán

Los términos que se repiten en la traducción al alemán (código `de`), para que digan siempre lo mismo. Cubre tanto los
textos del motor (`content/packs/game/localization/de/`) como los textos del original
(`content/packs/de-translation/text/de/`, el mismo flujo que el español: [Traducir los textos del original](translate.md)).
Un término nuevo que se repite se añade aquí con la traducción que lo usa por primera vez. Las decisiones del español
están en el [glosario del español](spanish-glossary.md); el alemán las sigue salvo que esta página diga otra cosa.

## Decisiones

1. Alemán estándar (Alemania), moderno y cercano, como un juego de aventuras para niños: frases cortas, palabras de
   todos los días.
2. Al jugador se le trata de tú con **du** en minúscula («Willst du speichern?»); los personajes también se hablan
   de *du* entre ellos, salvo donde el inglés es claramente formal (un dependiente que saluda a un cliente igual dice
   *du*: la isla es un lugar pequeño y amistoso).
3. Los nombres de objetos, técnicas, lugares, medallas, torneos y títulos de música se traducen, con palabras propias.
   Ningún nombre se toma de otra traducción, doblaje o juego.
4. Los nombres de los Digimon y de los personajes se quedan en inglés, letra por letra, erratas incluidas. El alemán
   solo los declina con la *s* del genitivo donde la frase lo necesita («Jijimons Haus», «Leomons Ahnen»).
5. `HP`, `MP` y `Bits` se quedan. Los números mantienen las cifras del original.
6. Las etapas: **Baby, Training, Neuling, Champion, Ultra** (Fresh, In-Training, Rookie, Champion, Ultimate). Con la
   opción `canonical_stages`: **Digi-Ei, Baby I, Baby II, Kind, Erwachsen, Perfekt, Ultimativ, Super-Ultimativ**.
7. Cuando el inglés está roto, se traduce la intención; se conservan todos los códigos de control y marcadores.
8. Tipografía: comillas alemanas „…“ donde el texto cita, `…` como tres puntos igual que el original, `-` para los
   guiones. Los sustantivos van con mayúscula como exige el alemán; los títulos de menú que el original escribe en
   mayúsculas se quedan en mayúsculas («NEUES SPIEL»).
9. Espacio: el alemán ocupa cerca de un tercio más que el inglés. Los nombres de objeto caben en unos 13 caracteres,
   las líneas de la historia en 30 y los nombres de mapa van centrados en 24. Cuando una frase no cabe, se acorta como
   lo haría un localizador alemán (un sinónimo más corto, una palabra compuesta, una abreviatura común con punto:
   «Ang.», «Vert.»), nunca cortada a mitad de palabra.

## Juego y crianza

| Inglés | Alemán | Nota |
|---|---|---|
| Digimon | Digimon | invariable en plural |
| Partner | Partner | |
| Tamer | Bändiger | «Bändigerstufe» (nivel de domador) |
| Digivolve, digivolution | digievolvieren, Digievolution | «Digievolviere zu `species_i`!» |
| Digi-Egg | Digi-Ei | |
| Raise | aufziehen, Aufzucht | |
| Full (hunger) | satt | «Macht das Digimon etwas satt» |
| Hunger | Hunger | |
| Tiredness | Müdigkeit | |
| Discipline (Disc.) | Disziplin (Diszi.) | |
| Happiness | Glück | |
| Weight | Gewicht | corto: «Gew.» (página del compañero) |
| Life, life span | Lebensdauer | corto: «Lebensd.» (página del compañero) |
| Virus (Vir.) | Virus (Vir.) | |
| Sick, sickness | krank, Krankheit | |
| Wound, injured | Wunde, verletzt | |
| Status errors | Statusprobleme | |
| Coma | Koma | |
| Poison / Confusion / Stun / Flat | Gift / Verwirrung / Betäubung / Platt | abreviado: «Gift», «Verw.», «Betäub.», «Platt» |
| Potty | Klo | «aufs Klo gehen» |
| Rest, sleep | ausruhen, schlafen | |
| Training | Training | |
| Praise / Scold | Loben / Schimpfen | |
| Medal | Medaille | |
| Card | Karte | las cartas Digimon |
| Bits | Bits | la moneda |

## Atributos

| Inglés | Alemán | Abreviatura |
|---|---|---|
| HP | HP | |
| MP | MP | |
| Offense, Off. Power | Angriff | Ang. |
| Defense, Def. Power | Verteidigung | Vert. |
| Speed | Tempo | Tmp. |
| Brains | Intelligenz | Int. |
| Abilities, skills | Fähigkeiten, Werte | |

## Batalla y técnicas

| Inglés | Alemán | Nota |
|---|---|---|
| Technique, Tech | Technik, Tech. | |
| Power / Range / Effect (technique columns) | KRAFT / RW / EFF | |
| Mastered / Unmastered | gemeistert / nicht gemeistert | |
| Set technique | Technik festlegen | |
| Battle | Kampf | |
| Run | Fliehen / Lauf weg! | orden / grito |
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

Los nombres de las técnicas son palabras alemanas propias que describen el ataque («Feuerturm», «Eisnadel»), en una
palabra compuesta o en dos palabras, de hasta unos 16 caracteres.

## Objetos

| Inglés | Alemán | Nota |
|---|---|---|
| Item | Item | «Items» en los menús; en los diálogos también «Gegenstand» |
| Use / Move / Sort / Drop | Benutzen / Verschieben / Sortieren / Wegwerfen | menú del inventario |
| Recovery | Heilung | «Heilung kl./mit./gr.» |
| Floppy, Disk | Diskette, Disk | |
| Chip | Chip | |
| Mushroom | Pilz | |
| Berry | Beere | |
| Banana | Banane | |
| Meat | Fleisch | |
| Bait | Köder | pesca |
| Fishing rod | Angel | |
| Key | Schlüssel | |
| Item bank, Item Keeper | Itemlager | |

La lista completa de nombres de objeto es `main.item_names` en `text/de/main.yaml`; cualquier otro texto que nombre un
objeto usa ese mismo nombre.

## Lugares

| Inglés | Alemán |
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
| Green Gym | Grüner Trainingsplatz («Trainingsplatz» en corto) |
| Item Keeper, item bank | Itemlager |
| File Island | File-Insel |
| Digimon World (the place) | Digimon-Welt |
| Net World | Netzwelt |
| Iceberg Beach | Eisbergstrand |
| Training Camp | Trainingslager |
| Ancient Dino Tomb | Urzeit-Dinograb |

La lista completa es `main.map_names` en `text/de/main.yaml`, centrada en 24 caracteres con espacios como el original.

## Torneos

| Inglés | Alemán |
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

Los nombres de las copas se unen con guiones: «Feuer-Pokal», «Version-1-Pokal». Títulos del nivel de domador: Amateur,
Lehrling, Anfänger, Erfahren, Profi, Veteran, Elite, Super, Meister, Genie, Legende («Neuling» se reserva para la
etapa).

## Menús y sistema

| Inglés | Alemán |
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
| Fixed in {variant} (opción fijada) | Fest: {variant} |
| Open to new players (co-op) | Offen für neue Spieler |
| Player (nombre en línea por defecto) | Spieler |
| Bits (etiqueta de la moneda en los menús) | Bits |
| Naming page of accented letters | ÄÖÜ (Ä Ö Ü ß) |
| The game's log (donde van los detalles de un error) | Spielprotokoll |
| Unknown variant (modo de una partida que no está instalado) | Unbekannte Variante |
| Key caps of the classic prompts (`prompt.cap.*`) | ESC, @tab, ENTF, EINFG, POS1, ENDE, BILDAUF, BILDAB, @caps_lock, @shift, STRG, ALT, ALTGR |

## Historia

Los diálogos de los scripts de eventos (`text/de/story_<n>.yaml`). La línea del que habla (`{color:7}<name> {color:1}\n`)
se copia igual y los nombres de los personajes se quedan en inglés. Las líneas tienen como máximo 30 caracteres (las
etiquetas no cuentan) y un texto tiene como máximo 4 líneas por cuadro, como el inglés.

| Inglés | Alemán | Nota |
|---|---|---|
| Digivice | Digivice | |
| Merit, Merit points | Verdienste, Verdienstpunkte | la tienda de cartas |
| Card, Trading card | Karte, Sammelkarte | |
| Tournament Schedule | Turnierplan | |
| Enter (a tournament), Register | sich anmelden | |
| Qualifications | Bedingungen | |
| X joins the city! | X zieht in die Stadt! | en dos líneas si no cabe |
| Begin / Explain / Quit | Starten / Erklären / Beenden | las sesiones de entrenamiento |
| Explain again / Bonus Try | Nochmal erklären / Bonusversuch | |
| Slot game | Spielautomat | |
| Vending Machine | Automat | precios en cifras: «Fleisch: 300 Bits» |
| Elevator | Aufzug | |
| Ogre mode | Oger-Modus | |
| Dimensional Key Chain | Dimensionsschlüsselbund | |
| Lake Guardian | Wächter des Sees | |
| Glacial / Speedy Time Zone | Eiszeit- / Tempo-Zeitzone | |
| Market manager | Marktleiter | |
| Come again. | Komm bald wieder. | las tiendas |
| See ya (option) | Tschüss | |
| main character (unnamed player) | Hauptfigur | `main.digimon_names.0000` |

## Términos elegidos al traducir

| Inglés | Alemán | Nota |
|---|---|---|
| X joins the city! | X zieht in die Stadt! | dos líneas cuando el nombre es largo |
| I mastered 'X'! | Ich kann jetzt „X“! | X es el nombre de la técnica en `main.move_names` |
| Tamer level went up / down | Die Bändigerstufe ist gestiegen / gesunken! | |
| Care mistakes | Pflegefehler | |
| You have too much stuff | Du hast zu viele Sachen dabei. | |
| Too many items | Ich habe zu viele Items. | |
| Got X! (prize) | Heraus kam: X! | evita el género del artículo |
| Data / Vaccine / Virus Digimon | Daten- / Vakzin- / Virus-Digimon | |
| Finish gauge, finisher | Spezialleiste, Spezialtechnik («Spezial») | |
| Shop job | Ladenjob | ganancia: Gewinn, ganancia total: Gesamtgewinn, precio normal: Normalpreis |
| Customer / mood / patience | Kunde / Laune / Geduld | el trabajo en la tienda |
| Card pack | Päckchen (UI), Pack (story) | |
| Trade | Tausch, tauschen | |
| Recycle shop | Recyclingladen | |
| Clinic / fortune teller / treasure hunter | Klinik / Wahrsager / Schatzsucher | |
| Training: X | `{color:5}Training: X {color:1}` | los títulos de los entrenamientos; «, verbessert» para los revisados |
| Minimap / island map | Minikarte / Inselkarte | |
| Interface / tab | Oberfläche / Reiter | la pestaña de opciones dice «Anzeige»: «Oberfläche» no cabe |
| Improvement / as the original | Verbesserung / Wie das Original | las opciones |
| Save slot / save file | Platz / Spielstand | |
| Room code / rival / series | Raumcode / Rivale / Serie | cooperativo y versus |
| Stat letters over fighters | ANG / VER / TMP | |
| Discount | Rabatt | |
| Fishing float / line / hook | Pose / Schnur / Haken | |
| Curling stone / power gauge | Curlingstein / Kraftanzeige | |
| Keyboard keys | Eingabe, Rücktaste, Leertaste, Strg, Umschalt, Entf, Pos1, Bild auf / ab | los avisos |
| Pad buttons | Kreuz, Kreis, Quadrat, Dreieck; Menü, Ansicht | |

Los precios en los menús van en cifras («Verband: 400 Bits»); las cantidades que dicen los personajes van en palabras,
como el original («fünfzehnhundert Bits»), salvo que una línea no quepa.
