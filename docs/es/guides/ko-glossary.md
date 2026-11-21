# Glosario de la traducción al coreano

Los términos que se repiten en la traducción al coreano (código `ko`), para que digan siempre lo mismo. Cubre los
textos del motor (`content/packs/game/localization/ko/`) y los del original (`content/packs/ko-translation/text/ko/`,
con el mismo flujo que el español: [Traducir los textos del original](translate.md)). Un término nuevo que se repite
se agrega aquí con la traducción que lo usa primero. Las decisiones del español están en el
[glosario del español](spanish-glossary.md); el coreano las sigue salvo que esta página diga otra cosa.

## Decisiones

1. Coreano estándar moderno, amable y sencillo, como un juego de aventuras para niños: frases cortas, palabras de
   todos los días, sin jerga que envejezca rápido.
2. **Niveles de habla.** Los personajes le hablan al jugador en **반말** informal («저장할래?», «고마워!»), como el
   inglés informal del original; los mayores como Jijimon usan un 반말 cálido de abuelo («~구나», «~단다»). Los
   tenderos y los mostradores de servicio hablan en **해요체** cortés a los clientes («어서 오세요!», «또 오세요!»).
   Las líneas del propio jugador son informales. Los avisos y preguntas del sistema van en 해요체 («저장할까요?»,
   «다시 시도해 주세요»); las etiquetas sueltas son sustantivos cortos («저장», «취소», «설정»).
3. Los nombres de objetos, técnicas, lugares, medallas, torneos y temas musicales se traducen, con palabras propias.
   Ningún nombre se toma de otra traducción, doblaje o juego.
