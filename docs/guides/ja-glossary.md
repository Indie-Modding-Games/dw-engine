# Japanese translation glossary

The terms that repeat in the Japanese translation (code `ja`), so that they always say the same thing. It covers both
the engine's texts (`content/packs/game/localization/ja/`) and the original's texts
(`content/packs/ja-translation/text/ja/`, the same flow as Spanish: [Translating the original's texts](translate.md)).
A new term that repeats is added here with the translation that first uses it. The Spanish decisions are in the
[Spanish glossary](spanish-glossary.md); Japanese follows them unless this page says otherwise.

## Decisions

1. Standard modern Japanese, friendly and simple, like a children's adventure game. Everyday kanji only (no rare
   kanji, no furigana); words a child reads easily go in kana («おなか», «ケガ», «しつけ»).
2. Our own translation from the extracted English, with the Spanish as a second reference for intent. No text, name or
   term is taken from the Japanese release of the game, from any other translation, dub or game. Where a plain
   translation of the English happens to be an ordinary Japanese word, that word is used.
3. Register: the system (menus, prompts, notices, save screens) is polite and neutral (です/ます: «セーブしますか？»).
   The characters speak casually, each in their own voice (Jijimon as an old man: わし, 〜じゃ, 〜のう; shopkeepers
   friendly-polite: «いらっしゃい！»). The player is addressed without pronouns where Japanese allows it, and as
   «きみ» when one is needed: the informal "you" the Spanish *tuteo* asks for.
4. Digimon names and the characters' names are written in katakana, our own reading of the English spelling, not the
   Japanese release's names (decision 2): «アグモン», «ジジモン», «マイオティスモン», «センタルモン». Japanese, like
   Korean, translates the `digimon_names` block (`own_names` in `scripts/translation.sh`); the speaker lines and every name inside a text use the same katakana. The generic word "Digimon" is «デジモン».
5. `HP` and `MP` stay. The currency is «ビット» everywhere, the menus' label too (`common.bits`; the original's
   `Bits` label stays kept in the lock and is no longer shown). Numbers are half-width digits with the original's
   figures.
6. The stages, our own renderings of the English stages: **フレッシュ, 見習い, ルーキー, チャンピオン,
   アルティメット** (Fresh, In-Training, Rookie, Champion, Ultimate); with the canonical stages on: **デジエッグ,
   フレッシュ, 見習い, ルーキー, チャンピオン, アルティメット, メガ, ウルトラ**. The franchise's Japanese stage terms
   are not used (decision 2).
7. Technique names are translated by meaning, with our own words (kanji compounds and common loanwords: «炎の塔»,
   «氷の針»), never a katakana copy of a name from any series.
8. When the English is broken, the intent is translated; every control code and placeholder stays. Japanese has no
   plural: every form of `{n:one|other}` gets the same Japanese (`{days}{days:日|日}`), or just `{n}日`.
9. Typography: full-width «、。！？» and «「」» for quotes, «…» for the English "...", «・» to join short words, half-width
   digits and Latin letters, «〜» for drawn-out sounds. No spaces between Japanese words; a line never starts with
   «、。！？」ー» or a small kana.
10. Fit: a CJK character is two columns. Story lines are at most 30 columns (15 kana/kanji; Latin letters and digits
    count one), at most as many lines as the English and never more than 4 per box. Item names fit in about 7
    characters, item descriptions in about 14, technique names in about 9, map names are centred in 24 columns.

## Game and raising

| English | Japanese | Note |
|---|---|---|
| Digimon (generic) | デジモン | each name: `main.digimon_names` |
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
| Training, Gym | トレーニング, ジム | menus: はじめる / 説明 / やめる |
| Care mistakes | お世話ミス | |
| Lives (partner lives) | ライフ | |
| Evolution chart | 進化図 | |
| Digivolution items | 進化アイテム | |
| Data / Vaccine / Virus type | データ種 / ワクチン種 / ウィルス種 | |
| Egg (choosing an egg) | タマゴ | デジエッグ for the stage and the English "Digitama" |
| Praise / Scold | ほめる / しかる | |
| Medal | メダル | |
| Card | カード | |
| Bits | ビット | |
| Digivice | デジヴァイス | |

