# Glosario de la traducción al portugués de Brasil

Los términos que se repiten en la traducción al portugués de Brasil (`pt`) del juego: los textos del motor
(`content/packs/game/localization/pt/`) y los textos del original (`content/packs/pt-translation/text/pt/`). Cómo se
escribe una traducción y cómo se mantiene al día está en [Traducir los textos del original](translate.md); esta página
sigue el patrón del [glosario del español](spanish-glossary.md). Un término nuevo que se repite se añade aquí con la
traducción que lo usa por primera vez.

## Decisiones

1. **Portugués de Brasil**, cercano y sencillo como el original. Al jugador se le habla de **«você»** (la forma
   informal de todos los días en Brasil), nunca «tu» con sus formas verbales ni «o senhor».
2. Los nombres de objetos, técnicas, lugares, torneos y medallas se traducen con palabras propias; no se toma nada de
   otras traducciones ni doblajes de la serie.
3. **Los nombres de los Digimon quedan en inglés**, letra por letra, con sus erratas, como en español
   (`kept_english`). Dentro de una frase llevan el artículo que pide el portugués: «a casa do Jijimon», «o Agumon».
4. `HP`, `MP`, la moneda `Bits`, las letras de clase de los torneos, `Overdell` y las marcas `GOOD`/`BAD` del curling
   quedan como en el original.
5. Las etapas: **Bebê, Treinamento, Novato, Campeão, Ultra**.
6. Las pantallas que el original parte en columnas por píxel usan sus propias claves de localización en el motor; sus
   ranuras de `main` se traducen columna por columna solo como respaldo (igual que en español).
7. Cuando el inglés del original está roto, se traduce la intención y se conservan todos los códigos de control.
8. Los nombres de cosas caben en unos 12–14 caracteres (nombres de objetos), los nombres de mapa van centrados en 24
   caracteres, las líneas de la historia tienen como mucho 30 caracteres y 4 líneas por caja. Si un texto no cabe se
   acorta como lo haría un localizador brasileño («Cogum.», «Mte.»), nunca cortando una palabra.
9. Género: cuando el sujeto es cualquier Digimon, una redacción neutra que sirva para ambos («se machucou», «ficou
   doente»).

## Juego y crianza

| Inglés | Portugués | Nota |
|---|---|---|
| Digimon | Digimon | igual en plural |
| Partner | parceiro | |
| Tamer | domador | «nível de domador» |
| Digivolve | digievoluir | «Digievolua para `species_i`!» |
| Fresh / In-Training / Rookie / Champion / Ultimate | Bebê / Treinamento / Novato / Campeão / Ultra | etapas |
| Digi-Egg / Fresh / In-Training / Rookie / Champion / Ultimate / Mega / Ultra | Digiovo / Bebê I / Bebê II / Infantil / Maduro / Perfeito / Supremo / Super Supremo | etapas canónicas (opción `canonical_stages`) |
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
| Potty | banheiro, penico | «ir ao banheiro»; el objeto es «Penico port.» |
| Poop | cocô | |
| Rest / sleep | descanso, dormir | |
| Training | treino | «treino de Ataque» |
| Medal | medalha | |
| Card | carta | las cartas Digimon |
| Bits | Bits | la moneda |

## Atributos

| Inglés | Portugués | Abreviatura |
|---|---|---|
| HP / MP | HP / MP | |
| Offense | Ataque | Atq |
| Defense | Defesa | Def |
| Speed | Velocidade | Vel |
| Brains | Inteligência | Int |

## Batalla y técnicas

| Inglés | Portugués | Nota |
|---|---|---|
| Technique, Tech | técnica, téc. | |
| Mastered / Unmastered | dominada / não dominada | |
| Battle | batalha | |
| Run | Fugir / Fuja! | orden / grito |
| Attack | Atacar / Ataque! | |
| Your Call, Auto | Do seu jeito, Automático | |
| Moderate | Moderado / Com calma! | |
| Defensive | Defensiva / Defenda! | |
| Change target | Troque de alvo! | «Outro alvo!» si no cabe |
| Go all the way | Com tudo! | |
| Hang in there | Aguente! | |
| Finisher (special move), Finish Gauge | golpe final, medidor final | |
| Set technique | Definir técnica | |
| New orders! | Novas ordens! | |
| dropped / got hurt / learned! | soltou / se machucou / aprendida! | caja de fin de batalla |

Algunos nombres de técnica: Fire Tower → Torre de fogo, Spit Fire → Cospe-fogo, Wind Cutter → Corta-vento, Pepper Breath →
Bafo picante, Party time → Hora da festa, Bubble → Bolha. La lista completa es `move_names` en `main.yaml`.

