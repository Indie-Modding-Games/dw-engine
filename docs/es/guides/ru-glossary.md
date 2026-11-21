# Glosario de la traducción al ruso

Los términos que se repiten en la traducción al ruso (código `ru`), para que digan siempre lo mismo. Cubre los textos
del motor (`content/packs/game/localization/ru/`) y los del original (`content/packs/ru-translation/text/ru/`, con el
mismo flujo que el español: [Traducir los textos del original](translate.md)). Un término nuevo que se repite se
agrega aquí con la traducción que lo usa primero. Las decisiones del español están en el
[glosario del español](spanish-glossary.md); el ruso las sigue salvo que esta página diga otra cosa.

## Decisiones

1. Ruso estándar moderno, amable y sencillo, como un juego de aventuras para niños: frases cortas, palabras de todos
   los días, sin jerga que envejezca rápido.
2. Al jugador se le habla de **ты** («Хочешь сохранить?»); los personajes también se tutean. Los comandos de menú
   usan el imperativo de *ты* («Выбери», «Нажми») o un infinitivo o sustantivo corto donde el original es una
   etiqueta suelta («Сохранить», «Отмена»).
3. Los nombres de objetos, técnicas, lugares, medallas, torneos y temas de música se traducen, con palabras propias.
   Ningún nombre se toma de otra traducción, doblaje o juego.
4. Los nombres de los Digimon y de los personajes quedan en inglés, en letras latinas, con sus erratas (son
   `kept_english`). No se declinan: la frase se arma para que el nombre quede en su forma base («у Jijimon», «дом
   Jijimon», «Дигиэволюция в Greymon!»). El sustantivo común *Digimon* va en cirílico y se declina: **Дигимон**,
   Дигимона, Дигимоны. Los nombres de objetos y lugares son palabras rusas normales y se declinan en la frase («три
   куска {color:6}Мяса{color:1}», «в Городе Файл»); cuando un nombre corto de la lista se leería mal declinado, la
   frase se arma alrededor de su forma base.
5. `HP`, `MP` y `Bits` quedan en letras latinas, como en el original, y `Bits` no se declina («300 Bits»).
   `Overdell` queda como nombre propio. Los números conservan las cifras del original.
6. Las etapas: **Малыш, Ученик, Новичок, Чемпион, Ультра** (Bebé, Entrenamiento, Novato, Campeón, Ultra). Con la
   opción `canonical_stages`: **Дигияйцо, Малыш I, Малыш II, Ребёнок, Взрослый, Совершенный, Высший, Сверхвысший**.
7. Cuando el inglés está roto se traduce la intención; se conservan todos los códigos de control y marcadores.
8. Tipografía: comillas «ёлочки» donde el texto cita, `…` escrito como tres puntos como el original, guion en los
   compuestos. La letra **ё** se escribe siempre (ещё, всё, её). Los títulos de menú que el original escribe en
   mayúsculas quedan en mayúsculas («НОВАЯ ИГРА»).
9. Espacio: el ruso ocupa cerca de un quinto más que el inglés y tiene palabras largas. Los nombres de objetos caben
   en unos 12 caracteres, los de técnicas en unos 16, las líneas de la historia en 30 y los nombres de mapa van
   centrados en 24. Si una frase no cabe, se acorta como lo haría un localizador ruso: un sinónimo más corto, una
   abreviatura común con punto («восст.», «макс.», «Атк»), nunca una palabra cortada a la mitad.
10. Plurales: el motor sigue CLDR, así que el ruso escribe tres formas, `{n:one|few|many}`
    («{days} {days:день|дня|дней}», «{amount} {amount:жизнь|жизни|жизней}»). Bits no cambia de forma
    («Выиграно {amount} Bits»). Los textos del original, que no tienen etiqueta de plural, usan dos puntos o una
    cifra («Мясо: 300 Bits», «очков: {shopvar}»).
11. Género gramatical después de un marcador o de un nombre de Digimon: una forma neutra que no necesite género («у
    {partner} рана», «Дигимон заболел») o el masculino, que concuerda con *Дигимон*.
12. **Carteles de los mapas**: conservan el tamaño del cartel, así que un nombre largo se acorta solo ahí: el tablero
    pequeño de la tienda de objetos del pueblo dice «ЛАВКА».

## Juego y crianza

| Inglés | Ruso | Nota |
|---|---|---|
| Digimon | Дигимон | se declina; los nombres de Digimon quedan en latín |
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
| Poison / Confusion / Stun / Flat | яд / смятение / оглушение / сплющен | abreviado «яд», «смят», «оглу», «плющ» |
| Potty | туалет | «сходить в туалет» |
| Rest / sleep | отдых, сон | |
| Training | тренировка | |
| Medal | медаль | |
| Card | карта | las cartas de Digimon |
| Bits | Bits | la moneda, en latín, sin declinar |
| Evolution tree | древо эволюции | |

## Atributos

| Inglés | Ruso | Abreviatura |
|---|---|---|
| HP | HP | |
| MP | MP | |
| Offense, Off. Power | Атака | Атк |
| Defense, Def. Power | Защита | Защ |
| Speed | Скорость | Скор |
| Brains | Интеллект | Инт |
| Abilities, skills | способности | |

