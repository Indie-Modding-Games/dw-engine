# Modelos y animaciones

Cómo lee `formats/model` los modelos del original (TMD), los paquetes de modelo con animaciones (MMD), los esqueletos
y las animaciones, y cómo los escribe en glTF. Las referencias son a ficheros y funciones del decomp (`dw_decomp`).

Todos los lectores reciben un `std::span<const std::byte>` y leen con `ByteReader`
([Disco](disco.md#lectura-de-bytes)): cada offset, contador e índice se comprueba contra el buffer, y un fichero
malformado da un error con el motivo.

## TMD

El formato de modelos de PsyQ (`TMDModel` en `include/dw/model.h`). `readTmd` devuelve un `Tmd`.

| Offset | Campo | Notas |
|---|---|---|
| 0x0 | `u32 id` | Siempre `0x41` |
| 0x4 | `u32 flags` | Bit 0: offsets absolutos. Todos los TMD del original tienen 0 (relativos); con 1 es un error |
| 0x8 | `u32 nobj` | Número de objetos |
| 0xC | objetos | `nobj` cabeceras de 28 bytes |

Cada objeto: `u32 vert_top, n_vert, normal_top, n_normal, primitive_top, n_primitive; s32 scale`. Los `*_top` son
offsets desde el inicio de la tabla de objetos (0xC). El original los convierte en punteros con `GsMapModelingData`.
`scale` se guarda pero no se usa: libgs lo ignora. Vértices y normales son `SVECTOR` (3 × `s16` y relleno, 8 bytes);
las normales miden 4096.

### Primitivas

Cada primitiva empieza con una palabra `olen | ilen << 8 | flag << 16 | mode << 24`, seguida de `ilen` palabras.

- `mode` = `001 IIP QUAD TME ABE TGE` para polígonos: IIP = una normal o un color por vértice, QUAD = cuatro vértices,
  TME = con textura, ABE = semitransparente, TGE = sin sombreado de textura.
- `flag`: bit 0 = sin luz (LGT), bit 1 = dos caras (FCE), bit 2 = un color por vértice en polígonos con luz y sin
  textura (GRD).

El paquete tiene, en este orden:

1. Con textura, una palabra por vértice: `u v` y en la mitad alta `CBA` (primer vértice), `TSB` (segundo) o relleno.
2. Colores `r g b`: ninguno si tiene luz y textura; uno, o uno por vértice si tiene GRD (con luz) o IIP (sin luz).
3. Índices: con luz y gouraud, una palabra por vértice `normal | vértice << 16`; con luz y plano, `normal | v0 << 16`
   y luego los demás vértices de dos en dos; sin luz, los vértices de dos en dos, sin normales.

El lector decodifica todas las combinaciones de polígono (es lo que registra `graphics.c: initializeGsTMDMap` y algo
más). Una línea, un sprite o un paquete cuyo `ilen` no cuadra con su modo se salta y se apunta en `Tmd::issues`, con
el objeto, la primitiva, el modo, el flag y la longitud.

`CBA` es la posición de la paleta en VRAM (x / 16 en los bits 0-5, y en los bits 6-14). `TSB` es la página de textura
(x / 64 en los bits 0-3, y / 256 en el bit 4, modo de semitransparencia en los bits 5-6, profundidad de color en los
bits 7-8).

### Normales fuera del objeto

libgs indexa las normales sin mirar `n_normal`: `GsSortObject4J` pasa cada primitiva a su función `GsTMDfast*`
(`graphics.c` las registra en `GsFCALL4`, por ejemplo `GsTMDfastTG3L` para los triángulos con textura, gouraud y luz),
y esa función lee la normal en `normal_top + 8 · índice`. Un índice de vértice fuera de `n_vert` sí es un error.

Un modelo del original, `CHDAT/MMD1/TANE.MMD`, tiene triángulos con luz (modo `0x34`, flag 0, así que la consola sí
lee sus normales) que usan más normales de las que dice su objeto: el objeto 0 dice 22 y usa 49, y el 1 dice 22 y usa
29. El fichero guarda las normales de todos los objetos seguidas, así que las que sobran son normales de los objetos 1
y 2: vectores unitarios bien formados, pero de otras caras. Comparadas con la normal de la cara que las usa, las
propias apuntan casi igual y las prestadas no tienen relación con ella. Es un fallo del modelo original, y la consola
lo dibuja con esa luz.

El lector hace lo mismo que la consola: lee las normales de más desde donde caen, siempre que estén dentro del
fichero.

- Si cada una es una normal entera de otro objeto, lo apunta en `Tmd::notes` (el extractor lo pasa a `notes`): el
  modelo se entiende y se convierte entero, con la luz del original.
- Si alguna cae en bytes que no son normales de ningún objeto, lo apunta en `Tmd::issues` (y el fichero queda
  parcial).

### Cara delantera

Las caras delanteras del original van en sentido horario (comprobado con las normales de todos los MMD del disco). glTF
usa el antihorario, así que el escritor invierte el orden. Un cuadrilátero son los triángulos (0, 1, 2) y (1, 3, 2).

## MMD

`CHDAT\MMDn\XXXX.MMD`: un modelo con sus animaciones (`model.c: loadMMD`). La carpeta es `tipo / 30` y el nombre de
cuatro letras sale de `PTR_DIGIMON_FILE_NAMES`. `readMmd` devuelve un `Mmd`.

| Offset | Campo |
|---|---|
| 0x0 | `u32 tmdOffset` (8 en todos) |
| 0x4 | `u32 animTableOffset` |
| `tmdOffset` | El TMD, hasta `animTableOffset` |
| `animTableOffset` | Tabla de `u32`: offset de cada animación desde el inicio de la tabla; 0 = sin animación |

La tabla no dice cuántas entradas tiene: termina donde empieza el primer flujo, así que la cuenta es el menor offset
distinto de 0 dividido entre 4. El índice en la tabla es el id de la animación que usan los scripts (`startAnimation`).
Cada offset tiene que caer dentro del fichero, después de la tabla y en una dirección par; si todas las entradas son 0
hasta el final del fichero, es un error.

La textura no está en el MMD: sale de `CHDAT\ALLTIM.TIM`, un TIM de 4 bits por tipo
([formatos](../architecture/formats.md#el-disco)). El original la sube a otro sitio de la VRAM según el hueco
(jugador, compañero o NPC) y reescribe la página y la paleta de cada primitiva al cargarla
(`graphics.c: updateTMDTextureData`), pero los ficheros ya apuntan a la posición propia del TIM: página 5, que es su
`(320, 0)`, y paletas desde `(0, 488)`. Por eso el extractor no tiene que corregir nada: sube el TIM a su sitio y
cada primitiva encuentra sus texels y su paleta en él (ver [glTF](#texturas)).

## Esqueletos

Los esqueletos están en el ejecutable (`DIGIMON_SKELETONS`), un array de `SkeletonBone { s8 objIndex; s8 parentIndex; }`
por tipo, con tantos huesos como `DIGIMON_DATA[tipo].boneCount` y sin terminador. `objIndex = -1` es un hueso sin
malla; `parentIndex = -1`, una raíz. El hueso 0 es la raíz de la entidad y el 1 la del cuerpo
(`model.c: initializeDigimonObject`).

`formats/model` no lee el ejecutable: recibe el esqueleto como parámetro. `validateSkeleton` comprueba que haya algún
hueso, que cada padre vaya antes que sus hijos y que cada objeto exista en el TMD.

## Animaciones

`simulateAnimation` reproduce el flujo de una animación exactamente como el original (`anim.c: startAnimation`,
`tickAnimation`, `tickMomentum`, `animHandleKeyFrameInstruction`, `animReadMomentumInstruction`, `animApplyMomentum`,
`applyRootMomentum`) y guarda la pose de cada hueso después de cada tick. No interpola por su cuenta: simula.

### Flujo

Palabras `s16` little-endian:

1. Cabecera: bit 15 = hay escala, bits 0-14 = `frameCount`.
2. Pose inicial de los huesos 1 a `boneCount - 1` (el 0 no): escala XYZ (solo si hay escala; si no, 4096), rotación
   XYZ y posición XYZ.
3. Instrucciones: el tipo en los bits 12-15 y el frame en que se ejecuta en los bits 0-11.

| Tipo | Palabras | Qué hace |
|---|---|---|
| `0x0` keyframe | entradas mientras la palabra tenga el bit 15 | Cada entrada: `hueso` (bits 0-5) y máscara de canales (bit 14 = canal 0 … bit 6 = canal 8), un divisor y un valor por canal marcado |
| `0x1` inicio de bucle | — | Cuenta en los bits 0-7 (`0xFF` = para siempre); el bucle vuelve a la palabra siguiente |
| `0x2` fin de bucle | frame destino | Resta uno a la cuenta (salvo `0xFF`); si llega a 0 sigue, si no salta al frame destino y al inicio del bucle |
| `0x3` mover textura | origen, tamaño, destino | `x << 8 \| y` y `ancho << 8 \| alto` desde el origen de la textura del modelo; x y ancho en halfwords de VRAM |
| `0x4` sonido | `banco << 8 \| sonido` | El banco 4 es el propio de la entidad (`vabId`); solo suena si la entidad está en pantalla |

Los canales son: 0-2 escala, 3-5 rotación y 6-8 posición. Un valor no es una posición final sino una velocidad:
`delta = valor / divisor` por tick, y el resto (`valor % divisor`, con el signo del valor) se reparte con un contador
que suma una unidad cada vez que se agota. La velocidad dura hasta que otra instrucción la cambia.

### Un tick

1. Cada canal con velocidad da su paso. La rotación es un `s16` y da la vuelta.
2. Si la palabra actual tiene el bit 12, se toma como inicio de bucle. El original mira un solo bit: un `0x3` en esa
   posición se tomaría también por uno; en los flujos del disco no pasa nunca.
3. Se ejecutan las instrucciones mientras el frame actual sea el de la siguiente. La primera se ejecuta en el frame 1
   diga lo que diga su frame.
4. Si el frame es `frameCount`, la animación termina; si no, avanza.

Un keyframe en el frame N mueve la pose desde el tick siguiente. El original da un tick por vuelta del bucle principal,
que espera tres VSync: 20 ticks por segundo (`AnimationTicksPerSecond`).

### Huesos y movimiento raíz

El hueso 0 empieza en la identidad (en el juego es la posición y orientación de la entidad). Sus canales de escala y
rotación lo afectan a él; los de posición son movimiento raíz: cada paso se gira con la rotación y la escala del hueso
0 y se suma a la posición de la entidad. En las animaciones `0x23` y `0x24` se ignora el eje X. El original lo hace en
coma fija con la GTE; aquí se hace en `double`, así que puede variar en fracciones de unidad. El resultado va en
`Animation::rootMotion`.

### Bucles y límites

- Un bucle con cuenta se reproduce entero, tantas veces como dice.
- Un bucle para siempre (`0xFF`, o una cuenta 0 que al restar da `0xFF`) se reproduce una vez: la simulación termina
  en su primer salto atrás y marca `endsInLoop`.
- Cada bucle que salta queda en `Animation::loops` con `firstTick` (el tick que repite el salto) y `lastTick`.
- Los sonidos y los movimientos de textura quedan como eventos con su tick.

Es un error: un hueso fuera del esqueleto, un divisor 0, un tipo de instrucción que no existe (el original se quedaría
colgado), un fin de bucle sin inicio, leer fuera del fichero, más de `MaxAnimationTicks` (32768) ticks o más de 65536
instrucciones en un tick. Los bytes que siguen a la última instrucción ejecutada no son parte del formato.

### Unidades

| Qué | Unidad |
|---|---|
| Escala | 4096 = 1.0 |
| Rotación | 4096 = 360°; matriz `Rx·Ry·Rz` (`RotMatrix`) |
| Posición | Unidades del modelo (100 = una casilla de colisión) |
| Matriz local | `[R·S \| posición]`; la del mundo es la del padre por la local |

La textura animada por código de algunos tipos (`anim.c: animateEntityTexture`) no está en el flujo: las llamas de `species_b`, `species_c`, `species_d`, `species_e`, `species_f`, `species_g`, `species_h` y las especies enemigas gemelas (mera, emer, blmr, bird, sabd, ebir, dkrz, flar, puti) son cuatro franjas de 32 filas a la izquierda de su textura, y el juego copia una de las tres de abajo sobre la de arriba en los fotogramas pares (6: la 2; si no, 4: la 1; si no, la 0). La textura extraída ya trae las cuatro; el motor hace la copia en `apps/game/src/flame_texture.cpp` (ancho 120, 160, 64 o 176 texeles según la especie).

## glTF

`writeModelGlb` escribe un único `.glb` por modelo (el sistema de ficheros virtual asocia un fichero a un asset, así
que no hay `.bin` aparte). Lo escribe con [cgltf](https://github.com/jkuhlmann/cgltf) y monta el contenedor GLB a mano.
La misma entrada da siempre los mismos bytes.

- **Ejes:** el original tiene Y hacia abajo y Z hacia dentro de la pantalla. Se gira 180° sobre X (`(x, y, z)` →
  `(x, -y, -z)`) para tener la Y hacia arriba de glTF. Es una rotación, así que la orientación de las caras y de las
  rotaciones no cambia.
- **Unidades:** las del original, sin escalar. La escala va a 1.0 = 4096.
- **Nodos:** uno por hueso (`bone_NN`) con su jerarquía y, en la raíz de la escena, el nodo `model` con la malla.
- **Malla y piel:** una malla con piel rígida: cada vértice tiene `JOINTS_0` = el hueso que dibuja su objeto y
  `WEIGHTS_0` = 1. Así un runtime anima todo el modelo con una sola malla y una sola piel, sin reorganizar nodos. La
  pose de reposo es la del primer tick de la primera animación, sin escala para que siempre se pueda invertir; los
  vértices se guardan ya colocados en ella, con sus `inverseBindMatrices`. Los nodos llevan esa pose con su escala.
- **Primitivas:** una por material. Atributos: `POSITION`, `NORMAL` (si tiene luz), `TEXCOORD_0` (si tiene textura y
  se da la textura), `COLOR_0` (si no tiene textura o no tiene luz; con textura, 128 = 1.0 porque modula el texel) y
  los de la piel.
- **Materiales:** por textura y paleta, semitransparencia, dos caras y luz. Sin luz usa `KHR_materials_unlit`. Uno
  semitransparente usa `BLEND` y guarda el modo de la consola en `extras.blending` (`BLEND` solo se le parece). Si no,
  con textura usa `MASK`, para recortar los texels que la consola no dibuja (los que el PNG trae con alfa 0).
- **Semitransparencia por texel:** la consola solo mezcla, en una primitiva semitransparente con textura, los texels
  cuyo color tiene el bit STP (15); los demás los dibuja opacos. Por eso `TexturePalette` trae el tipo de cada texel
  (`kinds`: transparente, `Plain` u `Stp`) y, si la imagen tiene de los dos, la imagen partida (`plainPng` con solo los
  `Plain`, `stpPng` con solo los `Stp`). Una primitiva semitransparente cuyos texels (la caja de sus `u v`) no tienen
  STP se escribe opaca (`MASK`, `blending` 0); una con los dos tipos se escribe dos veces, opaca con `plainPng` y
  semitransparente con `stpPng`; una con solo `Stp` queda como antes. Sin `kinds` se mezclan todos sus texels.
  Ejemplo: la caca del compañero (`ETCNA/UNTI.TMD`, `partner_impl.c: initializePoop`) es semitransparente en modo
  resta, pero su paleta (96, 501) no tiene STP: en la consola es marrón y opaca (medido: texels 148,99,41 /
  132,74,24 / 115,57,8 en `twna01`), y antes el motor la restaba del suelo y salía negra. Su sombra (paleta (96, 502),
  colores 5-14 con STP) sí resta.
- **Texturas:** ver [abajo](#texturas).
- **Animaciones:** una por animación (`anim_NN`, con NN el id), con un fotograma por tick, interpolación lineal,
  rotación en cuaterniones y traslación del hueso 0 = movimiento raíz. Un canal se omite si el hueso no se mueve de la
  pose del nodo en toda la animación. En `extras`: id, `frameCount`, `hasScale`, `ticksPerSecond`, `tickCount`,
  `endsInLoop`, los bucles, los movimientos de textura y los sonidos (todos con su tick).

### Texturas

La consola no sabe de ficheros de textura: cada primitiva con textura dice dónde lee en la VRAM. `TSB` da la página
(x = `(tsb & 0xF) * 64` halfwords, y = `((tsb >> 4) & 1) * 256` filas) y la profundidad (`(tsb >> 7) & 3`: 0 = 4 bits,
1 = 8 bits, 2 = 16 bits), las `u v` son texels dentro de la página, y `CBA` da la paleta (x = `(cba & 0x3F) * 16`,
y = `(cba >> 6) & 0x1FF`). `TmdPrimitive` tiene esos campos ya calculados (`pageX`, `pageY`, `depth`, `clutX`,
`clutY`, `blending`).

`writeModelGlb` recibe una lista de texturas (`ModelTexture`), cada una con su rectángulo en VRAM (en halfwords x,
filas y), su profundidad, su ancho y alto en texels, y un PNG por paleta (`TexturePalette`, con su posición en VRAM).
Una textura sin paletas no tiene imagen: sirve para dar a una primitiva las `u v` de su página sin decir qué se ve.

- Cada primitiva con textura toma la **primera** textura de su profundidad que contenga todos sus texels (las `u v` del
  borde lejano pueden quedar una más allá del último) y, si tiene paletas, la que esté en su `CBA`. Quien llama pone
  primero lo que se subió más tarde, porque eso es lo que tapa a lo anterior en la VRAM.
- Si ninguna textura le sirve, la escritura falla con el motivo. El extractor entonces escribe el modelo sin texturas
  y lo apunta en `unknowns`.
- Las `u v` se pasan a coordenadas de la textura elegida: se dividen entre su ancho y su alto.
- Solo entran en el GLB las combinaciones textura y paleta que alguna primitiva usa, con filtro `NEAREST`.
- Cada material lleva en `extras` dónde lee en la VRAM: `{"image": [x, y], "depth": d, "clut": [x, y], "blending": b}`,
  con `image` = esquina de su textura y `clut` = su paleta. `clut` es `null` en las de 16 bits y en las texturas sin
  imagen, cuya esquina es la de la página. Un material sin textura solo lleva `{"blending": b}`.

## En el motor

El motor no lee nada de lo anterior: carga el `.glb` con `assets::loadModel` y reproduce sus clips con
`animation::AnimationPlayer`, respetando los bucles y el movimiento raíz de `extras`. Qué clips usan el jugador y el
compañero y a qué velocidad andan está en [modelos y animación en el motor](../architecture/models.md).

## Pruebas

- Fixtures sintéticos en `packages/formats/tests/model_fixtures.hpp`: un TMD con dos objetos (triángulo y cuadrilátero, con y
  sin textura, con y sin luz, y una línea que se salta), y un MMD con dos animaciones que usan todas las instrucciones,
  un bucle con cuenta, uno para siempre y escala por hueso.
- Los GLB se vuelven a leer con cgltf (`cgltf_parse`, `cgltf_validate`) y se comprueban nodos, accesores, canales y
  límites. Dos escrituras dan los mismos bytes.
- `model_disc_tests.cpp` solo corre en la máquina de quien tenga el disco: con `DWE_MMD_SAMPLE` = la ruta de un MMD
  lo lee y, con `DWE_MMD_SKELETON` = su esqueleto como `objeto:padre,...`, reproduce todas sus animaciones, escribe
  el GLB y lo vuelve a leer (`DWE_GLB_OUT` lo guarda). Sin esas variables, como en la CI, no hace nada.
  Pasado por los 178 MMD del disco US: todos se leen, todas las animaciones (4843) terminan o entran en su bucle
  infinito, y todos los GLB pasan `cgltf_validate`.