## Objetos

| Inglés | Portugués | Nota |
|---|---|---|
| Item | item, itens | la palabra habitual en los juegos brasileños |
| Use / Move / Sort / Drop | Usar / Mover / Ordenar / Largar | inventario |
| Recovery | recuperação, recup. | |
| Floppy, Disk | disquete, disco | |
| Chip | chip | |
| Mushroom | cogumelo, cogum. | |
| Berry | baga | |
| Meat | carne | |
| Bait | isca | pesca |
| Fishing rod | vara (de pescar) | |
| Key | chave | |
| Bandage | atadura | |
| Hook / line / tension gauge | anzol / linha / barra de tensão | pesca |

## Lugares

| Inglés | Portugués |
|---|---|
| File City | Cidade File |
| Mt. | Monte (Mte. si no cabe) |
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

## Torneos

| Inglés | Portugués |
|---|---|
| Tournament | torneio |
| Grade D… | Classe D… |
| Cup / Version 1 Cup | Copa / Copa Versão 1 |
| Fire / Grapple / Thunder Wind / Cool / Nature / Metalic / Filth | Fogo / Luta / Trovão-Vento / Frio / Natureza / Metal / Sujeira |
| Dino / Wing / Animal / Human / Beetle | Dino / Asa / Animal / Humana / Besouro |

## Menús y sistema

| Inglés | Portugués |
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
| Complementos (lanzador) | Complementos |
| Saves | jogos salvos |
| Press X | Aperte X |
| Game mode / variant | Modo de jogo / variante |
| Classic Enhanced / Folder Mode | Clássico Aprimorado / Modo Folder |
| Fixed in {variant} (opción fijada) | Fixa em {variant} |
| Open to new players (co-op) | Aberto a novos jogadores |
| Player (nombre en línea por defecto) | Jogador |
| Bits (etiqueta de la moneda en los menús) | Bits |
| Naming page of accented letters | ÁÂÃ (Á Â Ã À Ç É Ê Í Ó Ô Õ Ú) |
| The game's log (donde van los detalles de un error) | registro do jogo |
| Unknown variant (modo de una partida que no está instalado) | Variante desconhecida |
| Key caps of the classic prompts (`prompt.cap.*`) | como en el teclado, en inglés: ESC, DEL, CTRL, SHIFT… |

## Historia

La línea del que habla (`{color:7}<nombre> {color:1}\n`) se copia igual y los nombres de los personajes quedan en inglés.

| Inglés | Portugués | Nota |
|---|---|---|
| Digivice | Digivice | |
| Merit points | pontos de mérito | la tienda de cartas |
| Tournament Schedule | Calendário de torneios | |
| Enter (a tournament) | inscrever-se | |
| Battle Royal | batalha real | |
| X joins the city! / the Arena! | X entrou na cidade! / na Arena! | |
| Begin / Explain / Quit | Começar / Explicar / Sair | las sesiones de entrenamiento |
| Explain again / Bonus Try | Explicar de novo / Tentativa bônus | |
| Training heading | {color:5}Treino: … | Golpes, Golpe especial, Esteira, Cachoeira, Empurrar rocha, Mover pedra, Corrida |
| Slot game | Caça-níqueis | |
| Vending Machine | máquina de venda | precios en cifras: «Carne: 300 Bits» |
| Elevator | elevador | |
| Market manager | Gerente do mercado | también como nombre del que habla |
| Elevator choices | Subir / Descer / Terraço / Não entrar | |
| Ogre mode | Modo Ogro: LIGADO / DESLIGADO | |
| Dimensional Key Chain | Chaveiro Dimensional | |
| Lake Guardian | Guardião do Lago | |
| Glacial / Speedy Time Zone | Zona do Tempo Glacial / Veloz | |
| Mystery Circles / UFO | Círculos Misteriosos / OVNI | |
| Digimon Card Shop, card pack | Loja de Cartas Digimon, pacote | «carta de X» |
| Data / Vaccine / Virus | Dados / Vacina / Vírus | tipos de Digimon |
| Insectoid | Digimon Inseto | |
| Runner-up | vice | premios de torneo: «Prêmio de vice :» |
| Tamer Level went up / down | O nível de domador subiu! / caiu! | |
| Net World / File Island | Mundo da Rede / Ilha File | |
| Clinic / Fortune Teller / Treasure Hunter | Clínica / Vidente / Caçador de tesouros | |
| Shell Newspaper | Jornal Concha | |
| Meat Farm / Training Camp | Fazenda de Carne / Campo de Treino | |
| Shop job | Trabalho na loja | la tienda de Monochromon |
| Come again. | Volte sempre. | las tiendas |
