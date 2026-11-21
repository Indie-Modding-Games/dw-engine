# Glosario de la traducción al japonés

Los términos que se repiten en la traducción al japonés (código `ja`), para que siempre digan lo mismo. Cubre tanto
los textos del motor (`content/packs/game/localization/ja/`) como los textos del original
(`content/packs/ja-translation/text/ja/`, el mismo flujo que el español:
[Traducir los textos del original](translate.md)). Un término nuevo que se repite se agrega aquí con la traducción
que lo usa primero. Las decisiones del español están en el [glosario del español](spanish-glossary.md); el japonés las
sigue salvo que esta página diga otra cosa.

## Decisiones

1. Japonés moderno estándar, cercano y simple, como un juego de aventuras para niños. Solo kanji de uso diario (sin
   kanji raros ni furigana); las palabras que un niño lee con facilidad van en kana («おなか», «ケガ», «しつけ»).
2. Traducción propia desde el inglés extraído, con el español como segunda referencia de intención. Ningún texto,
   nombre ni término se toma de la versión japonesa del juego, de otra traducción, doblaje o juego. Cuando la
   traducción llana del inglés coincide con una palabra japonesa común, se usa esa palabra.
3. Registro: el sistema (menús, avisos, pantallas de guardado) es cortés y neutro (です/ます: «セーブしますか？»).
   Los personajes hablan de manera informal, cada uno con su voz (Jijimon como anciano: わし, 〜じゃ, 〜のう; los
   tenderos, amables y corteses: «いらっしゃい！»). Al jugador se le habla sin pronombres donde el japonés lo permite, y
   con «きみ» cuando hace falta uno: el «tú» informal que pide el tuteo del español.
4. Los nombres de los Digimon y de los personajes se escriben en katakana, con una lectura propia de la grafía
   inglesa, no con los nombres de la versión japonesa (decisión 2): «アグモン», «ジジモン», «マイオティスモン»,
   «センタルモン». El japonés, como el coreano, traduce el bloque `digimon_names` (`own_names` en
   `scripts/translation.sh`); las líneas del hablante y cada nombre dentro de un texto usan el mismo katakana. La
   palabra genérica «Digimon» es «デジモン».
5. `HP` y `MP` quedan. La moneda es «ビット» en todas partes, también la etiqueta de los menús (`common.bits`; la
   etiqueta `Bits` del original sigue conservada en el lock y ya no se muestra). Los números van en dígitos de ancho
   medio con las cifras del original.
6. Las etapas, con nombres propios a partir de las etapas en inglés: **フレッシュ, 見習い, ルーキー, チャンピオン,
   アルティメット** (Fresh, In-Training, Rookie, Champion, Ultimate); con las etapas canónicas: **デジエッグ,
   フレッシュ, 見習い, ルーキー, チャンピオン, アルティメット, メガ, ウルトラ**. No se usan los términos japoneses de
   la franquicia (decisión 2).
7. Los nombres de las técnicas se traducen por su significado, con palabras propias (compuestos de kanji y préstamos
   comunes: «炎の塔», «氷の針»), nunca con una copia en katakana de un nombre de alguna serie.
8. Cuando el inglés está roto se traduce la intención; se conservan todos los códigos de control y marcadores. El
   japonés no tiene plural: todas las formas de `{n:one|other}` llevan el mismo japonés (`{days}{days:日|日}`), o
   simplemente `{n}日`.
9. Tipografía: «、。！？» de ancho completo y «「」» para las citas, «…» para el "..." del inglés, «・» para unir
   palabras cortas, dígitos y letras latinas de ancho medio, «〜» para los sonidos alargados. Sin espacios entre
   palabras japonesas; una línea nunca empieza con «、。！？」ー» ni con un kana pequeño.
10. Espacio: un carácter CJK ocupa dos columnas. Las líneas de la historia tienen como máximo 30 columnas (15 kana o
    kanji; letras latinas y dígitos cuentan uno), no más líneas que el inglés y nunca más de 4 por cuadro. Los nombres
    de objetos caben en unos 7 caracteres, las descripciones en unos 14, las técnicas en unos 9 y los nombres de mapa
    van centrados en 24 columnas.

## Juego y crianza