## Attributes

| English | Japanese | Short |
|---|---|---|
| HP / MP | HP / MP | |
| Offense, Off. Power | 攻撃力 | 攻撃, 攻 |
| Defense, Def. Power | 防御力 | 防御, 防 |
| Speed | 素早さ | 速 |
| Brains | 賢さ | 賢 |
| Abilities | 能力 | |

## Battle and techniques

| English | Japanese | Note |
|---|---|---|
| Battle | バトル | |
| Technique | 技 | |
| Finishing move | 必殺技 | |
| Mastered | 習得 | |
| Enemy | 敵 | |
| Run (order) | 逃げる / 逃げろ！ | menu / shout |
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

## Items

| English | Japanese |
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

The full list is `main.item_names` in `text/ja/main.yaml`; every text that names an item uses that same name.

## Places

| English | Japanese |
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
| Overdell | Overdell (kept) / オーバーデル in other names |
| Jijimon's house | ジジモンの家 |
| Green Gym | グリーンジム |
| Centar Clinic | センタル診療所 |
| Birdra Transport | バードラ便 |

The full list is `main.map_names`, centred in 24 columns (a CJK character counts two).

## Tournaments

| English | Japanese |
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

Tamer titles: アマチュア, 新米, 初心者, 中級者, 上級者, ベテラン, 一流, 超一流, 達人, 天才, 伝説.

## Menus and system

| English | Japanese |
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
| Fixed in {variant} (locked option) | {variant}では固定 |
| Open to new players (co-op) | 新しいプレイヤーの参加 |
| Player (default online name) | プレイヤー |
| Bits (currency label in menus) | ビット (common.bits; the extracted label stays as kept) |
| The game's log (where an error's details go) | ゲームのログ |
| Unknown variant (a save's mode that is not installed) | 不明なバージョン |
| Key caps of the classic prompts (`prompt.cap.*`) | as on the keyboard, in English: ESC, DEL, CTRL, SHIFT… |

## Story

| English | Japanese | Note |
|---|---|---|
| X joins the city! | Xが町の仲間になった！ | |
| Merit points | 功績ポイント | the card shop |
| Tournament Schedule | 大会スケジュール | |
| Enter, register | エントリーする | |
| Vending Machine | 自動販売機 | «肉：300ビット» |
| Elevator | エレベーター | |
| Lake Guardian | 湖の守り神 | |
| Come again. | また来てね。 | the shops |
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

"X joins the city!" is «Xが町の仲間になった！»; when a long name does not fit 30 columns it becomes «Xが町の仲間に！» or
«Xが町に加わった！», or goes on two lines.

### Voices

| Character | Voice |
|---|---|
| Jijimon | old man: わし, 〜じゃ, 〜のう |
| Player, partner | casual, ぼく when a pronoun is needed |
| Leomon, Seadramon, Centarumon | dignified: わたし, 〜のだ |
| Ogremon, Meramon, Greymon, rough monsters | オレ, 〜だぜ |
| Etemon, Vegiemon | cocky: オレ様 |
| Ninjamon | samurai-like: 拙者, 〜でござる |
| Mamemon, Meteormon, Drimogemon | オイラ |
| Piximon, Patamon, Koromon, Poyomon | childlike: ボク / ぼく |
| Palmon, Biyomon, Shellmon | soft feminine: わたし, 〜わ |
| Angemon | archaic: 我, 汝 |
| Cherrymon, MasterTyrannomon | old sage: わし, 〜じゃ |
| ShogunGekomon | lordly: わし, 〜のだ, 〜ぞ (no じゃ) |
| Gekomon | «ケロ» |
| Andromon, receptionists, recordkeeper, help texts | polite です/ます |
| Shopkeepers | friendly casual, the player-shopkeeper polite |
