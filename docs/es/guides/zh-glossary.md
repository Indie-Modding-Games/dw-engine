# Glosario de la traducción al chino simplificado

Los términos que se repiten en la traducción al chino simplificado (código `zh`) de los textos del motor
(`content/packs/game/localization/zh/`) y de los textos del original (`content/packs/zh-translation/`), para que
digan siempre lo mismo. Cómo se traduce y cómo se mantiene al día está en
[Traducir los textos del original](translate.md); las decisiones del español de las que parte están en el
[glosario del español](spanish-glossary.md). Un término nuevo que se repite se añade aquí con la traducción que lo usa
por primera vez.

## Decisiones

1. **Se tutea (你, nunca 您)**, como el español: todos hablan al jugador y entre sí con el tono amable y sencillo del
   original. Las partículas habladas (呢, 吧, 啊, 哦, 嘛) son bienvenidas donde un hablante nativo las usaría; nada de
   estilo clásico o escrito rígido.
2. **Los nombres de Digimon quedan en inglés**, letra por letra, erratas incluidas; no se escriben en el paquete (la
   clave que falta muestra el inglés). Lo mismo los nombres de quien habla en la historia (`Jijimon`, `Leomon`...). Los
   dos títulos de ese bloque que son palabras inglesas se traducen: `Market Manager` («市场经理») y `main character`
   («主角»). No se pone espacio entre un nombre latino y el chino que lo rodea (`Agumon生病了`), salvo donde la
   cadena del original ya empieza con uno.
3. **La palabra «Digimon» como sustantivo común se traduce: 数码兽.** Solo los nombres de especie quedan en inglés
   (`Agumon`, no `亚古兽`). Los lugares y objetos compuestos la siguen (`数码兽桥`, `数码兽冰壶`).
4. **Los nombres de técnica se traducen** con palabras propias que describen el ataque (no nombres de ninguna serie
   ni doblaje); una errata del original se traduce por su intención. Caben en unos 8 caracteres.
5. **Los nombres de objeto se traducen** y caben en 6 caracteres (12 columnas, la columna de 11 letras del original),
   sin puntos de abreviatura.
6. **`HP`, `MP` y `Bits` quedan como en el original**, como en español: las barras y las herramientas compartidas
   (`kept_original_ids` en `scripts/translation.sh`) los tratan como símbolos. El número va delante con un espacio
   (`500 Bits`, `+500 MP`).
7. **Etapas:** 幼年期I (Fresh), 幼年期II (In-Training), 成长期 (Rookie), 成熟期 (Champion), 完全体 (Ultimate).
8. **Los lugares se traducen**, salvo los nombres propios que no son palabras (`Overdell`, `Geko`, `Centar`,
   `Birdra`, `Kunemon`); un nombre formado con un Digimon lo deja en inglés (`Leomon祖先洞窟`, `Jijimon之家`).
9. **La puntuación es de ancho completo** (，。！？：；、……), con “ ” para las citas y 「」 para títulos o letreros
   leídos en voz alta; los `...` del inglés pasan a `……` (dos caracteres). `!?` pasa a `？！`. La puntuación de ancho
   completo cuenta como un carácter (dos columnas) en los límites de línea. Ante letras latinas, cifras o un marcador
   los dos puntos son `: ` (dos puntos ASCII y un espacio): la fuente del aspecto original dibuja `：` como su `:`
   estrecho, que quedaría pegado a la letra siguiente.
10. **Ancho de línea:** una línea de la historia cabe en 15 caracteres chinos (30 columnas, dos por carácter, el ancho
    del cuadro); `scripts/translation.sh long` mide el CJK a dos columnas. Los textos de una línea (descripciones de
    objetos, líneas de pesca y de curling) quedan en unos 14 caracteres. Las cadenas que el original parte en columnas
    con espacios conservan el ancho de cada columna en columnas (un carácter chino son dos).
11. **El texto que no cabe se acorta** como lo haría un localizador chino (una palabra más corta, una partícula
    menos), nunca cortando una palabra.
12. **El inglés roto se traduce por su intención**, conservando todos los códigos de control (la regla del español).
13. **Plurales:** el chino no tiene plural; una forma `{n:one|other}` escribe el mismo texto en las dos ranuras o usa
    la última forma (`{n}天`).

## Juego y crianza

| Inglés | Chino | Nota |
|---|---|---|
| Digimon (common noun) | 数码兽 | los nombres de especie quedan en inglés |
| partner | 伙伴 | |
| tamer | 驯兽师 | nivel de domador: 驯兽师等级 |
| digivolve / digivolution | 进化 | `进化为Greymon！` |
| raise | 培育 | |
| train / training | 训练 | |
| Fresh / In-Training | 幼年期I / 幼年期II | columnas de la tabla de cartas: 幼年1 / 幼年2 |
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
| Merit / merit points | 功绩点 | tienda de cartas |
| joins the city | XXX加入了城镇！ | |
| Digivice | Digivice | queda en inglés, como en español |
| Analog Humans | 模拟人类 | |

## Atributos

| Inglés | Chino |
|---|---|
| Offense / Off | 攻击 |
| Defense / Def | 防御 |
| Speed | 速度 |
| Brains / Brain | 智力 |
| HP / MP | HP / MP |
| status error | 异常状态 |
| coma | 昏迷 |
| poison / confusion / stun / flat | 中毒 / 混乱 / 麻痹 / 压扁 |

## Batalla y técnicas

| Inglés | Chino |
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

Nombres de técnica: ver `main.move_names` en `text/zh/main.yaml` (`Pepper Breath` → 辣椒吐息, `Party time` →
派对时间, `Bubble` → 泡泡).

## Objetos

| Inglés | Chino |
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
| bag | 背包 (las líneas de pesca del original dicen 包包) |
| Dimensional Key Chain | 次元钥匙扣 |
| Steel Heart / Blood Moon | 钢铁之心 / 血月 |

Todos los nombres de objeto están en `main.item_names`; el diálogo que nombra un objeto usa exactamente ese nombre.

## Lugares

| Inglés | Chino |
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

La lista completa es `main.map_names`, centrada en 24 columnas (dos por carácter chino).

## Torneos

| Inglés | Chino |
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

## Menús y sistema

| Inglés | Chino |
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
| as the original (valor de opción) | 同原版 |
| Mega / Ultra (etapas del motor) | 究极体 / 超究极体 |
| trabajo en la tienda: tally / patience | 营业额 / 耐心 |
| launcher / add-ons | 启动器 / 插件 |
| update channel: stable / testing (beta) | 更新渠道：稳定版 / 测试版 |
| logs / safe mode | 日志 / 安全模式 |
| disc image / extract | 光盘镜像 / 提取 |

## Historia

- La línea del hablante `{color:7}<nombre> {color:1}\n` se copia igual, con el nombre en inglés.
- Los nombres de hablante nunca se traducen, aunque sean títulos en palabras inglesas (`{color:7}Sign {color:1}`,
  `Market manager`): la línea del hablante se copia como está.
- Cuadros de entrenamiento: `{color:5}训练：<nombre> {color:1}` con el menú 开始 / 说明 / 退出.
- Minijuego de precios de la tienda: 涨价 / 维持N Bits / 降价 / 再想想 / 原价 / 总利润.
- Los menús de elección conservan su número de líneas, una opción corta por línea (unos 6 caracteres).
- `{partner}` y `{tamer}` representan nombres; no se añade nada a su alrededor salvo lo que pida la gramática china
  (ni clasificadores ni espacios).