4. **Los nombres de los Digimon y de los personajes se escriben en hangul**, según se lee el nombre inglés
   («Agumon» → **아구몬**, «Jijimon» → **지지몬**), decisión del coordinador del 2026-11-20 para el coreano: las letras
   latinas dentro del texto coreano se leen mal. Son los `main.digimon_names` propios de la traducción (y los
   hablantes del curling que son solo un nombre), así que `scripts/translation.sh` los cuenta como traducidos para el
   coreano, mientras los otros idiomas los dejan en inglés. Las grafías están en
   [Nombres de los Digimon](#nombres-de-los-digimon). El sustantivo común *Digimon* es **디지몬**. `HP`, `MP` y `Bits`
   quedan en letras latinas («300 Bits»); `Overdell` queda como nombre propio.
5. **Partículas tras los nombres.** Todos los nombres terminan en consonante final (…몬, …맨, 왕), así que llevan
   **이, 을, 은, 과, 으로** («아구몬이», «지지몬과»). Tras un marcador cuya palabra no se conoce (`{partner}`,
   `{tamer}`, `{name}`, `{item}`...) se escriben las dos formas, como es costumbre en los juegos coreanos:
   **이(가), 을(를), 은(는), 와(과), (으)로**.
6. Las etapas: **유아기, 유년기, 성장기, 성숙기, 완전체** (Bebé, Entrenamiento, Novato, Campeón, Ultra). Con la
   opción `canonical_stages`: **디지타마, 유년기 I, 유년기 II, 성장기, 성숙기, 완전체, 궁극체, 초궁극체**.
7. Cuando el inglés está roto se traduce la intención; se conservan todos los códigos de control y marcadores.
8. Tipografía: `...` como lo escribe el inglés, `!` y `?` como en el inglés, “comillas dobles” donde el texto cita,
   sin espacios antes de la puntuación. Los títulos de menú que el original escribe en mayúsculas no tienen
   mayúsculas en hangul y se escriben tal cual («새 게임»).
9. **Que quepa.** El hangul se dibuja con dos columnas de ancho: una línea de la historia admite unas **15 sílabas**
   (30 columnas; los espacios ocupan una), los nombres de objetos unas **6** (12 columnas), los de técnicas unas
   **8**, y los nombres de mapas se centran en 24 columnas. El coreano suele ser más corto que el inglés en
   significado, así que rara vez hay que recortar; cuando hace falta se usa un sinónimo más corto, nunca una palabra
   cortada. Las líneas se parten entre palabras (어절), nunca dentro de una.
10. Plurales: el coreano no tiene concordancia de plural. El `{n:one|other}` del motor lleva la misma forma dos veces
    (`{days:일|일}`) o solo el `{n}` con un clasificador («{days}일», «{count}개»). Clasificadores: 개 (objetos),
    마리 (peces, Digimon), 번/회 (veces), 일 (días), 년 (años), 장 (cartas).
11. Persona: el coreano omite el «tú» siempre que puede; cuando hace falta los personajes dicen **너**, nunca 당신.
12. **Carteles de los mapas** (`signs.yaml`): las palabras van separadas por espacios, nunca por `\n`: el motor
    parte la línea en un espacio si el tablero tiene dos líneas y deja una sola si así las letras salen más grandes.

## Juego y crianza

| Inglés | Coreano | Nota |
|---|---|---|
| Digimon | 디지몬 | el sustantivo común; los nombres de especie en [Nombres de los Digimon](#nombres-de-los-digimon) |
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
| Digitama, Digi-Egg | 디지타마 | sustantivo común, como 디지몬 |
| Parameters, stats | 능력치 | |
| Care mistakes | 돌봄 실수 | |
| Lives | 목숨 | |
| Data / Vaccine / Virus type | 데이터형 / 백신형 / 바이러스형 | |
| Card | 카드 | |
| Bits | Bits | la moneda |

## Atributos

| Inglés | Coreano | Etiqueta corta |
|---|---|---|
| HP | HP | |
| MP | MP | |
| Offense, Off. Power | 공격력 | 공격 |
| Defense, Def. Power | 방어력 | 방어 |
| Speed | 속도 | 속도 |
| Brains | 지능 | 지능 |
| Abilities | 능력 | |

## Batalla y técnicas

| Inglés | Coreano | Nota |
|---|---|---|
| Battle | 배틀 | |
| Technique, Tech | 기술 | |
| Mastered / Unmastered | 습득 / 미습득 | |
| Set technique | 기술 장착 | |
| Finisher | 필살기 | «필살 게이지» |
| Damage | 대미지 | |
| Run | 도망 / 도망쳐! | orden / grito |
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

Los nombres de las técnicas son palabras coreanas propias que describen el ataque, de unas 8 sílabas como mucho
(«불기둥», «메가톤 펀치»).

## Objetos

| Inglés | Coreano |
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

Los nombres de objetos caben en unas 6 sílabas (12 columnas), como los 12 caracteres del original.

## Lugares

| Inglés | Coreano |
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

## Torneos

| Inglés | Coreano |
|---|---|
| Tournament | 대회 |
| Grade D… | D급… |
| Cup | 컵 |
| Version 1 Cup | 버전 1 컵 |
| Fire / Grapple / Thunder Wind / Cool / Nature / Metalic / Filth | 불꽃 / 격투 / 천둥바람 / 냉기 / 자연 / 메탈 / 오물 |
| Dino / Wing / Animal / Human / Beetle | 공룡 / 날개 / 동물 / 인간 / 갑충 |

## Menús y sistema

| Inglés | Coreano |
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
| Game mode / variant | 게임 모드 / 버전 |
| Classic Enhanced / Folder Mode | 클래식 강화판 / Folder 모드 |
| Fixed in {variant} (opción fijada) | {variant}에서 고정 |
| Open to new players (co-op) | 새 플레이어 참가 허용 |
| Player (nombre en línea por defecto) | 플레이어 |
| Bits (etiqueta de la moneda en los menús) | Bits |

## Historia

Los diálogos de los scripts de eventos (`text/ko/story_<n>.yaml`). La línea de quien habla
(`{color:7}<name> {color:1}\n`) se copia con el nombre de quien habla en hangul.

| Inglés | Coreano | Nota |
|---|---|---|
| Digivice | 디지바이스 | |
| Merit, Merit points | 공적, 공적 포인트 | la tienda de cartas |
| Dimensional Key Chain | 차원 열쇠고리 | |
| Market manager | 시장 관리인 | |
| Ogremon's gang, hideout | 산적단, 아지트 | |
| Insectoid Digimon | 곤충형 디지몬 | |
| Digivolution charts | 진화 도감 | |
| Training: X | {color:5}훈련: X | los recuadros de entrenamiento |
| Normal / Bonus Try | 일반 / 보너스 도전 | |
| Card pack | 팩 | «3장 팩» |
| Master (Tyrannomon's) | 스승님 | |
| Tournament Schedule | 대회 일정 | |
| Enter (a tournament) | 참가하다 | |
| Qualifications | 참가 조건 | |
| Battle Royal | 배틀 로열 | |
| X joins the city! | X(이)가 마을에 들어왔다! | «아구몬이 마을에 들어왔다!»; en los diálogos el pueblo es «마을» |
| Begin / Explain / Quit | 시작 / 설명 / 그만두기 | las sesiones de entrenamiento |
| Slot game | 슬롯머신 | |
| Vending Machine | 자판기 | precios en cifras: «고기: 300 Bits» |
| Elevator | 엘리베이터 | |
| Come again. | 또 오세요. | las tiendas |
| See ya (option) | 안녕 | |
| Yes / No (the player's answers) | 응 / 아니 | las preguntas del propio sistema dicen 예 / 아니요 |
| Prize money | 우승 상금: 5000 Bits | tablones de torneos |

## Nombres de los Digimon

Nuestra grafía en hangul de cada nombre, según se lee el inglés (los nombres de estilo japonés, por sus sílabas).

| Inglés | Coreano |
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
