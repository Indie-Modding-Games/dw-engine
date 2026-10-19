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
antes del final, una memoria guardada fuera de los datos o una palabra final que no es cero.

- El id de la cabecera coincide con el enum `Overlay` del decomp hasta SHOP; desde DGET vale uno menos (DGET tiene el
  mismo que SHOP).
- El nombre de la cabecera no siempre tiene las mayúsculas del fichero (`DoO2_REL.BIN`, `DoOA_REL.BIN`,
  `EaB_REL.BIN`).
- DGET no tiene memoria guardada. SHOP no tiene build en el decomp: solo su cabecera dice dónde están su código y sus
  datos.
- La memoria guardada de TRN y TRN2 es todo ceros; la de los demás, restos de la memoria del build. La de MOV son los
  buffers del decodificador de vídeo (`mov_bss_us.c`).

## Tablas

Cuatro overlays tienen tablas de juego; `overlay_tables.hpp` las lee con `MemoryImage::fromOverlay` y
`overlay_yaml.hpp` las escribe en YAML, con los ids de las tablas del ejecutable para objetos, mapas, técnicas y
Digimon. Cada documento tiene su esquema en `content/schemas`.

| Overlay | Documento | Tablas |
|---|---|---|
| FISH | `fishing` | Probabilidad de cada cebo, probabilidad de picar por profundidad y tipo de pez, tipos de pez, las dos curvas de orilla de la prueba de agua y los dos mapas de pesca (zonas de cada pez, horas y ruta de la serpiente marina, sprite) |
| STD | `stadium` | Stat base de los rivales por torneo, TIM del grado de cada torneo, probabilidad de victoria por niveles, cámaras y arenas |
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

## Lo que no se sabe

- Los datos de `SHOP_REL.BIN` (`[0x7860, 0x7E0C)` en el US): empiezan con una tabla de saltos a su código y no tienen
  textos ASCII; sin build ni símbolos, el resto no tiene nombre. Probablemente es un resto de la versión japonesa.
- Los campos `unkN` de las tablas de pesca, que se escriben como `unknown_*`.