## Batalla y técnicas

| Inglés | Ruso | Nota |
|---|---|---|
| Technique, Tech | приём | los ataques; «Освоено приёмов» |
| Mastered / Unmastered | освоен / не освоен | |
| Set technique | назначить приём | |
| Battle | бой | «в бою» |
| Run | Бежать / Беги! | orden / grito |
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

## Objetos

| Inglés | Ruso | Nota |
|---|---|---|
| Item | предмет | |
| Use / Move / Sort / Drop | Использовать / Переместить / Сортировать / Выбросить | menú del inventario |
| Recovery | восстановление, восст. | |
| Floppy, Disk | дискета, диск | |
| Chip | чип | |
| Mushroom (mushrm) | гриб | |
| Berry | ягода | |
| Banana | банан | |
| Meat | мясо | |
| Bait | наживка | pesca |
| Fishing rod | удочка | |
| Key | ключ | |

Los nombres de objetos están en `text/ru/main.yaml` (`item_names.*`); todo texto que nombra un objeto usa exactamente esos nombres.

## Lugares

| Inglés | Ruso |
|---|---|
| File City | Город Файл |
| Mt. | гора (Г. si no cabe) |
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

Los nombres de mapa están en `text/ru/main.yaml` (`map_names.*`), centrados en 24 caracteres con espacios, como los del original.

## Torneos

| Inglés | Ruso |
|---|---|
| Tournament | турнир |
| Grade D… | Класс D… |
| Cup | Кубок |
| Version 1 Cup | Кубок Версии 1 |
| Fire / Grapple / Thunder Wind / Cool / Nature / Metalic / Filth | Кубок Огня / Борьбы / Грома и Ветра / Холода / Природы / Металла / Грязи |
| Dino / Wing / Animal / Human / Beetle | Кубок Дино / Крыльев / Зверей / Людей / Жуков |

## Menús y sistema

| Inglés | Ruso |
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

## Historia

El diálogo de los scripts de eventos (`text/ru/story_<n>.yaml`). La línea de quien habla (`{color:7}<nombre> {color:1}\n`)
se copia igual y los nombres de los personajes quedan en inglés.

| Inglés | Ruso | Nota |
|---|---|---|
| Digivice | Дигивайс | |
| Merit, Merit points | заслуги, очки заслуг | la tienda de cartas |
| Card, Trading card | карта, коллекционная карта | |
| Tournament Schedule | Расписание турниров | |
| Enter (a tournament), Register | записаться | |
| Qualifications | Условия | |
| Battle Royal | королевская битва | |
| X joins the city! | X теперь живёт в городе! | en dos líneas si no cabe |
| Begin / Explain / Quit | Начать / Объяснить / Выйти | las sesiones de entrenamiento |
| Explain again / Bonus Try | Ещё раз объяснить / Доп. попытка | |
| Slot game | игровой автомат | |
| Vending Machine | торговый автомат | precios en cifras: «Мясо: 300 Bits» |
| Elevator | лифт | |
| Ogre mode | Режим Огра | |
| Dimensional Key Chain | Брелок измерений | |
| Lake Guardian | Страж озера | |
| Glacial / Speedy Time Zone | Ледниковая / Быстрая эпоха | |
| Market manager | Управляющий рынком | |
| Come again. | Заходи ещё! | las tiendas |
| See ya (option) | Пока | |
| X came / joins the Arena | Пришёл X… / X теперь на Арене! | |
| I got X | Я получил X! / Получено: X! | |
| You have too many items | У тебя слишком много вещей. | |
| Not enough bits | Тебе не хватает Bits. | |
| Buy / Don't buy | Купить / Не покупать | |
| Register / Don't register | Записаться / Не записываться | torneos |
| Explain / Don't explain | Объяснить / Не объяснять | |
| Training: X (revision) | Тренировка: X (улучшенная) | |
| Stat gains | Атака увеличена на N! / HP увеличено на N! | el restaurante, el entrenamiento |
| Item Keeper, Item Bank | Склад предметов | como el nombre del mapa |
| File Island | остров Файл | |
| Net World / Digimon World | Сетевой мир / Мир Дигимонов | |
| Iceberg Beach | Пляж Айсбергов | |
| Treasure Hunter | кладоискатель | |
| Card shop, card pack | магазин карт Дигимонов, набор | |
| Finishing technique, finish gauge | коронный приём, шкала коронного приёма | |
| Care mistakes | ошибки ухода | |
| Ogre Mode: ON / OFF | Режим Огра: ВКЛ / ВЫКЛ | |
| Tournament boards | «- Класс D -», «Один на один, три раунда, восемь участников», «Условия: нет.» | el mismo texto en todas las tablas |

## Los textos del motor

| Inglés | Ruso | Nota |
|---|---|---|
| Versus (online battle) | Онлайн-бой | «РЕЖИМ VS» en la pantalla de título |
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
| Skins | Современное / Оригинальное / Пиксель-арт | neutro, concuerda con *оформление* |
| Mega (stage) | Мега | |
| Tournament champion | Победитель | no *Чемпион*, que es la etapa |
| Steel Heart / Blood Moon | Стальное сердце / Кровавая луна | objetos propios de dwe (`text/ru/game.yaml`) |
