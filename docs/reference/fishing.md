# Pesca

La pesca del original: en dos mapas de agua el jugador saca la caña desde el menú de pausa, pone un cebo, lanza, espera
a que pique, tira a tiempo y recoge el pez peleando contra la tensión del sedal. Lo que pesca va a la bolsa.

## Qué hace

- El menú de pausa ofrece **Pescar** solo en un mapa con pesca; sale en gris si falta la caña o comida para cebar
  (`main/fish.c: getFishingAvailability`, 0/1/2) **(comprobado)**. La mejor caña manda: el disparador 46 da la caña
  buena y el 45 la vieja (`getBestFishingRod`) **(comprobado)**. Cebo es cualquier objeto de la bolsa con categoría de
  orden 2, la comida (`hasFoodItem`) **(comprobado)**.
- Mientras se pesca, el reloj del juego se para (`clock.c: tickGameClock` no avanza con `isFishing`) **(comprobado)**.
- Al sacar la caña se elige el cebo; sin cebo se guarda la caña. Se apunta girando, se carga el lanzamiento con
  Confirmar y se suelta; la fuerza (los fotogramas cargados más 5, hasta 20) da la velocidad del corcho, entre 100 y
  200 con la caña vieja o 400 con la buena (`FISH_tickRod`) **(comprobado)**.
- Los peces nadan en zonas de la pantalla del mapa (polígonos sobre el fondo prerrenderizado) y solo en el agua, que es
  la parte de la pantalla a un lado de una línea de orilla (`FISH_isFishableWater1` y `2`) **(comprobado)**.

## Reglas

- **Población**: hasta 33 peces; cada 20 fotogramas, si hay menos de 32, nace uno de la especie con menos peces
  respecto a su peso en el mapa (`FISH_tickFishPopulation`) **(comprobado)**. Viven entre 600 y 1200 fotogramas; tras
  asustarse (un chapoteo cerca) no nace ninguno en 30 **(comprobado)**. El original divide entre el total sin mirar
  si es 0; el motor lo evita **(comprobado)**.
- **Picada**: cada 20 fotogramas, un pez a menos de 200 del corcho tira su probabilidad según cebo y especie
  (`FISH_findFishToBite`); el cebo que le gusta a una especie la atrae desde 600 **(comprobado)**.
- **Anzuelo**: un pez de menos de 150 mordisquea entre 20 y 40 fotogramas y tira durante 30; uno mayor, entre 40 y 60
  y 60. Hay que tirar en la primera mitad del tirón (`FISH_tryStrike`); antes es «demasiado pronto», después
  «demasiado tarde» y se lleva el cebo **(comprobado)**. La fuerza del pez es su tamaño más 150, y hasta 100 más
  **(comprobado)**.
- **Tensión**: el límite es 5000 con la caña vieja y 10000 con la buena. Sin Confirmar baja 166 por fotograma; con
  Confirmar sube de 71 a 125 según el tamaño (de 50 a 300), y de 166 a 500 si el pez corre; si el pez corre sin
  Confirmar sube de 41 a 83 (`FISH_tickHookedFish`) **(comprobado)**. En −límite el pez se escapa; en +límite
  aguantado unos fotogramas (de 15 a 5 según el tamaño) se rompe el sedal **(comprobado)**.
- **Enganches**: con el cebo comido y Confirmar pulsado, desde el fotograma 31 y cada 20, 1 entre 10 engancha algo de
  una tabla de objetos (`FISH_tickRod`) **(comprobado)**.
- **Captura**: el pez da su objeto; si la bolsa está llena se suelta. Se guarda el récord de tamaño por especie y el
  partner suma una pesca (`PARTNER_PARA.fishCaught`; la medalla 9 llega con 100, `tamer.c`) **(comprobado)**. El
  récord no lo lee nada más (`newRecord` se escribe y no se usa) **(comprobado)**.
- **Serpiente marina**: en un mapa, a ciertas horas, puede aparecer una serpiente marina que pica con un 20 % cada 8
  fotogramas; al sacarla la pesca se apaga en el mapa y se lanza la escena de la sección 0x4e4 de MAPHEAD
  (`triggerSeadramonCutscene`) **(comprobado)**.

## Datos

`dwx` extrae las tablas del ejecutable a `data/fishing` (`content/schemas/fishing.schema.json`): las especies
(`FISH_KIND_INFO`: objeto, tamaño, pausas, cebo que la atrae), las probabilidades por cebo y especie
(`FISH_BITE_CHANCE`), los enganches (`FISH_BAIT_CHANCES`), las líneas de orilla y los mapas (`FISHING_MAP_DATA`:
zonas, horas de la serpiente, alcance del lanzamiento, ruta de la serpiente). Las constantes de las reglas están en la
sección `fishing` del ruleset ([auditoría](../legal/auditoria-rulesets.md)).

## Rarezas

- `FISH_REEL_TARGET` no se escribe nunca: un pez que se recoge nada hacia el origen del mapa **(comprobado)**.
- Las zonas y el agua están en píxeles de la pantalla del fondo, no en el mundo: con otra cámara cambian de sitio
  **(comprobado)**.

## Dónde está en el decomp

- `src/main/fish.c`: disponibilidad, caña, carga del overlay por mapa, el tic del jugador y la serpiente.
- `src/fish/fish.c`: `FISH_tickRod` (la caña), `FISH_tickHookedFish` (el pez enganchado), `FISH_tickFishPopulation`
  y `FISH_tickFish` (el agua), `FISH_updateRodPose` y `FISH_getRodTipPos` (la punta de la caña).

## En el motor

`packages/game/fishing` (reglas, agua y caña), `session::FishingAdapter` (tablas, caña, cebo, capturas, récords y el contador
del partner) y la pantalla de pesca de `apps/game`, con su cámara. La caña, el corcho, el sedal, los peces, las ondas
y las marcas de picada se dibujan como el original, y al sacar la serpiente la historia corre su escena (sección
0x4e4 del `MAPHEAD`).
