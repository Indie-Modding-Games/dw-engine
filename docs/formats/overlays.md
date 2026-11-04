# Overlays

Los 16 `*_REL.BIN` de la raíz del disco son código y datos que el ejecutable carga enteros en una dirección fija
cuando una escena los necesita (`utils2.c: loadDynamicLibrary`, que no mira el fichero). Cómo se reparten por dentro
sale del build del decomp (`config/<ver>/<overlay>.yaml` y `overlay.ld`); `formats/exe/overlay.hpp` lo guarda por
versión, sin datos. Las direcciones de carga están en [Tablas](tablas.md).

## Estructura

| Parte | Qué es |
|---|---|
| Ficheros incrustados | Solo BTL, STD y VS: `ETCDAT/EX.TIM`, `ETCDAT/EX.TMD`, `ETCDAT/FEEL_EF.TMD`, `ETCHI/MAHI.TMD` y `ETCDAT/DEFEFF.TMD`, seguidos y sin relleno en `[0, 0x4188)` |
| Cabecera (0x40 bytes) | `"MWo1"`, un byte de id, la dirección de la propia cabecera (carga + offset), el tamaño del código, el de los datos y un nombre ASCII terminado en NUL; el resto son ceros |
| Código | Del final de la cabecera al primer `.rodata`/`.data` |
| Datos | Constantes del código y tablas |
| Memoria guardada | Las fuentes `*_bss_us`: memoria que el overlay solo usa en ejecución, guardada con lo que tenía cuando se generó el fichero |
| Palabra final | Un cero (`LONG(0)` en `overlay.ld`, o dentro del último array de la memoria guardada) |

`readOverlayHeader` comprueba la firma, los ceros y el NUL del nombre. `readOverlayParts` corta el fichero con la
cabecera y la versión, y anota lo que no cuadra: una dirección que no es carga + offset, tamaños que no acaban 4 bytes
antes del final, una memoria guardada fuera de los datos o una palabra final que no es cero. Si el layout trae un mapa
de los datos (solo SHOP), comprueba también que sus rangos cubran los datos de punta a punta y que cada uno tenga lo que
dice: las tablas de saltos apuntan al código, los punteros a los datos (o son cero) y los textos acaban en cero.

- El id de la cabecera coincide con el enum `Overlay` del decomp hasta SHOP; desde DGET vale uno menos (DGET tiene el
  mismo que SHOP).
- El nombre de la cabecera no siempre tiene las mayúsculas del fichero (`DoO2_REL.BIN`, `DoOA_REL.BIN`,
  `EaB_REL.BIN`).
- Ni DGET ni SHOP tienen memoria guardada.
- El `SHOP_REL.BIN` del disco US es el build `jp_rev1` del decomp, byte a byte (el SHA-1 de
  `config/jp_rev1/shop.yaml`). El ejecutable US enlaza su propia copia de ese código (`shop/script_menu` en
  `config/us/main.yaml`) y nunca salta al overlay, aunque `script_common.c: checkShopMap` y la op `0x23` del script
  (`script_interp.c`) todavía cargan el fichero. Sus símbolos están en `config/jp_rev1/symbols_shop.txt`.
- La memoria guardada de TRN y TRN2 es todo ceros; la de los demás, restos de la memoria del build. La de MOV son los
  buffers del decodificador de vídeo (`mov_bss_us.c`).

## Los datos de SHOP

El decomp no compila SHOP para el US, así que su layout lleva un mapa de los datos (`OverlayDataRange`), hecho a mano
desde el código del overlay: cada par `lui`/`addiu` o `lui`/`lw` que apunta a los datos y cada `jr` a través de una
tabla. Cubre `[0x7860, 0x7E0C)` entero; los nombres son los de `symbols_shop.txt`.

Tablas de saltos, `[0x7860, 0x79F8)`: cada una es la de un `switch`, con el índice acotado por un `sltiu` con su número
de casos, y todas sus entradas caen dentro de la función.

| Offset | Función | Casos |
|---|---|---|
| `0x7860` | `openRecycleShop` | 10 |
| `0x7888` | `rollCardPack` | 5 |
| `0x789C` | `openBuyCardMenu` | 5 |
| `0x78B0` | `openSellCardMenu` | 5 |
| `0x78C4` | `openMeritShop` | 14 |
| `0x78FC` | `tickItemKeeper` | 7 |
| `0x7918` | `openJukebox` | 5 |
| `0x792C` | `tickBirdraTransport` | 7 |
| `0x7948` | `openMojyamonShop` | 7 |
| `0x7964` | `newGameStateMachine` | 32 |
| `0x79E4` | `tickNamingBox` | 5 |

Datos, `[0x79F8, 0x7E0C)`:

