# Modelos y animación en el motor

Cómo carga el motor los modelos que extrae `dwx` (un `.glb` por modelo, ver [modelos](../formats/modelos.md#gltf)),
cómo reproduce sus animaciones y qué modelo y qué clips usan el jugador y el compañero. Las decisiones de fondo están
en el [ADR 0021](../adr/0021-animacion-en-cpu-y-skinning-en-gpu.md).

| Pieza | Módulo | Qué hace |
|---|---|---|
| `assets::ModelData`, `parseModel`, `loadModel` | `assets` | Lee un `.glb` por asset id a datos de CPU, validados |
| `assets::CharacterModel`, `loadCharacterModel` | `assets` | Lee `models/<nombre>.yaml` del paquete `game`: modelo y clip de cada animación |
| `animation::AnimationPlayer` y `clip_sampling.hpp` | `animation` | Muestrea clips, separa el movimiento raíz y calcula las matrices de los huesos |
| `ModelRenderer` | `render` | Sube los modelos una vez y dibuja N instancias por frame con la cámara del mapa |

`animation` es un módulo de la capa de sistemas que solo depende de `core` y `assets`: el campo (`game/field`) puede
pedirle velocidades de paso o movimiento raíz sin depender de `render`, y `render` solo recibe matrices.

## `ModelData`

`parseModel` acepta un glTF binario con el buffer y las imágenes dentro, como los escribe `dwx`, y lo comprueba con
`cgltf_validate` y con reglas propias. Lo que no sabe dibujar es un error con el motivo, nunca un modelo a medias:

- **Nodos:** nombre, padre y pose de reposo (traslación, rotación, escala). Un nodo con matriz, o con un número que
  no es finito, es un error.
  `nodeOrder` lista los nodos con cada padre antes que sus hijos.
- **Primitivas:** solo listas de triángulos. Cada vértice (`ModelVertex`) trae posición, normal, uv, color, cuatro
  huesos y cuatro pesos; lo que la primitiva no tiene toma un valor neutro y `hasNormals`, `hasUvs` y `hasColors`
  dicen qué había. Los índices se comprueban contra los vértices, los huesos contra la piel y los pesos tienen que
  sumar 1 (se normalizan).
- **Color de vértice:** se guarda tal cual. Con textura, el extractor ya ha pasado el 128 del original a 1.0, así que
  el color multiplica el texel y puede llegar a 2; sin textura es el color.
- **Materiales:** color base, imagen (índice en `images`, PNG decodificado a RGBA8), `alphaMode` (`Opaque`, `Mask`
  con su `alphaCutoff`, `Blend`), `unlit` (`KHR_materials_unlit`), `doubleSided` y `blending`, el modo de
  semitransparencia del original (`extras.blending`: media, suma, resta o suma de un cuarto). Una primitiva sin
  material usa uno por defecto.
- **Piel:** una como mucho, con sus huesos y sus matrices de bind inversas. Un modelo con piel solo puede tener mallas
  con piel (la transformación del nodo de la malla no cuenta, como dice glTF); uno sin piel lleva cada malla ya
  colocada con la transformación de su nodo.
- **Clips (`AnimationClip`):** canales por nodo (traslación, rotación como cuaternión `x y z w`, escala), con
  interpolación `LINEAR` o `STEP` (`CUBICSPLINE` es un error y los pesos de morph se ignoran), y los `extras` de
  `dwx`: `id`, `ticksPerSecond`, `tickCount`, `frameCount`, `endsInLoop`, `loops` y `rootMotionNode`. Un clip que
  termina en bucle trae `repeat`: el último bucle en segundos (`start` = `firstTick`, `end` = `lastTick`, `gap` = un
  tick, el paso de vuelta). Un bucle fuera de los ticks del clip, o un clip en bucle cuyo último bucle no acaba en su
  último tick, es un error. `AnimationLoop::count` dice cuántas veces se da el bucle en total, o va vacío si es para
  siempre: el `0xFF` del original solo existe en los `extras` que escribe `dwx`, y se traduce al leer. Un bucle puede
  empezar en el tick 0 (todo el clip); entonces no hay tick antes de su inicio y el paso de vuelta no mueve la raíz.
- **Límites:** 2^20 vértices, 2^22 índices, 1024 nodos, 2^22 fotogramas clave y 2^24 píxeles de imagen (64 MiB en
  RGBA8, contados con las cabeceras de los PNG antes de decodificar ninguno) por modelo. Todo accessor que se lee
  (atributos, índices, matrices de bind, fotogramas) tiene que estar en un buffer view: glTF deja que un accessor sin
  él valga `count` ceros, y un fichero hostil pediría así memoria sin fin. Los mods son contenido no fiable.

`findClip(id)` busca por el id del original (el de los scripts y los YAML) y `findClip(nombre)` por nombre
(`anim_NN`).

## Reproducir clips

Las funciones de `clip_sampling.hpp` son puras y el `AnimationPlayer` las usa por instancia:

- **Tiempo.** `locate` pasa el tiempo desde que empezó el clip a un instante de sus fotogramas. Un clip sin `repeat`
  se queda en su última pose. Uno con `repeat` va de 0 a `end` y luego da vueltas: tras `end` viene `start`, y en el
  tick que hay entre ambos la pose mezcla la de `end` con la de `start`, como el original, que salta del último
  frame del bucle al primero en un tick. Los bucles con cuenta ya vienen desenrollados en los fotogramas
  ([modelos](../formats/modelos.md#bucles-y-límites)).
- **Muestreo.** Un clip muestreado en el tick N da exactamente el fotograma N del glTF; entre fotogramas interpola
  lineal (rotaciones con slerp por el camino corto).
- **Movimiento raíz.** La traslación del nodo `rootMotionNode` (el hueso 0) no entra en la pose: es el movimiento de
  la entidad. `rootMotion(t)` da cuánto se ha movido desde el inicio, sumando cada vuelta del bucle (una vuelta mueve
  lo que va del tick anterior a `firstTick` hasta `lastTick`), en el espacio del modelo como si la entidad mirase a
  su dirección de reposo. `AnimationPlayer::advance` devuelve el movimiento de ese paso; el campo decide si lo usa o
  mueve la entidad con sus propias velocidades.
- **Velocidad de paso.** `repeatGroundSpeed` divide lo que avanza una vuelta entre lo que dura: la velocidad sobre el
  suelo a la que los pies no patinan. Si el campo mueve a otra velocidad, reproduce el clip con
  `speed = velocidad del campo / repeatGroundSpeed`.
- **Fundido.** `play(clip, fadeSeconds)` mezcla desde la pose actual durante ese tiempo. El original no funde; por
  defecto es 0. Un clip de otro modelo se rechaza (`play` devuelve `false`).
- **Matrices.** `computeJointMatrices` da, por hueso de la piel, su transformación en el espacio del modelo por su
  matriz de bind inversa. Es lo que `render` sube por instancia para el skinning en la GPU. Usa un espacio de trabajo
  que le da quien llama (el reproductor guarda el suyo), así que reproducir no reserva memoria en cada frame.

## Jugador y compañero

Los datos están en el paquete `game` (`content/packs/game/models/`, esquema
[`character_model.schema.json`](../../content/schemas/character_model.schema.json)):

```yaml
model: digimon/boys/model
animations:
  idle: 0
  walk: 2
  run: 3
```

El campo pide animaciones por nombre (`idle`, `walk`, `run`) y `CharacterModel::clipOf` da el id del clip.
`checkCharacterModel` comprueba que el modelo tiene todos.

| Personaje | Modelo | Animación → clip | De dónde sale (decomp) |
|---|---|---|---|
| Jugador (`models/tamer`) | `digimon/boys/model` (tipo 0) | `idle` 0, `walk` 2, `run` 3 | `tamer.c: tamerTickWalkingState`: con una dirección pulsada pone la 3; si además se mantiene R1 o cancelar, la 2; sin dirección, la 0 |
| Compañero (`models/partner`) | `digimon/agum/model` (tipo 3) | `idle` 0, `idle_tired` 1, `walk` 2, `walk_tired` 3, `run` 4 | `main.c`: la partida nueva crea el compañero de tipo 3 (o 0x11 con un flag); `partner.c: partnerTickWalking` usa la 4 cuando se queda lejos, `setPartnerSlowWalking` la 2 (3 si está cansado o enfermo) y `setPartnerIdle` la 0 (1 si está cansado o enfermo) |

Los ids del compañero son los mismos para cualquier Digimon: cambiar de compañero es cambiar `model`.

En el original el movimiento sale de la animación (movimiento raíz). Medido sobre los modelos extraídos (lo comprueba
el test de `animation` con `DWE_CONTENT`), en unidades por segundo (100 = una casilla):

| Clip | Vuelta | Velocidad |
|---|---|---|
| Jugador `walk` (2) | 400 unidades en 20 ticks | 400 |
| Jugador `run` (3) | 600 unidades en 12 ticks | 1000 |
| Compañero `walk` (2) | 360 unidades en 16 ticks | 450 |
| Compañero `run` (4) | 540 unidades en 12 ticks | 900 |

El jugador corre por defecto y anda con un botón mantenido: es el comportamiento del original.

**Con el campo.** `game/field` mueve las entidades con sus propias velocidades (`content/packs/game/field/rules.yaml`,
los mismos modelos: `digimon/boys/model` y `digimon/agum/model`) y deja en cada una un `scene::AnimationIntent`.
`scene::motionName(intent.motion)` da `idle`, `walk` o `run`, los nombres de estos YAML, así que el clip es
`clipOf(motionName(motion))`: `models/tamer` para la entidad con `PlayerTag` y `models/partner` para la de
`PartnerTag`. Para que los pies no patinen, el clip va a
`speed = intent.speedScale × velocidad de rules.yaml / repeatGroundSpeed(clip)`. Las velocidades del jugador son
las mismas (400 y 1000); las del compañero en `rules.yaml` (452 y 870) son la media del movimiento raíz de todo el
clip, y las de la tabla, la de una vuelta del bucle (450 y 900), así que el compañero corre a 0,97.

## Dibujar: `ModelRenderer`

`render::ModelRenderer` dibuja en el pase de escena ([render](render.md)) con dos drawers, `opaque()` y
`translucent()`. `render` depende de `assets` (lee
`ModelData`) pero no de `animation`: recibe matrices.

```cpp
auto models = render::ModelRenderer::create(renderer);           // 8 pipelines, una vez
auto handle = (*models)->add(modelData);                         // buffers y texturas; ModelData se puede soltar
animation::AnimationPlayer player{modelData};
player.play(*character.clipOf("walk"));
// Cada frame:
(*models)->clear();                                              // antes de enviar las instancias del frame
player.advance(seconds);
(*models)->setCamera(camera);                                    // render::Camera (la del mapa)
(*models)->setLighting(lighting);                                // ambiente + 3 luces direccionales
(*models)->submit({.model = *handle, .world = transform, .joints = player.jointMatrices()});
// FrameDesc::scene = {fondo, &(*models)->opaque(), capas del mapa, &(*models)->translucent()}
```

- **Subida.** `add` junta los vértices (`ModelVertex` tal cual) e índices de 32 bits de todas las primitivas en
  **un** buffer (vértices y detrás los índices; un solo `createWithData`, que espera a la GPU una vez: es para la
  carga) y pone en cola una textura por imagen (`queueTexture`, sin esperar). Llamado entre frames, el modelo se
  dibuja en el siguiente.
- **Handles.** `ModelHandle` es `{index, generation}` (un `SlotMap`, como `TextureId`): `remove` suelta todo y sube
  la generación del hueco, así que un handle viejo se rechaza aunque el hueco ya tenga otro modelo (`submit`
  devuelve `false`, `jointCount` 0).
- **Instancias.** `submit` copia las matrices de los huesos ya multiplicadas por la transformación de la instancia;
  vacías, el modelo sale en su pose de reposo (la pose de reposo de los nodos por las matrices de bind inversas).
  `prepare` del drawer opaco toma lo enviado (y lo vacía), sube un bloque por frame (cámara y luces) y las matrices
  de **cada instancia por separado** al anillo, y pasa sus direcciones en las push constants (48 bytes). Si el anillo
  se llena, las instancias que caben se dibujan y el resto no: lo registra en el log como error con las cuentas
  cuando cambia (no cada frame), nunca en silencio. Un frame que no se dibuja (ventana minimizada) no llama a
  `prepare`, así que el juego llama a `clear()` antes de enviar cada frame. Un draw por primitiva e instancia; las
  listas del frame se reutilizan sin reservar memoria nueva.
- **Skinning.** En el vertex shader (`shaders/skinned_model.slang`): suma de las matrices de los cuatro huesos por sus
  pesos. Los modelos extraídos son rígidos (un hueso con peso 1), pero el shader no lo supone.
- **Luz.** Texel × color de vértice × color base, por `ambiente + Σ max(n·−dir, 0) × color` de tres luces
  direccionales, limitado a [0, 2] como el original (1 deja el texel igual). `unlit` se salta la luz. Una luz con
  dirección cero no ilumina y una normal de longitud cero (un hueso escalado a cero) solo recibe el ambiente, sin
  NaN.
  `defaultModelLighting()` es una luz neutra para herramientas; el campo pasará las luces del mapa.
- **Transparencias.** `Mask` descarta bajo `alphaCutoff`. `Blend` lo dibuja `translucent()`, después de todo lo
  opaco (modelos y capas del mapa), de atrás a delante **por instancia**: la clave es la profundidad en espacio de
  vista del origen de la instancia (`(view × world[3]).z`, menor = más lejos), y dentro de una instancia las
  primitivas van en su orden. Con test de profundidad y sin escribirla; el modo sale de `blending` (`materialBlend`:
  media = `Alpha` con alfa 0,5; suma = `Additive`; resta = `Subtractive`; suma de un cuarto = `Additive` con alfa
  0,25). Los texels con alfa 0 se descartan también aquí.
- **Caras.** `doubleSided` dibuja sin descartar caras; si no, se descartan las traseras (glTF es antihorario).

## Visor de modelos

`scripts/dev.sh run --content <contenido extraído> --model-viewer [id...]` monta los paquetes del contenido más el
paquete `game` y pone en fila cada personaje (por defecto `models/tamer` y `models/partner`), una vez por animación,
en el sitio (sin movimiento raíz), con una cámara que gira. Se adapta a cualquier tamaño de ventana. Pone en
`FrameDesc::scene` el drawer opaco y después el semitransparente, llama a `clear()` antes de enviar cada frame y
registra en el log lo que tarda el `add` de cada personaje.

## Pruebas

- `engine/assets/tests/model_data_tests.cpp` monta un `.glb` a mano (`glb_builder.hpp`) con piel, textura, material
  sin luz, un clip como los de `dwx` y otro con `STEP`, y comprueba cada campo y los errores (índices, huesos, pesos,
  bucles, semitransparencia). Con `DWE_CONTENT` carga **todos** los `.glb` extraídos: 337 modelos, 4843 clips (2070
  en bucle infinito), todos con id, 20 ticks por segundo y un fotograma por tick.
- `engine/animation/tests/animation_tests.cpp` comprueba que el tick N es el fotograma N, que el bucle vuelve bien
  (incluido el tick de vuelta), que el movimiento raíz sigue a ritmo constante por todas las vueltas, las matrices de
  los huesos, el fundido y que los YAML del paquete `game` se leen. Con `DWE_CONTENT` monta el contenido con el
  paquete `game` y comprueba las velocidades de la tabla y un minuto de cada animación de los dos personajes.
- `engine/render/tests/model_renderer_tests.cpp` comprueba la traducción de materiales a modos de mezcla y la luz por
  defecto, y `slot_map_tests.cpp` la vida de los handles (quitar → añadir rechaza el handle viejo): el
  `ModelRenderer` necesita un `rhi::Device` y en Docker no hay GPU, así que su ciclo añadir/quitar/dibujar se prueba
  a través del `SlotMap` que guarda sus modelos. El dibujo se comprueba a mano con el visor (sin GPU en Docker no hay capturas de referencia todavía).
