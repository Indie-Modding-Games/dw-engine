# Brazilian Portuguese translation glossary

The terms that repeat in the Brazilian Portuguese (`pt`) translation of the game: the engine's texts
(`content/packs/game/localization/pt/`) and the original's texts (`content/packs/pt-translation/text/pt/`). How a
translation is written and kept up to date is in [Translating the original's texts](translate.md); this page follows
the pattern of the [Spanish glossary](spanish-glossary.md). A new term that repeats is added here with the
translation that first uses it.

## Decisions

1. **Brazilian Portuguese**, friendly and simple like the original. The player is addressed with **«você»** (the
   everyday informal form in Brazil), never «tu» with its verb forms nor «o senhor».
2. Item names, technique names, places, tournaments and medals are translated, with words of our own; nothing is taken
   from other translations or dubs of the series.
3. **Digimon names stay in English**, letter by letter, typos included, as in Spanish (`kept_english`). Inside a
   sentence they take the article Portuguese asks for: «a casa do Jijimon», «o Agumon».
4. `HP`, `MP`, the currency `Bits`, the tournament grade letters, `Overdell` and the curling's `GOOD`/`BAD` stay as
   in the original.
5. The stages: **Bebê, Treinamento, Novato, Campeão, Ultra**.
6. The screens the original splits into columns by pixel use their own localization keys in the engine; their slots
   in `main` are translated column by column only as a fallback (same as Spanish).
7. When the original's English is broken, the intent is translated and every control code is kept.
8. Names of things fit in about 12–14 characters (item names), map names are centered in 24 characters, story lines
   are at most 30 characters, 4 lines per box. When a text would not fit, it is shortened the way a Brazilian
   localizer would («Cogum.», «Mte.»), never cut mid-word.
9. Gender: when the subject is any Digimon, neutral wording that works for both («se machucou», «ficou doente»).

## Game and raising

| English | Portuguese | Note |
|---|---|---|
| Digimon | Digimon | unchanged in the plural |
| Partner | parceiro | |
| Tamer | domador | «nível de domador» |
| Digivolve | digievoluir | «Digievolua para `species_i`!» |
| Fresh / In-Training / Rookie / Champion / Ultimate | Bebê / Treinamento / Novato / Campeão / Ultra | stages |
| Digi-Egg / Fresh / In-Training / Rookie / Champion / Ultimate / Mega / Ultra | Digiovo / Bebê I / Bebê II / Infantil / Maduro / Perfeito / Supremo / Super Supremo | canonical stages (option `canonical_stages`) |
| Raise | criar, criação | |
| Full (hunger) | saciar, satisfeito | |
| Tiredness | cansaço | |
| Discipline (Disc.) | disciplina (disc.) | |
| Happiness | felicidade | |
| Weight | peso | |
| Life span | vida | |
| Virus | vírus | |
| Sick, sickness | doente, doença | «ficou doente» |
| Wound, injured | ferida, machucado | «se machucou» |
| Status errors | estados alterados | |
| Poison / Confusion / Stun / Flat | veneno / confusão / atordoado / achatado | |
| Potty | banheiro, penico | «ir ao banheiro»; the item is «Penico port.» |
| Poop | cocô | |
| Rest / sleep | descanso, dormir | |
| Training | treino | «treino de Ataque» |
| Medal | medalha | |
| Card | carta | the Digimon cards |
| Bits | Bits | the currency |

## Attributes

| English | Portuguese | Abbreviation |
|---|---|---|
| HP / MP | HP / MP | |
| Offense | Ataque | Atq |
| Defense | Defesa | Def |
| Speed | Velocidade | Vel |
| Brains | Inteligência | Int |

## Battle and techniques

| English | Portuguese | Note |
|---|---|---|
| Technique, Tech | técnica, téc. | |
| Mastered / Unmastered | dominada / não dominada | |
| Battle | batalha | |
| Run | Fugir / Fuja! | order / shout |
| Attack | Atacar / Ataque! | |
| Your Call, Auto | Do seu jeito, Automático | |
| Moderate | Moderado / Com calma! | |
| Defensive | Defensiva / Defenda! | |
| Change target | Troque de alvo! | «Outro alvo!» if it does not fit |
| Go all the way | Com tudo! | |
| Hang in there | Aguente! | |
| Finisher (special move), Finish Gauge | golpe final, medidor final | |
| Set technique | Definir técnica | |
| New orders! | Novas ordens! | |
| dropped / got hurt / learned! | soltou / se machucou / aprendida! | end-of-battle box |

Some technique names: Fire Tower → Torre de fogo, Spit Fire → Cospe-fogo, Wind Cutter → Corta-vento, Pepper Breath →
Bafo picante, Party time → Hora da festa, Bubble → Bolha. The full list is `move_names` in `main.yaml`.

## Items

| English | Portuguese | Note |
|---|---|---|
| Item | item, itens | the usual word in Brazilian games |
| Use / Move / Sort / Drop | Usar / Mover / Ordenar / Largar | inventory |
| Recovery | recuperação, recup. | |
| Floppy, Disk | disquete, disco | |
| Chip | chip | |
| Mushroom | cogumelo, cogum. | |
| Berry | baga | |
| Meat | carne | |
| Bait | isca | fishing |
| Fishing rod | vara (de pescar) | |
| Key | chave | |
| Bandage | atadura | |
| Hook / line / tension gauge | anzol / linha / barra de tensão | fishing |

## Places

| English | Portuguese |
|---|---|
| File City | Cidade File |
| Mt. | Monte (Mte. if it does not fit) |
| Drill Tunnel | Túnel Broca |
| Great Canyon | Grande Cânion |
| Freezeland | Terra Gelada |
| Gear Savanna | Savana Engrenagem |
| Toy Town | Cidade Brinquedo |
| Factorial Town | Cidade Fatorial |
| Volume Villa | Vila Volume |
| Beetle Land | Terra dos Besouros |
| Trash Mountain | Montanha de Lixo |
| Grey Lord's Mansion | Mansão do Lorde Cinza |
| Misty Trees | Árvores Nebulosas |
| Ancient Dino Region | Região Dino Antiga |
| Native Forest | Floresta Nativa |
| Item Shop / Item Keeper | Loja de Itens / Guarda-Itens |
| Arena | Arena |

## Tournaments

| English | Portuguese |
|---|---|
| Tournament | torneio |
| Grade D… | Classe D… |
| Cup / Version 1 Cup | Copa / Copa Versão 1 |
| Fire / Grapple / Thunder Wind / Cool / Nature / Metalic / Filth | Fogo / Luta / Trovão-Vento / Frio / Natureza / Metal / Sujeira |
| Dino / Wing / Animal / Human / Beetle | Dino / Asa / Animal / Humana / Besouro |

## Menus and system

| English | Portuguese |
|---|---|
| New game / Continue | Novo jogo / Continuar |
| Save / Load / Delete / Copy | Salvar / Carregar / Apagar / Copiar |
| Save file | arquivo de salvamento, jogo salvo |
| Memory Card | cartão de memória |
| Slot | espaço |
| Select / Cancel / Back | Escolher / Cancelar / Voltar |
| Yes / No | Sim / Não |
| Pause | Pausa |
| Options, Settings | Opções |
| Name | Nome |
| Player | jogador |
| Skin | visual |
| Co-op, online room | cooperativo, sala |
| Host / Guest | Anfitrião / Convidado |
| Multiplayer | Multijogador |
| Pad, gamepad | controle |
| Tab | Aba |
| Scold / Praise | Dar bronca / Elogiar |
| Evolution chart | tabela de evolução |
| Add-ons (launcher) | Complementos |
| Saves | jogos salvos |
| Press X | Aperte X |
| Game mode / variant | Modo de jogo / variante |
| Classic Enhanced / Folder Mode | Clássico Aprimorado / Modo Folder |
| Fixed in {variant} (locked option) | Fixa em {variant} |
| Open to new players (co-op) | Aberto a novos jogadores |
| Player (default online name) | Jogador |
| Bits (currency label in menus) | Bits |
| Naming page of accented letters | ÁÂÃ (Á Â Ã À Ç É Ê Í Ó Ô Õ Ú) |
| The game's log (where an error's details go) | registro do jogo |
| Unknown variant (a save's mode that is not installed) | Variante desconhecida |
| Key caps of the classic prompts (`prompt.cap.*`) | as on the keyboard, in English: ESC, DEL, CTRL, SHIFT… |

## Story

The speaker's line (`{color:7}<name> {color:1}\n`) is copied the same and character names stay in English.

| English | Portuguese | Note |
|---|---|---|
| Digivice | Digivice | |
| Merit points | pontos de mérito | the card shop |
| Tournament Schedule | Calendário de torneios | |
| Enter (a tournament) | inscrever-se | |
| Battle Royal | batalha real | |
| X joins the city! / the Arena! | X entrou na cidade! / na Arena! | |
| Begin / Explain / Quit | Começar / Explicar / Sair | the training sessions |
| Explain again / Bonus Try | Explicar de novo / Tentativa bônus | |
| Training heading | {color:5}Treino: … | Golpes, Golpe especial, Esteira, Cachoeira, Empurrar rocha, Mover pedra, Corrida |
| Slot game | Caça-níqueis | |
| Vending Machine | máquina de venda | prices in figures: «Carne: 300 Bits» |
| Elevator | elevador | |
| Market manager | Gerente do mercado | also the speaker's name |
| Elevator choices | Subir / Descer / Terraço / Não entrar | |
| Ogre mode | Modo Ogro: LIGADO / DESLIGADO | |
| Dimensional Key Chain | Chaveiro Dimensional | |
| Lake Guardian | Guardião do Lago | |
| Glacial / Speedy Time Zone | Zona do Tempo Glacial / Veloz | |
| Mystery Circles / UFO | Círculos Misteriosos / OVNI | |
| Digimon Card Shop, card pack | Loja de Cartas Digimon, pacote | «carta de X» |
| Data / Vaccine / Virus | Dados / Vacina / Vírus | Digimon types |
| Insectoid | Digimon Inseto | |
| Runner-up | vice | tournament rewards: «Prêmio de vice :» |
| Tamer Level went up / down | O nível de domador subiu! / caiu! | |
| Net World / File Island | Mundo da Rede / Ilha File | |
| Clinic / Fortune Teller / Treasure Hunter | Clínica / Vidente / Caçador de tesouros | |
| Shell Newspaper | Jornal Concha | |
| Meat Farm / Training Camp | Fazenda de Carne / Campo de Treino | |
| Shop job | Trabalho na loja | Monochromon's shop |
| Come again. | Volte sempre. | the shops |
