# animation

Reproduce las animaciones de los modelos que carga `assets` (`ModelData`): `clip_sampling.hpp` muestrea un clip en un
instante (con los bucles y el movimiento raíz que trae el extractor en `extras`), mezcla poses y calcula las matrices
de los huesos para el skinning; `AnimationPlayer` lleva el tiempo, el clip y el fundido de una instancia y devuelve
el movimiento raíz de cada paso. `finished()` dice si un clip sin bucle se quedó en su última pose; `playedThrough()`
dice si cualquier clip, también uno con bucle, llegó al final de su primera vuelta (`anim.c`: `animFrame` llega a
`frameCount`), que es lo que esperan las escenas de cuidado del campo. `clipSounds` da los sonidos del clip
(`extras.sounds`, la instrucción 0x4000 de `anim.c: tickAnimation`) de los ticks que un paso deja atrás, en cada vuelta
del bucle, y `AnimationPlayer::takeSounds()` los del último `advance` para que el dueño los toque o no. No usa la GPU: `render` solo recibe las matrices. Depende de `core` y `assets`.
Detalle en [modelos y animación en el motor](../../architecture/models.md) y en el
[ADR 0021](../../adr/0021-animacion-en-cpu-y-skinning-en-gpu.md).

## Estilo chibi

Mejora del motor y prototipo (el original no la tiene): `chibi.hpp` dibuja un modelo con la cabeza grande, el torso
corto y los miembros cortos sin tocar sus animaciones. `classifyChibi` reconoce las partes en la pose de reposo, con el
esqueleto y la malla de cada hueso:

- el tronco va de la raíz del cuerpo hasta el último hueso donde se separan dos miembros (dos ramas con dos o más
  huesos con malla y al menos el 4 % de los vértices cada una); lo que cuelga de él son ramas;
- una pierna llega al suelo (su vértice más bajo a menos de 1/16 de la altura del modelo), cuelga de la parte baja del
  cuerpo y baja más de lo que se abre hacia el lado;
- la cabeza es la rama del centro, con al menos el 5 % de los vértices, más alta y adelantada respecto al tronco; los
  huesos entre su raíz y el hueso con más vértices son el cuello;
- las demás ramas de dos o más huesos son brazos (a un lado, colgando), alas (a un lado, sin colgar o hacia atrás) o
  colas (en el centro, detrás); una rama de un hueso se queda como está;
- cuatro piernas hacen un cuadrúpedo, ninguna y una cola una serpiente, alas sin brazos un cuerpo alado y dos piernas o
  dos brazos un bípedo.

Los modelos del original ya son chibi y son la referencia. `measureBody` mide un modelo en su pose de reposo (con la
malla ya pegada a los huesos) y da once proporciones: la cabeza respecto a la altura y el torso respecto a la altura
(solo para decidir), la cabeza respecto al torso (`head_size`), el ancho de la cabeza respecto al del tronco
(`head_width`), el alcance de brazos, piernas, cuello, cola y alas y el tamaño de manos y pies (lo ancho del último
30 % del miembro), todo respecto al torso. En una serpiente, sin tronco, la cola hace de torso. `profileOf` junta las
medidas de los originales por forma y guarda, por proporción, el mínimo, la mediana y el máximo (una medida repetida
cuenta una vez); el perfil vive como datos en `models/chibi_profile.yaml` del paquete del juego (40 bípedos, 13
cuadrúpedos, 6 alados y 6 serpientes) y se lee con `parseChibiProfile`. Lo genera la prueba con contenido:

```
DWE_CONTENT=<contenido extraído> DWE_CHIBI_PROFILE_OUT=<paquete del juego>/models/chibi_profile.yaml \
  dwe_animation_tests --test-case='*chibi profile*'
```

Sin `DWE_CHIBI_PROFILE_OUT` la misma prueba compara el perfil guardado con el que sale del disco del usuario. Las
especies con `off: true` en `models/chibi.yaml` no entran en el perfil.

`buildChibiRig(modelo, perfil)` es un detector: si todas las proporciones medidas caen dentro del rango de su forma
(con una tolerancia de 0,1 % por el redondeo del archivo), el modelo ya es chibi (`ChibiRig::alreadyChibi`) y se
dibuja exactamente igual que sin el estilo (las mismas matrices, bit a bit). Si no, se calcula cuánto se sale (el
mayor desvío, en escala logarítmica, hasta el doble del extremo del rango) y cada proporción que se ajusta va hacia la
mediana: desde el extremo del rango más cercano hasta la mediana, más cerca cuanto más se sale. Unas rondas de ajuste
buscan los factores (cabeza, brazos, piernas, cuello, cola, alas, manos y pies; el torso no cambia y hace de unidad)
que dan esas proporciones, con límites para que el modelo no se deforme: entre ×0,35 y ×3, manos y pies hasta ×2 y el
cuello nunca crece. `ChibiRig` guarda la medida, el rango, qué proporciones se salían y cómo quedan dibujadas. Un
cuerpo de forma desconocida, o sin perfil para su forma, se queda sin cambios. Los factores se aplican sobre la pose
animada en `computeChibiJointMatrices`: acerca los huesos de cada miembro, aplasta cada segmento a lo largo y lo
ensancha un poco, escala la cabeza entera desde su hueso y acorta el tronco por su eje (arriba en un cuerpo erguido,
adelante en uno a cuatro patas). En cada fotograma sube o baja el modelo para que el punto más bajo de las piernas
quede donde el del original; un modelo que vuela (en reposo, por encima de 1/16 de su altura) se mueve una vez por
todas. Si un brazo, la cola, un ala o la cabeza quedan por debajo del punto más bajo del original, esa parte se encoge
un poco hasta que no. Las matrices de la lógica (`AnimationPlayer::jointMatrices`) no cambian.

Coste: una pasada más por los nodos y, por fotograma, los puntos de apoyo de las piernas (entre 86 y 296, sin
repetir) de cada modelo; con la compilación de depuración, el avance de la animación tarda una vez y media más.
Pruebas aisladas en `tests/chibi_tests.cpp`: modelos de cajas hechos en la prueba (un humanoide alto y delgado y un
cuadrúpedo de patas largas entran en el rango sin despegar los pies del suelo; uno ya chibi queda igual bit a bit
durante un paso), los bordes del detector, el perfil escrito y leído de nuevo y, con el contenido extraído, el perfil
del disco y que ningún original cambia.
