# Korean translation glossary

The terms that repeat in the Korean translation (code `ko`), so that they always say the same thing. It covers both
the engine's texts (`content/packs/game/localization/ko/`) and the original's texts
(`content/packs/ko-translation/text/ko/`, the same flow as Spanish: [Translating the original's texts](translate.md)).
A new term that repeats is added here with the translation that first uses it. The Spanish decisions are in the
[Spanish glossary](spanish-glossary.md); Korean follows them unless this page says otherwise.

## Decisions

1. Standard modern Korean, friendly and simple, like a children's adventure game: short sentences, everyday words,
   no slang that dates quickly.
2. **Speech levels.** The characters talk to the player in casual **반말** («저장할래?», «고마워!»), the way the
   original's English is informal; elders like Jijimon use a warm elder's 반말 («~구나», «~단다»). Shopkeepers and
   service counters speak polite **해요체** to customers («어서 오세요!», «또 오세요!»). The player's own lines are
   casual. System prompts and notices use 해요체 («저장할까요?», «다시 시도해 주세요»); bare labels are short nouns
   («저장», «취소», «설정»).
3. Item names, technique names, places, medals, tournaments and music titles are translated, with words of our own.
   No name is taken from any other translation, dub or game.
4. **Digimon and character names are written in Hangul**, by how the English name is read («Agumon» → **아구몬**,
   «Jijimon» → **지지몬**), the coordinator's decision of 2026-11-20 for Korean: Latin letters inside Korean text read
   badly. They are the translation's own `main.digimon_names` (and the curling speakers that are just a name), so
   `scripts/translation.sh` counts them as translated for Korean while other languages keep them in English. The
   spellings are in [Digimon names](#digimon-names). The common noun *Digimon* is **디지몬**. `HP`, `MP` and `Bits`
   stay in Latin letters («300 Bits»); `Overdell` stays as a proper name.
5. **Particles after names.** Every name ends in a final consonant (…몬, …맨, 왕), so it takes **이, 을, 은, 과,
   으로** («아구몬이», «지지몬과»). After a placeholder whose word is not known (`{partner}`, `{tamer}`, `{name}`,
   `{item}`...) both forms are written, the usual way of Korean games: **이(가), 을(를), 은(는), 와(과), (으)로**.
6. The stages: **유아기, 유년기, 성장기, 성숙기, 완전체** (Fresh, In-Training, Rookie, Champion, Ultimate). With the
   `canonical_stages` option: **디지타마, 유년기 I, 유년기 II, 성장기, 성숙기, 완전체, 궁극체, 초궁극체**.
7. When the English is broken, the intent is translated; every control code and placeholder stays.
8. Typography: `...` as the English writes it, `!` and `?` as in the English, “double quotes” where the text quotes,
   no spaces before punctuation. Menu titles that the original writes in capitals have no capitals in Hangul and are
   written plainly («새 게임»).
9. **Fit.** Hangul is drawn two columns wide: a story line holds about **15 Hangul syllables** (30 columns, spaces
   one column), item names about **6** (12 columns), technique names about **8**, map names are centred in 24
   columns. Korean is usually shorter than English in meaning, so a line rarely needs cutting; when it does, a shorter
   synonym is used, never a word cut in the middle. Lines break between words (어절), never inside one.
10. Plurals: Korean has no plural agreement. The engine's `{n:one|other}` gets the same form twice (`{days:일|일}`)
    or just the bare `{n}` with a counter («{days}일», «{count}개»). Counters: 개 (items), 마리 (fish, Digimon),
    번/회 (times), 일 (days), 년 (years), 장 (cards).
11. Grammatical person: Korean drops «you» where it can; when it is needed the characters say **너**, never 당신.

## Game and raising

| English | Korean | Note |
|---|---|---|
| Digimon | 디지몬 | the common noun; species names in [Digimon names](#digimon-names) |
| Partner | 파트너 | |
| Tamer | 테이머 | «테이머 레벨» |
| Digivolve, Digivolution | 진화하다, 진화 | «그레이몬으로 진화!» |
| Raise | 키우다, 육성 | |
| Full (hunger), Hunger | 배부르다, 배고픔 | «배를 꽤 채워 준다» |
| Tiredness | 피로 | |
| Discipline | 규율 | |
| Happiness | 행복 | |
| Weight | 체중 | |
| Life span | 수명 | |
| Virus | 바이러스 | |
| Sick, sickness | 병에 걸리다, 병 | «(이)가 아프다!» |
| Wound, injured | 상처, 부상 | |
| Status errors | 상태 이상 | |
| Coma | 혼수 | |
| Poison / Confusion / Stun / Flat | 독 / 혼란 / 기절 / 납작 | |
| Potty | 화장실, 볼일 | «볼일을 보다» |
| Rest / sleep | 휴식 / 잠 | |
| Training | 훈련 | |
| Medal | 메달 | |
| Digitama, Digi-Egg | 디지타마 | common noun, like 디지몬 |
| Parameters, stats | 능력치 | |
| Care mistakes | 돌봄 실수 | |
| Lives | 목숨 | |
| Data / Vaccine / Virus type | 데이터형 / 백신형 / 바이러스형 | |
| Card | 카드 | |
| Bits | Bits | the currency |

## Attributes

| English | Korean | Short label |
|---|---|---|
| HP | HP | |
| MP | MP | |
| Offense, Off. Power | 공격력 | 공격 |
| Defense, Def. Power | 방어력 | 방어 |
| Speed | 속도 | 속도 |
| Brains | 지능 | 지능 |
| Abilities | 능력 | |

## Battle and techniques

| English | Korean | Note |
|---|---|---|
| Battle | 배틀 | |
| Technique, Tech | 기술 | |
| Mastered / Unmastered | 습득 / 미습득 | |
| Set technique | 기술 장착 | |
| Finisher | 필살기 | «필살 게이지» |
| Damage | 대미지 | |
| Run | 도망 / 도망쳐! | order / shout |
| Attack | 공격 / 공격해! | |
| Your Call, Auto | 알아서 / 자동 | «알아서 해!» |
| Moderate | 적당히 / 적당히! | |
| Distance | 거리 두기 / 거리 둬! | |
| Defensive | 방어 / 방어해! | |
| Change | 교체 / 바꿔! | |
| Change target | 목표를 바꿔! | |
| Go all the way | 전력을 다해! | |
| Hang in there | 버텨! | |
| Take it easy, Keep it down | 살살 해! | |

Technique names are our own descriptive Korean words, about 8 syllables at most («불기둥», «메가톤 펀치»).

## Items

| English | Korean |
|---|---|
| Item | 아이템 |
| Use / Move / Sort / Drop | 사용 / 이동 / 정렬 / 버리기 |
| Recovery | 회복약(소/중/대/특) |
| Floppy, Disk | 플로피, 디스크 |
| Chip | 칩 |
| Mushroom | 버섯 |
| Berry | 베리 |
| Meat / Giant Meat | 고기 / 왕고기 |
| Bait | 미끼 |
| Fishing rod | 낚싯대 |
| Key | 열쇠 |

Item names fit in about 6 syllables (12 columns), like the original's 12 characters.

## Places

| English | Korean |
|---|---|
| File City | 파일 시티 |
| Mt. Panorama | 파노라마 산 |
| Native Forest | 원시의 숲 |
| Drill Tunnel | 드릴 터널 |
| Great Canyon | 대협곡 |
| Ogre Fortress | 오거 요새 |
| Freezeland | 얼음 나라 |
| Gear Savanna | 기어 사바나 |
| Toy Town | 장난감 마을 |
| Factorial Town | 팩토리얼 마을 |
| Beetle Land | 딱정벌레 나라 |
| Trash Mountain | 쓰레기 산 |
| Grey Lord's Mansion | 회색 영주의 저택 |
| Misty Trees | 안개 숲 |
| Geko Swamp | 게코 늪 |
| Ancient Dino Region | 고대 공룡 지대 |
| Mt. Infinity | 무한 산 |
| Back Dimension | 이면 차원 |
| Overdell | Overdell |
| File Island | 파일 섬 |
| Arena | 경기장 |
| Item Keeper | 아이템 보관소 |
| Lake Guardian | 호수의 수호신 |

## Tournaments

| English | Korean |
|---|---|
| Tournament | 대회 |
| Grade D… | D급… |
| Cup | 컵 |
| Version 1 Cup | 버전 1 컵 |
| Fire / Grapple / Thunder Wind / Cool / Nature / Metalic / Filth | 불꽃 / 격투 / 천둥바람 / 냉기 / 자연 / 메탈 / 오물 |
| Dino / Wing / Animal / Human / Beetle | 공룡 / 날개 / 동물 / 인간 / 갑충 |

## Menus and system

| English | Korean |
|---|---|
| New game / Continue | 새 게임 / 이어하기 |
| Save / Load / Delete / Copy | 저장 / 불러오기 / 삭제 / 복사 |
| Data file | 데이터 |
| Memory Card | 메모리 카드 |
| Slot | 슬롯 |
| Register | 등록 |
| Select / Cancel / Back | 선택 / 취소 / 뒤로 |
| Yes / No | 예 / 아니요 |
| Pause | 일시정지 |
| Settings, Options | 설정 |
| Improvement / As the original | 개선 / 원작대로 |
| Co-op / Online versus | 협동 / 온라인 대전 |
| Room / Host / Guest | 방 / 호스트 / 게스트 |
| Trade | 교환 |
| Save data / Autosave | 저장 데이터 / 자동 저장 |
| Shop job | 가게 일 |
| Launcher / Add-on | 런처 / 애드온 |
| Name | 이름 |
| Player | 플레이어 |
| Directional Pad | 방향 버튼 |
| Press X | {prompt:confirm} 버튼을 눌러 |

## Story

The dialogue of the event scripts (`text/ko/story_<n>.yaml`). The speaker's line (`{color:7}<name> {color:1}\n`) is
copied with the speaker's name in Hangul.

| English | Korean | Note |
|---|---|---|
| Digivice | 디지바이스 | |
| Merit, Merit points | 공적, 공적 포인트 | the card shop |
| Dimensional Key Chain | 차원 열쇠고리 | |
| Market manager | 시장 관리인 | |
| Ogremon's gang, hideout | 산적단, 아지트 | |
| Insectoid Digimon | 곤충형 디지몬 | |
| Digivolution charts | 진화 도감 | |
| Training: X | {color:5}훈련: X | the training boxes |
| Normal / Bonus Try | 일반 / 보너스 도전 | |
| Card pack | 팩 | «3장 팩» |
| Master (Tyrannomon's) | 스승님 | |
| Tournament Schedule | 대회 일정 | |
| Enter (a tournament) | 참가하다 | |
| Qualifications | 참가 조건 | |
| Battle Royal | 배틀 로열 | |
| X joins the city! | X(이)가 마을에 들어왔다! | «아구몬이 마을에 들어왔다!»; the town is «마을» in dialogue |
| Begin / Explain / Quit | 시작 / 설명 / 그만두기 | the training sessions |
| Slot game | 슬롯머신 | |
| Vending Machine | 자판기 | prices in figures: «고기: 300 Bits» |
| Elevator | 엘리베이터 | |
| Come again. | 또 오세요. | the shops |
| See ya (option) | 안녕 | |
| Yes / No (the player's answers) | 응 / 아니 | the system's own prompts say 예 / 아니요 |
| Prize money | 우승 상금: 5000 Bits | tournament boards |

## Digimon names

Our own Hangul spelling of each name, by how the English reads (Japanese-style names by their syllables).

| English | Korean |
|---|---|
| Agumon | 아구몬 |
| Airdramon | 에어드라몬 |
| Akatorimon | 아카토리몬 |
| Analogman | 아날로그맨 |
| Andromon | 안드로몬 |
| Angemon | 엔젤몬 |
| Aruraumon | 아루라우몬 |
| Bakemon | 바케몬 |
| Betamon | 베타몬 |
| Birdramon | 버드라몬 |
| Biyomon | 비요몬 |
| BlueMeramon | 블루메라몬 |
| Botamon | 보타몬 |
| Brachiomon | 브라키오몬 |
| Centarumon | 센타루몬 |
| Cherrymon | 체리몬 |
| ClearAgumon | 클리어아구몬 |
| Coelamon | 코엘라몬 |
| Darkrizamon | 다크리자몬 |
| DemiMeramon | 데미메라몬 |
| Devimon | 데비몬 |
| Digitamamon | 디지타마몬 |
| Dokunemon | 도쿠네몬 |
| Drimogemon | 드리모게몬 |
| Elecmon | 엘렉몬 |
| Etemon | 에테몬 |
| Flarerizamon | 플레어리자몬 |
| Frigimon | 프리지몬 |
| Fugamon | 후가몬 |
| Gabumon | 가부몬 |
| Garurumon | 가루루몬 |
| Gekomon | 게코몬 |
| Geremon | 게레몬 |
| Gigadramon | 기가드라몬 |
| Giromon | 기로몬 |
| Goburimon | 고부리몬 |
| Gotsumon | 고츠몬 |
| Greymon | 그레이몬 |
| Guardromon | 가드로몬 |
| Gururumon | 구루루몬 |
| H-Kabuterimon | H-카부테리몬 |
| Hagurumon | 하구루몬 |
| HerculesKabuterimon | 헤라클레스카부테리몬 |
| Hyogamon | 효가몬 |
| IceDevimon | 아이스데비몬 |
| Icemon | 아이스몬 |
| J-Mojyamon | J-모쟈몬 |
| Jijimon | 지지몬 |
| Kabuterimon | 카부테리몬 |
| King of Sukamon | 스카몬 왕 |
| Kokatorimon | 코카토리몬 |
| Koromon | 코로몬 |
| Kunemon | 쿠네몬 |
| Kuwagamon | 쿠와가몬 |
| Leomon | 레오몬 |
| Machinedramon | 머신드라몬 |
| Mamemon | 마메몬 |
| Master Tyrannomon | 마스터 티라노몬 |
| MegaSeadramon | 메가시드라몬 |
| Megadramon | 메가드라몬 |
| Meramon | 메라몬 |
| MetalEtemon | 메탈에테몬 |
| MetalGreymon | 메탈그레이몬 |
| MetalMamemon | 메탈마메몬 |
| Meteormon | 메테오몬 |
| ModokiBetamon | 모도키베타몬 |
| Mojyamon | 모쟈몬 |
| Monochromon | 모노크로몬 |
| Monzaemon | 몬자에몬 |
| MoriShellmon | 모리셸몬 |
| Muchomon | 무초몬 |
| MudFrigimon | 머드프리지몬 |
| Myotismon | 마이오티스몬 |
| Nanimon | 나니몬 |
| Ninjamon | 닌자몬 |
| NiseDrimogemon | 니세드리모게몬 |
| Numemon | 누메몬 |
| Ogremon | 오거몬 |
| Otamamon | 오타마몬 |
| Palmon | 팔몬 |
| Panjyamon | 판쟈몬 |
| Patamon | 파타몬 |
| Penguinmon | 펭귄몬 |
| Phoenixmon | 피닉스몬 |
| Piddomon | 피도몬 |
| Piximon | 픽시몬 |
| PlatinumSukamon | 플래티넘스카몬 |
| Poyomon | 포요몬 |
| Psychemon | 사이케몬 |
| Punimon | 푸니몬 |
| RedVegiemon | 레드베지몬 |
| Rockmon | 록몬 |
| Saberdramon | 세이버드라몬 |
| SandYanmamon | 샌드얀마몬 |
| Seadramon | 시드라몬 |
| Sharmamon | 샤마몬 |
| Shellmon | 셸몬 |
| ShimaUnimon | 시마유니몬 |
| ShogunGekomon | 쇼군게코몬 |
| SkullGreymon | 스컬그레이몬 |
| SnowAgumon | 스노우아구몬 |
| SnowGoburimon | 스노우고부리몬 |
| Soulmon | 소울몬 |
| Sukamon | 스카몬 |
| Tanemon | 타네몬 |
| Tankmon | 탱크몬 |
| Tekkamon | 텟카몬 |
| Tentomon | 텐토몬 |
| Tinmon | 틴몬 |
| Tokomon | 토코몬 |
| ToyAgumon | 토이아구몬 |
| Tsukaimon | 츠카이몬 |
| Tsunomon | 츠노몬 |
| Tyrannomon | 티라노몬 |
| Unimon | 유니몬 |
| Vademon | 베이드몬 |
| Vegiemon | 베지몬 |
| Vermilimon | 버밀리몬 |
| WaruMonzaemon | 와루몬자에몬 |
| WaruSeadramon | 와루시드라몬 |
| Weedmon | 위드몬 |
| WereGarurumon | 워가루루몬 |
| Whamon | 와몬 |
| Yanmamon | 얀마몬 |
| Yuramon | 유라몬 |
