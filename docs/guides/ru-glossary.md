# Russian translation glossary

The terms that repeat in the Russian translation (code `ru`), so that they always say the same thing. It covers both
the engine's texts (`content/packs/game/localization/ru/`) and the original's texts
(`content/packs/ru-translation/text/ru/`, the same flow as Spanish: [Translating the original's texts](translate.md)).
A new term that repeats is added here with the translation that first uses it. The Spanish decisions are in the
[Spanish glossary](spanish-glossary.md); Russian follows them unless this page says otherwise.

## Decisions

1. Standard modern Russian, friendly and simple, like a children's adventure game: short sentences, everyday words,
   no slang that dates quickly.
2. The player is addressed informally with **ты** («Хочешь сохранить?»); characters talk to each other with *ты* as
   well. Menu commands use the imperative of *ты* («Выбери», «Нажми») or a short infinitive/noun where the original
   is a bare label («Сохранить», «Отмена»).
3. Item names, technique names, places, medals, tournaments and music titles are translated, with words of our own.
   No name is taken from any other translation, dub or game.
4. Digimon names and the names of the characters stay in English, in Latin letters, typos included (they are
   `kept_english`). They are not declined: the sentence is built so that the name stays in its base form («у
   Jijimon», «дом Jijimon», «Дигиэволюция в Greymon!»). The common noun *Digimon* is written in Cyrillic and declined:
   **Дигимон**, Дигимона, Дигимоны. Item and place names are ordinary Russian words and are declined in a sentence
   («три куска {color:6}Мяса{color:1}», «в Городе Файл»); where a short item name from the list would read badly
   declined, the sentence is built around its base form.
5. `HP`, `MP` and `Bits` stay in Latin letters, as in the original, and `Bits` is not declined («300 Bits»).
   `Overdell` stays as a proper name. Numbers keep the original's figures.
6. The stages: **Малыш, Ученик, Новичок, Чемпион, Ультра** (Fresh, In-Training, Rookie, Champion, Ultimate). With the
   `canonical_stages` option: **Дигияйцо, Малыш I, Малыш II, Ребёнок, Взрослый, Совершенный, Высший, Сверхвысший**.
7. When the English is broken, the intent is translated; every control code and placeholder stays.
8. Typography: «ёлочки» quotes where the text quotes, `…` written as three dots like the original, a hyphen for
   compounds. The letter **ё** is always written (ещё, всё, её). Menu titles that the original writes in capitals stay
   in capitals («НОВАЯ ИГРА»).
9. Fit: Russian runs about a fifth longer than English and has long words. Item names fit in about 12 characters,
   technique names in about 16, story lines in 30, map names are centred in 24. When a phrase does not fit, it is
   shortened the way a Russian localiser would: a shorter synonym, a common abbreviation with a dot («восст.», «макс.»,
   «Атк»), never a word cut in the middle.
10. Plurals: the engine follows CLDR, so Russian writes three forms, `{n:one|few|many}`
    («{days} {days:день|дня|дней}», «{amount} {amount:жизнь|жизни|жизней}»). Bits never changes form
    («Выиграно {amount} Bits»). The original's texts, which have no plural tag, use a colon or a figure instead
    («Мясо: 300 Bits», «очков: {shopvar}»).
11. Grammatical gender after a placeholder or a Digimon name: neutral wording that does not need a gender («у
    {partner} рана», «Дигимон заболел») or the masculine, which agrees with *Дигимон*.

## Game and raising

| English | Russian | Note |
|---|---|---|
| Digimon | Дигимон | declined; Digimon names stay in Latin |
| Partner | напарник | |
| Tamer | укротитель | «уровень укротителя» |
| Digivolve, digivolution | дигиэволюционировать, дигиэволюция | «Дигиэволюция в `species_i`!» |
| Digi-Egg | Дигияйцо | |
| Raise | растить, воспитание | |
| Full (hunger) | сытость, насыщать | «Немного насыщает Дигимона» |
| Tiredness | усталость | |
| Discipline (Disc.) | дисциплина (дисц.) | |
| Happiness | счастье | |
| Weight | вес | |
| Life / life span | жизнь | |
| Virus (Vir.) | вирус (вир.) | |
| Sick, sickness | болен, болезнь | «заболел» |
| Wound | рана | |
| Status errors | недуги | «Лечит недуги» |
| Coma | кома | |
| Poison / Confusion / Stun / Flat | яд / смятение / оглушение / сплющен | abbreviated «яд», «смят», «оглу», «плющ» |
| Potty | туалет | «сходить в туалет» |
| Rest / sleep | отдых, сон | |
| Training | тренировка | |
| Medal | медаль | |
| Card | карта | the Digimon cards |
| Bits | Bits | the currency, Latin, not declined |
| Evolution tree | древо эволюции | |

## Attributes

| English | Russian | Abbreviation |
|---|---|---|
| HP | HP | |
| MP | MP | |
| Offense, Off. Power | Атака | Атк |
| Defense, Def. Power | Защита | Защ |
| Speed | Скорость | Скор |
| Brains | Интеллект | Инт |
| Abilities, skills | способности | |

## Battle and techniques

| English | Russian | Note |
|---|---|---|
| Technique, Tech | приём | the attacks; «Освоено приёмов» |
| Mastered / Unmastered | освоен / не освоен | |
| Set technique | назначить приём | |
| Battle | бой | «в бою» |
| Run | Бежать / Беги! | order / shout |
| Attack | Атака / Атакуй! | |
| Your Call, Auto | Сам решай, Авто | |
| Moderate | Умеренно / Спокойнее! | |
| Distance | Дистанция / Держи дистанцию! | |
| Defensive | Защита / Защищайся! | |
| Change | Сменить / Меняй! | |
| Change target | Смени цель! | |
| Go all the way, Give it all you got | Изо всех сил! | |
| Hang in there, Put up with it | Держись! | |
| Technique mastered | приём освоен | |
| Finisher (Final technique) | коронный приём | |

## Items

| English | Russian | Note |
|---|---|---|
| Item | предмет | |
| Use / Move / Sort / Drop | Использовать / Переместить / Сортировать / Выбросить | inventory menu |
| Recovery | восстановление, восст. | |
| Floppy, Disk | дискета, диск | |
| Chip | чип | |
| Mushroom (mushrm) | гриб | |
| Berry | ягода | |
| Banana | банан | |
| Meat | мясо | |
| Bait | наживка | fishing |
| Fishing rod | удочка | |
| Key | ключ | |

Item names are in `text/ru/main.yaml` (`item_names.*`); every text that names an item uses exactly those names.

## Places

| English | Russian |
|---|---|
| File City | Город Файл |
| Mt. | гора (Г. if it does not fit) |
| Native Forest | Родной лес |
| Drill Tunnel | Бур-туннель |
| Great Canyon | Великий каньон |
| Freezeland | Ледяной край |
| Gear Savanna | Саванна Шестерён |
| Toy Town | Город Игрушек |
| Factorial Town | Город Факториал |
| Beetle Land | Земля Жуков |
| Trash Mountain | Мусорная гора |
| Grey Lord's Mansion | Особняк Серого Лорда |
| Misty Trees | Туманная роща |
| Ancient Dino Region | Древний Дино-край |
| Ogre Fortress | Крепость Огра |
| Overdell | Overdell |

Map names are in `text/ru/main.yaml` (`map_names.*`), centred in 24 characters with spaces, like the original's.

## Tournaments

| English | Russian |
|---|---|
| Tournament | турнир |
| Grade D… | Класс D… |
| Cup | Кубок |
| Version 1 Cup | Кубок Версии 1 |
| Fire / Grapple / Thunder Wind / Cool / Nature / Metalic / Filth | Кубок Огня / Борьбы / Грома и Ветра / Холода / Природы / Металла / Грязи |
| Dino / Wing / Animal / Human / Beetle | Кубок Дино / Крыльев / Зверей / Людей / Жуков |

## Menus and system

| English | Russian |
|---|---|
| New game / Continue | Новая игра / Продолжить |
| Save / Load / Delete / Copy | Сохранить / Загрузить / Удалить / Копировать |
| Data file | файл |
| Format | форматировать |
| Memory Card | карта памяти |
| Slot | слот |
| Register, registration | зарегистрировать, регистрация |
| Select / Cancel | Выбрать / Отмена |
| Yes / No | Да / Нет |
| Pause | Пауза |
| Name | Имя |
| Player | Игрок |
| Options | Настройки |
| Skin (UI) | оформление |
| Room (online) | комната |
| Host / Guest | хост / гость |
| Co-op | кооператив |
| Directional Pad | крестовина |
| Press X | Нажми X |

## Story

The dialogue of the event scripts (`text/ru/story_<n>.yaml`). The speaker's line (`{color:7}<name> {color:1}\n`) is
copied the same and the characters' names stay in English.

| English | Russian | Note |
|---|---|---|
| Digivice | Дигивайс | |
| Merit, Merit points | заслуги, очки заслуг | the card shop |
| Card, Trading card | карта, коллекционная карта | |
| Tournament Schedule | Расписание турниров | |
| Enter (a tournament), Register | записаться | |
| Qualifications | Условия | |
| Battle Royal | королевская битва | |
| X joins the city! | X теперь живёт в городе! | on two lines if it does not fit |
| Begin / Explain / Quit | Начать / Объяснить / Выйти | the training sessions |
| Explain again / Bonus Try | Ещё раз объяснить / Доп. попытка | |
| Slot game | игровой автомат | |
| Vending Machine | торговый автомат | prices in figures: «Мясо: 300 Bits» |
| Elevator | лифт | |
| Ogre mode | Режим Огра | |
| Dimensional Key Chain | Брелок измерений | |
| Lake Guardian | Страж озера | |
| Glacial / Speedy Time Zone | Ледниковая / Быстрая эпоха | |
| Market manager | Управляющий рынком | |
| Come again. | Заходи ещё! | the shops |
| See ya (option) | Пока | |
| X came / joins the Arena | Пришёл X… / X теперь на Арене! | |
| I got X | Я получил X! / Получено: X! | |
| You have too many items | У тебя слишком много вещей. | |
| Not enough bits | Тебе не хватает Bits. | |
| Buy / Don't buy | Купить / Не покупать | |
| Register / Don't register | Записаться / Не записываться | tournaments |
| Explain / Don't explain | Объяснить / Не объяснять | |
| Training: X (revision) | Тренировка: X (улучшенная) | |
| Stat gains | Атака увеличена на N! / HP увеличено на N! | the restaurant, the training |
| Item Keeper, Item Bank | Склад предметов | as the map name |
| File Island | остров Файл | |
| Net World / Digimon World | Сетевой мир / Мир Дигимонов | |
| Iceberg Beach | Пляж Айсбергов | |
| Treasure Hunter | кладоискатель | |
| Card shop, card pack | магазин карт Дигимонов, набор | |
| Finishing technique, finish gauge | коронный приём, шкала коронного приёма | |
| Care mistakes | ошибки ухода | |
| Ogre Mode: ON / OFF | Режим Огра: ВКЛ / ВЫКЛ | |
| Tournament boards | «- Класс D -», «Один на один, три раунда, восемь участников», «Условия: нет.» | the same text in every table |

## The engine's texts

| English | Russian | Note |
|---|---|---|
| Versus (online battle) | Онлайн-бой | «РЕЖИМ VS» on the title |
| Multiplayer | Мультиплеер | |
| Trade (co-op) | обмен, обменяться | |
| Series / rival | серия / соперник | versus |
| Relay | ретранслятор | |
| Ready / Not ready | Готов / Не готов | |
| Public / private room | открытая / закрытая комната | |
| Recruit, join the city | переедет в город, переезд | «Шкала переезда» |
| Shop job / customer / tally | Работа в магазине / покупатель / выручка | |
| Off | Выкл. | |
| Title screen | главное меню | |
| Minimap | Миникарта | |
| Pad, D-pad | геймпад, крестовина | |
| Skins | Современное / Оригинальное / Пиксель-арт | neuter, agrees with *оформление* |
| Mega (stage) | Мега | |
| Tournament champion | Победитель | not *Чемпион*, which is the stage |
| Steel Heart / Blood Moon | Стальное сердце / Кровавая луна | dwe's own items (`text/ru/game.yaml`) |