| Inglés | Japonés | Nota |
|---|---|---|
| Digimon (generic) | デジモン | cada nombre: `main.digimon_names` |
| Partner | パートナー | |
| Tamer | テイマー | |
| Digivolve, digivolution | 進化する, 進化 | «グレイモンに進化！» |
| Digi-Egg | デジエッグ | |
| Raise | 育てる | |
| Full, hunger | おなかがいっぱい, おなかがすく | «おなかを少し満たす» |
| Tiredness | 疲れ | |
| Discipline | しつけ | |
| Happiness | 機嫌 | |
| Weight | 体重 | |
| Life, life span | 寿命 | |
| Sick, sickness | 病気 | |
| Wound, injured | ケガ | |
| Status errors | 状態異常 | |
| Coma | 気絶 | |
| Poison / Confusion / Stun / Flat | 毒 / 混乱 / マヒ / ペシャンコ | |
| Potty | トイレ | |
| Rest, sleep | 休む, ねる | |
| Training, Gym | トレーニング, ジム | menús: はじめる / 説明 / やめる |
| Care mistakes | お世話ミス | |
| Lives (partner lives) | ライフ | |
| Evolution chart | 進化図 | |
| Digivolution items | 進化アイテム | |
| Data / Vaccine / Virus type | データ種 / ワクチン種 / ウィルス種 | |
| Egg (choosing an egg) | タマゴ | デジエッグ para la etapa y el «Digitama» del inglés |
| Praise / Scold | ほめる / しかる | |
| Medal | メダル | |
| Card | カード | |
| Bits | ビット | |
| Digivice | デジヴァイス | |

## Atributos

| Inglés | Japonés | Corto |
|---|---|---|
| HP / MP | HP / MP | |
| Offense, Off. Power | 攻撃力 | 攻撃, 攻 |
| Defense, Def. Power | 防御力 | 防御, 防 |
| Speed | 素早さ | 速 |
| Brains | 賢さ | 賢 |
| Abilities | 能力 | |

## Batalla y técnicas

| Inglés | Japonés | Nota |
|---|---|---|
| Battle | バトル | |
| Technique | 技 | |
| Finishing move | 必殺技 | |
| Mastered | 習得 | |
| Enemy | 敵 | |
| Run (order) | 逃げる / 逃げろ！ | menú / grito |
| Attack | 攻撃 / いけ！ | |
| Your Call | おまかせ | |
| Moderate | ほどほど / ほどほどに！ | |
| Distance | 距離を取る / 離れろ！ | |
| Defensive | 守る / 守れ！ | |
| Change target | ねらいを変えろ！ | |
| Go all the way | 全力だ！ | |
| Hang in there | がんばれ！ | |
| Finish gauge | 必殺ゲージ | |
| Change (order) | 交代 | |

## Objetos

| Inglés | Japonés |
|---|---|
| Item | アイテム |
| Item Keeper | アイテム預かり所 |
| Item Shop | 道具屋 |
| Recovery (sm./med./lrg./sup.) | 回復(小/中/大/超) |
| Floppy, Disk, Chip | フロッピー, ディスク, チップ |
| Meat | 肉 |
| Mushroom | キノコ |
| Berry | ベリー |
| Fishing rod | 釣りざお |
| Bait | エサ |
| Key | カギ |
| Technique names | `main.move_names`: «炎の塔», «水の魔法», «大氷結», «虫の大群» |

La lista completa es `main.item_names` en `text/ja/main.yaml`; todo texto que nombra un objeto usa ese mismo nombre.

## Lugares

| Inglés | Japonés |
|---|---|
| File Island | ファイル島 |
| File City | ファイルシティ |
| Native Forest | 原生林 |
| Mt. Panorama | パノラマ山 |
| Drill Tunnel | ドリルトンネル |
| Great Canyon | 大渓谷 |
| Ogre Fortress | オーガの砦 |
| Grey Lord's Mansion | グレイロードの館 |
| Gear Savanna | ギアサバンナ |
| Freezeland | フリーズランド |
| Ancient Dino Region | 古代恐竜地帯 |
| Misty Trees | 霧の森 |
| Geko Swamp | ゲコ沼 |
| Volume Villa | ボリューム村 |
| Toy Town | おもちゃの町 |
| Factorial Town | ファクトリアルタウン |
| Trash Mountain | ゴミの山 |
| Beetle Land | ビートルランド |
| Mt. Infinity | 無限山 |
| Back Dimension | 裏次元 |
| Overdell | Overdell (se conserva) / オーバーデル dentro de otros nombres |
| Jijimon's house | ジジモンの家 |
| Green Gym | グリーンジム |
| Centar Clinic | センタル診療所 |
| Birdra Transport | バードラ便 |

La lista completa es `main.map_names`, centrada en 24 columnas (un carácter CJK cuenta dos).

## Torneos

