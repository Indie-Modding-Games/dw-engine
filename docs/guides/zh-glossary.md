# Simplified Chinese translation glossary

The terms that repeat in the Simplified Chinese translation (code `zh`) of the engine's texts
(`content/packs/game/localization/zh/`) and of the original's texts (`content/packs/zh-translation/`), so that they
always say the same thing. How a translation is written and kept up to date is in
[Translating the original's texts](translate.md); the Spanish choices this one started from are in the
[Spanish glossary](spanish-glossary.md). A new term that repeats is added here with the translation that first uses it.

## Decisions

1. **Informal "you" (你, never 您)**, as the Spanish chose tuteo: every character speaks to the player and to each
   other in the friendly, simple tone of the original. Spoken particles (呢, 吧, 啊, 哦, 嘛) are welcome where a
   native speaker would use them; no classical or stiff written style.
2. **Digimon names stay in English**, letter by letter, typos included; they are not written in the pack (the missing
   key shows the English). The same goes for the names of the people and Digimon who speak in the story
   (`Jijimon`, `Leomon`...). The two titles of that block that are English words are translated: `Market Manager`
   («市场经理») and `main character` («主角»). No space is added between a Latin name and the Chinese around it
   (`Agumon生病了`), except where the original's string already starts with one.
3. **The word "Digimon" as a common noun is translated: 数码兽.** Only species names stay in English
   (`Agumon`, not `亚古兽`). Compound places and items follow (`数码兽桥`, `数码兽冰壶`).
4. **Technique names are translated** with words of our own that describe the attack (not names from any series or
   dub); a typo in the original is translated by its intent. They fit in about 8 characters.
5. **Item names are translated** and fit in 6 characters (12 columns, the original's 11-letter column), without
   abbreviation dots.
6. **`HP`, `MP` and `Bits` stay as in the original**, like in Spanish: the gauges and the shared tooling
   (`kept_original_ids` in `scripts/translation.sh`) treat them as symbols. A number goes before them with a space
   (`500 Bits`, `+500 MP`).
7. **Stages:** 幼年期I (Fresh), 幼年期II (In-Training), 成长期 (Rookie), 成熟期 (Champion), 完全体 (Ultimate).
8. **Places are translated**, except proper names that are not words (`Overdell`, `Geko`, `Centar`, `Birdra`,
   `Kunemon`); a name built from a Digimon keeps the Digimon in English (`Leomon祖先洞窟`, `Jijimon之家`).
9. **Punctuation is full width** (，。！？：；、……), with “ ” for quotes and 「」 for titles or signs spoken aloud;
   `...` in the English becomes `……` (two characters). `!?` becomes `？！`. Full-width punctuation counts as one
   character (two columns) in the line limits. Before Latin letters, digits or a placeholder the colon is `: `
   (ASCII colon and a space): the original look's font draws `：` as its narrow `:`, which would touch the next letter.
10. **Line width:** a story line holds 15 Chinese characters (30 columns, two per character, the box's width);
    `scripts/translation.sh long` measures CJK at two columns. One-line texts (item descriptions, fishing and curling
    lines) stay within about 14 characters. Strings the original splits into columns by spaces keep each column's
    width in columns (a Chinese character is two).
11. **Text that does not fit is shortened** the way a Chinese localizer would (a shorter word, a dropped particle),
    never by cutting a word in two.
12. **Broken English is translated by its intent**, keeping every control code (the Spanish rule).
13. **Plurals:** Chinese has no plural; a `{n:one|other}` form writes the same text in both slots or uses the
    last form (`{n}天`).

## Game and raising

| English | Chinese | Note |
|---|---|---|
| Digimon (common noun) | 数码兽 | species names stay English |
| partner | 伙伴 | |
| tamer | 驯兽师 | tamer level: 驯兽师等级 |
| digivolve / digivolution | 进化 | `进化为Greymon！` |
| raise | 培育 | |
| train / training | 训练 | |
| Fresh / In-Training | 幼年期I / 幼年期II | card chart columns: 幼年1 / 幼年2 |
| Rookie / Champion / Ultimate | 成长期 / 成熟期 / 完全体 | |
| lifespan | 寿命 | |
| hunger / full | 饥饿 / 吃饱 | |
| tiredness | 疲劳 | |
| discipline | 纪律 | |
| happiness | 快乐 | |
| weight | 体重 | |
| sick / sickness | 生病 / 疾病 | |
| injured / wound | 受伤 / 伤势 | |
| poop / potty / toilet | 便便 / 上厕所 / 厕所 | |
| sleep / rest | 睡觉 / 休息 | |
| meat farm | 肉田 | |
| care mistake | 照顾失误 | |
| prosperity | 繁荣度 | |
| rebirth / reincarnate | 转生 | |
| Digi-Egg / egg / Digitama | 数码蛋 | |
| parameters / stats | 能力值 | |
| Data / Vaccine / Virus | 数据种 / 疫苗种 / 病毒种 | |
| Merit / merit points | 功绩点 | card shop |
| joins the city | XXX加入了城镇！ | |
| Digivice | Digivice | kept in English, like the Spanish |
| Analog Humans | 模拟人类 | |

## Attributes

| English | Chinese |
|---|---|
| Offense / Off | 攻击 |
| Defense / Def | 防御 |
| Speed | 速度 |
| Brains / Brain | 智力 |
| HP / MP | HP / MP |
| status error | 异常状态 |
| coma | 昏迷 |
| poison / confusion / stun / flat | 中毒 / 混乱 / 麻痹 / 压扁 |

## Battle and techniques

| English | Chinese |
|---|---|
| battle | 战斗 |
| technique | 招式 |
| finisher (Final) | 必杀 |
| command | 指令 |
| Run / Attack / Your Call (Auto) | 逃跑 / 进攻 / 自由 (自动) |
| Moderate / Distance / Defensive / Change | 稳健 / 远离 / 防守 / 切换 |
| mastered / unmastered | 已掌握 / 未掌握 |
| win / loss / draw | 胜 / 负 / 平局 |
| enemy | 敌人 |
| finishing technique / finish gauge | 必杀招式 / 必杀槽 |
| technique range: close / long / wide / self | 近程 / 远程 / 广域 / 自身 |
| specialties: fire / battle / air / earth / ice / mech / filth | 火焰 / 格斗 / 空中 / 大地 / 冰霜 / 机械 / 污秽 |
| boss | 头目 |

Technique names: see `main.move_names` in `text/zh/main.yaml` (`Pepper Breath` → 辣椒吐息, `Party time` →
派对时间, `Bubble` → 泡泡).

## Items

| English | Chinese |
|---|---|
| item | 道具 |
| recovery (sm./med./lrg./sup.) | 小/中/大/超回复药 |
| Floppy (MP) | 软盘 |
| Disk / Chip | 磁盘 / 芯片 |
| Meat / Giant Meat / Sirloin / Steak | 肉 / 大块肉 / 西冷肉 / 牛排 |
| Auto Pilot | 自动导航 |
| bandage / medicine | 绷带 / 药品 |
| fishing rod | 钓竿 |
| card | 卡片 |
| medal | 奖章 |
| bag | 背包 (the original's fishing lines say 包包) |
| Dimensional Key Chain | 次元钥匙扣 |
| Steel Heart / Blood Moon | 钢铁之心 / 血月 |

Every item name is in `main.item_names`; dialogue that names an item uses exactly that name.

## Places

| English | Chinese |
|---|---|
| File Island | 文件岛 |
| File City | 文件城 |
| Jijimon's house | Jijimon之家 |
| Item Keeper | 道具保管所 |
| Item Shop / Secret Item Shop | 道具店 / 秘密道具店 |
| Native Forest | 原生森林 |
| Coela Point | 腔棘岬 |
| Dragon Eye Lake | 龙眼湖 |
| Mt. Panorama | 全景山 |
| Drill Tunnel | 钻头隧道 |
| Great Canyon | 大峡谷 |
| Ogre Fortress | 恶鬼要塞 |
| Grey Lord's Mansion | 灰色领主宅邸 |
| Gear Savanna | 齿轮草原 |
| Freezeland | 冰冻大地 |
| Misty Trees | 迷雾森林 |
| Toy Town | 玩具镇 |
| Factorial Town | 工厂镇 |
| Trash Mountain | 垃圾山 |
| Beetle Land | 甲虫乐园 |
| Mt. Infinity | 无限山 |
| Back Dimension | 里次元 |
| Amida Forest | 阿弥陀森林 |
| Arena / Coliseum | 竞技场 |
| clinic / restaurant | 诊所 / 餐厅 |
| Iceberg Beach | 冰山海滩 |
| Glacial / Speedy Time Zone | 冰河时代区 / 疾速时代区 |
| Trash Pile / Dust Kingdom | 垃圾场 / 尘埃王国 |
| Everything Shop | 杂货店 |
| Beatwood Coliseum | 节拍森林竞技场 |

The full list is `main.map_names`, centered in 24 columns (two per Chinese character).

## Tournaments

| English | Chinese |
|---|---|
| tournament | 大赛 |
| Grade D Cup (…S, R) | D级赛 |
| Version 1 Cup | 版本1杯 |
| Fire / Grapple / Thunder Wind / Cool Cup | 火焰杯 / 格斗杯 / 雷风杯 / 冰霜杯 |
| Nature / Metalic / Filth Cup | 自然杯 / 金属杯 / 污秽杯 |
| Dino / Wing / Animal / Human / Beetle Cup | 恐龙杯 / 羽翼杯 / 动物杯 / 人形杯 / 甲虫杯 |
| champion (title) | 冠军 |
| curling | 冰壶 |
| Battle Royal | 大混战 |
| second place / prize money | 亚军 / 奖金 |
| Grade / Special Tournament | 等级大赛 / 特别大赛 |

## Menus and system

| English | Chinese |
|---|---|
| Yes / No | 是 / 否 |
| OK / Cancel / Back | 确定 / 取消 / 返回 |
| save / load | 保存 / 读取 |
| new game / continue | 新游戏 / 继续游戏 |
| options / settings | 选项 / 设置 |
| skin | 皮肤 |
| language | 语言 |
| co-op / host / guest / room | 合作 / 房主 / 访客 / 房间 |
| online | 联机 |
| Memory Card / slot | 记忆卡 / 卡槽 |
| Sign / Box (`main.special_speakers`) | 告示牌 / 箱子 |
| pause | 暂停 |
| save slot / autosave | 存档位 / 自动保存 |
| room code / relay / direct | 房间代码 / 中继 / 直连 |
| ready | 已准备 |
| as the original (option value) | 同原版 |
| Mega / Ultra (engine stages) | 究极体 / 超究极体 |
| shop job: tally / patience | 营业额 / 耐心 |
| launcher / add-ons | 启动器 / 插件 |
| update channel: stable / testing (beta) | 更新渠道：稳定版 / 测试版 |
| logs / safe mode | 日志 / 安全模式 |
| disc image / extract | 光盘镜像 / 提取 |
| Game mode / variant | 游戏模式 / 版本 |
| Classic Enhanced / Folder Mode | 经典强化版 / Folder模式 |
| Fixed in {variant} (locked option) | {variant}中固定 |
| Open to new players (co-op) | 允许新玩家加入 |
| Player (default online name) | 玩家 |
| Bits (currency label in menus) | Bits |
| Hangul naming page | 한글 (the page's own letters, as in every language) |
| The game's log (where an error's details go) | 游戏日志 |
| Unknown variant (a save's mode that is not installed) | 未知版本 |
| Key caps of the classic prompts (`prompt.cap.*`) | as on the keyboard, in English: ESC, DEL, CTRL, SHIFT… |

## Story

- The speaker line `{color:7}<name> {color:1}\n` is copied exactly, name in English.
- Speaker names are never translated, even titles in English words (`{color:7}Sign {color:1}`,
  `Market manager`): the speaker line is copied as it is.
- Training boxes: `{color:5}训练：<name> {color:1}` with the menu 开始 / 说明 / 退出.
- Shop price minigame: 涨价 / 维持N Bits / 降价 / 再想想 / 原价 / 总利润.
- Choice menus keep their number of lines, one short option per line (about 6 characters).
- `{partner}` and `{tamer}` stand for names; nothing is added around them but the Chinese grammar needs (no
  measure words or spaces).