| Rango | Qué es | Lo leen | Copia en el ejecutable |
|---|---|---|---|
| `[0x79F8, 0x7A78)` | Títulos de la gramola: fuente y pista, un byte cada una (`MAIN_D_801303B8`) | `jukeboxFillTitleList`, `tickJukeboxMenu` | `0x801303B8`, igual |
| `[0x7A78, 0x7A84)` | Los 12 objetos que pide el trueque (`MOJYAMON_ITEMS_GIVE`) | `mojyaTradeFillItemList` | `0x80130438`, igual |
| `[0x7A84, 0x7A90)` | Los 12 que da a cambio (`MOJYAMON_ITEMS_GET`) | `mojyaTradeFillItemList` | `0x80130444`, igual |
| `[0x7A90, 0x7AD4)` | Los cuatro textos del cuadro de nombre (`STR_YOUR_NAME` a `STR_BACK_OK`) | `updateNamingPreview`, `fillNamingMenuStrings` | no |
| `[0x7AD4, 0x7DB0)` | Filas de caracteres del cuadro de nombre (11 bytes y un cero cada una), en seis grupos, cada grupo seguido de su tabla de 9 punteros a filas (`CHAR_PAGE3_LEFT` a `CHAR_PAGE1_RIGHT`) | por `NAMING_CHAR_PAGES` | las filas sí, desde `0x80130450`; un byte de una fila difiere |
| `[0x7DB0, 0x7DC8)` | Las seis tablas de filas (`NAMING_CHAR_PAGES`) | `tickNamingBox`, `fillNamingMenuStrings` | no |
| `[0x7DC8, 0x7DEC)` | 18 `s16`: posiciones de los controles del cuadro (`NAMING_CTRL_BOXES`) | `renderNamingBox`, `renderSelectionBox` | no |
| `[0x7DEC, 0x7E00)` | 10 `u16`: adónde salta el cursor al salirse de cada fila (`NAMING_ROLLOVER_CHARS`) | `namingSelectionLeft`, `namingSelectionRight` | no |
| `[0x7E00, 0x7E0C)` | 6 `u16`: lo mismo en los controles (`NAMING_ROLLOVER_CTRL`) | `namingSelectionLeft`, `namingSelectionRight` | no |

Nada de esto es un asset: el ejecutable US no corre este código y tiene sus propias copias de las tablas de juego. La
de la gramola ya sale del ejecutable (`music`); las del trueque, no (ver abajo).

## Tablas

Cuatro overlays tienen tablas de juego; `overlay_tables.hpp` las lee con `MemoryImage::fromOverlay` y
`overlay_yaml.hpp` las escribe en YAML, con los ids de las tablas del ejecutable para objetos, mapas, técnicas y
Digimon. Cada documento tiene su esquema en `content/schemas`.

| Overlay | Documento | Tablas |
|---|---|---|
| FISH | `fishing` | Probabilidad de cada cebo, probabilidad de picar por profundidad y tipo de pez, tipos de pez, las dos curvas de orilla de la prueba de agua y los dos mapas de pesca (zonas de cada pez, horas y ruta de la serpiente marina, sprite) |
| STD | `stadium` | Stat base de los rivales por torneo, TIM del grado de cada torneo, probabilidad de victoria por niveles, cámaras, arenas y el cuadro del torneo (`bracket`: filas, salto, caminos de cada hueco, paleta del sprite y disparo de cada especie) |
| VS | `versus` | Cámaras y arenas |
| TRN | `training` | Animación y frame del golpe por compañero, técnicas que enseña el entrenamiento y sus pesos, tiras de la tragaperras, asistencias y puntos de entrenamiento por mapa |

- Las variables de 8 bytes o menos de esas fuentes van al `.sdata` del ejecutable, no al overlay. Por eso el número de
  objetos y de TIM de cada arena se lee del ejecutable (`STD_ARENA_COUNTS`, `VS_ARENA_MODEL_COUNTS`,
  `VS_ARENA_TIM_COUNTS`).
- Una arena sin ficheros (los tres punteros nulos) no se escribe; una con unos sí y otros no es un error.
- `FishingMapData` guarda dos punteros a funciones: la prueba de agua se escribe como el índice de la curva que recorre
  (`FISH_isFishableWater1` interpola por x, `FISH_isFishableWater2` por y); el otro es un stub de código. El número de
  zonas por mapa (6) lo fija el código.
- La tabla de picar tiene 200 bytes: 33 filas de 6 tipos desde la profundidad `0x26`; los 2 bytes que sobran se
  guardan en `unused`.
- Los puntos de entrenamiento acaban en el mapa `-1` y los de cada mapa en el stat `-1`.
- TRN2 es otro build del entrenamiento: sus tablas son las de TRN, sin las técnicas. El extractor lo comprueba y lo
  anota; si difieren, queda parcial.

No se convierten, porque son datos del código y no de juego: tablas de saltos, textos de mensajes, sprites y UV de la
interfaz, posiciones del texto de combate, constantes de efectos y cámara, los nombres de Digimon de la presentación
del torneo (`STD_INTRO_DIGIMON_NAMES`) y los recorridos del cuadro (`STD_BRACKET_PATHS`). Tampoco las del `.sdata`
del ejecutable que no son tablas de juego, ni `REINCARNATE_BABY_TYPE` (DOOA).

## Extractor

`dwx` (extractor `overlay`) da cuenta de cada parte en las notas del informe, con su rango de bytes:

- Ficheros incrustados: se comparan con el fichero suelto; si son iguales, una nota (`same bytes as ETCDAT/EX.TIM`)
  y el asset sale de ese fichero. Si difieren o falta el suelto, es un `unknown`.
- Cabecera, código (no es un asset), datos, memoria guardada y palabra final: una nota cada una.
- SHOP: una nota más con lo que hace el ejecutable con el fichero, y una por cada rango de su mapa de datos. Los rangos
  con copia en el ejecutable se comparan con ella y la nota dice si son iguales o cuántos bytes difieren.
- Las tablas de FISH, STD, VS, TRN y BTL: `data/fishing`, `data/stadium`, `data/versus`, `data/training` y
  `data/battle` (el dibujo del disparo del aplastado de cada especie).
- Son `unknowns`: lo que `readOverlayParts` anota (también los rangos del mapa de datos que no cuadran), un overlay
  sin layout para la versión, un rango con copia que no se pudo comparar (sin ejecutable) y las tablas que no se
  pudieron leer.

## Lo que no se sabe

- Las tablas del trueque (`MOJYAMON_ITEMS_GIVE` y `MOJYAMON_ITEMS_GET`) no se convierten a YAML desde el ejecutable,
  que es donde las lee el US.
- Los campos `unkN` de las tablas de pesca, que se escriben como `unknown_*`.