| Inglés | Japonés |
|---|---|
| Tournament | 大会 |
| Grade D… | Dクラス… |
| Cup | カップ |
| Version 1 Cup | バージョン1カップ |
| Fire / Grapple / Thunder Wind / Cool / Nature / Metalic / Filth | 炎 / 格闘 / 雷風 / 冷気 / 自然 / メタル / よごれ |
| Dino / Wing / Animal / Human / Beetle | 恐竜 / つばさ / 動物 / 人型 / 甲虫 |
| Battle Royal | バトルロイヤル |
| Arena | アリーナ |
| Qualifications | 出場条件 («出場条件：なし。») |
| Victory prize, second place, participation award | 優勝賞金 / 優勝賞品, 準優勝賞金 / 準優勝賞品, 参加賞 |
| Insectoid Warrior Tournament | 昆虫戦士大会 |

Títulos de domador: アマチュア, 新米, 初心者, 中級者, 上級者, ベテラン, 一流, 超一流, 達人, 天才, 伝説.

## Menús y sistema

| Inglés | Japonés |
|---|---|
| New game / Continue | はじめから / つづきから |
| Save / Load / Delete / Copy | セーブ / ロード / 削除 / コピー |
| Data file | データ |
| Memory Card | メモリーカード |
| Slot | スロット |
| Select / Cancel / Back | 決定 / キャンセル / もどる |
| Yes / No | はい / いいえ |
| Pause / Options | ポーズ / オプション |
| Name / Player / Level | 名前 / プレイヤー / レベル |
| Press X | Xを押してください |
| Skin (UI look) | スキン |
| Co-op / Versus / Room / Host / Guest | 協力プレイ / 対戦 / ルーム / ホスト / ゲスト |
| Game mode / variant | ゲームモード / バージョン |
| Classic Enhanced / Folder Mode | クラシック強化版 / Folderモード |
| Fixed in {variant} (opción fijada) | {variant}では固定 |
| Open to new players (co-op) | 新しいプレイヤーの参加 |
| Player (nombre en línea por defecto) | プレイヤー |
| Bits (etiqueta de la moneda en los menús) | ビット (common.bits; la etiqueta extraída se deja como está) |

## Historia

| Inglés | Japonés | Nota |
|---|---|---|
| X joins the city! | Xが町の仲間になった！ | |
| Merit points | 功績ポイント | la tienda de cartas |
| Tournament Schedule | 大会スケジュール | |
| Enter, register | エントリーする | |
| Vending Machine | 自動販売機 | «肉：300ビット» |
| Elevator | エレベーター | |
| Lake Guardian | 湖の守り神 | |
| Come again. | また来てね。 | las tiendas |
| See ya (option) | さよなら | |
| Dimensional Key Chain | 次元キーホルダー | |
| Glacial / Speedy Time Zone | 氷河ゾーン / スピードゾーン | |
| Ogre mode | オーガモード | |
| Slot game | スロットゲーム | |
| Shop job (Monochromon's shop) | 店番 | 値上げする / 値下げする / 定価で売る |
| Customer, tally, prosperity | お客, 売上, 発展度 | |
| Treasure hunt plans | ふつう / スペシャル | |
| Meat Farm | 肉畑 | |
| Messenger, bulletin board | 運び屋, 掲示板 | |
| Shell Newspaper | 貝がら新聞 | |
| Analog humans, Net Keeper | アナログ人間, ネットキーパー | |
| Iceberg Beach | 氷山の浜辺 | |
| Training Camp | トレーニングキャンプ | |
| Digimon World (the place), Net World | デジモンワールド, ネットワールド | |

«X joins the city!» es «Xが町の仲間になった！»; cuando un nombre largo no cabe en 30 columnas pasa a «Xが町の仲間に！» o
«Xが町に加わった！», o va en dos líneas.

### Voces

| Personaje | Voz |
|---|---|
| Jijimon | anciano: わし, 〜じゃ, 〜のう |
| Jugador, compañero | informal, ぼく cuando hace falta un pronombre |
| Leomon, Seadramon, Centarumon | digno: わたし, 〜のだ |
| Ogremon, Meramon, Greymon, monstruos rudos | オレ, 〜だぜ |
| Etemon, Vegiemon | fanfarrón: オレ様 |
| Ninjamon | de samurái: 拙者, 〜でござる |
| Mamemon, Meteormon, Drimogemon | オイラ |
| Piximon, Patamon, Koromon, Poyomon | infantil: ボク / ぼく |
| Palmon, Biyomon, Shellmon | femenina suave: わたし, 〜わ |
| Angemon | arcaico: 我, 汝 |
| Cherrymon, MasterTyrannomon | sabio anciano: わし, 〜じゃ |
| ShogunGekomon | señorial: わし, 〜のだ, 〜ぞ (sin じゃ) |
| Gekomon | «ケロ» |
| Andromon, recepcionistas, el archivista, textos de ayuda | cortés です/ます |
| Tenderos | informales y amables; el jugador como tendero, cortés |
