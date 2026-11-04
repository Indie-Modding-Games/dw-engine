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

`buildChibiRig` da a cada forma sus factores (`defaultProportions`; un bípedo: cabeza ×1,6, torso ×0,8, brazos ×0,7,
piernas ×0,65, manos ×1,2, pies ×1,15) y, en un bípedo, los ajusta al tamaño de sus partes en reposo: la cabeza crece
más cuanto más pequeña es respecto a la altura (×1,6 con una cabeza de 0,325 de la altura, entre ×1,3 y ×2) y una mano
que ya es grande no crece. Un cuerpo de forma desconocida se queda sin cambios. Los factores se aplican sobre la pose
animada en `computeChibiJointMatrices`: acerca los huesos de cada miembro, aplasta cada segmento a lo largo y lo
ensancha un poco, escala la cabeza entera desde su hueso y acorta el tronco por su eje (arriba en un cuerpo erguido,
adelante en uno a cuatro patas). En cada fotograma sube o baja el modelo para que el punto más bajo de las piernas
quede donde el del original; un modelo que vuela (en reposo, por encima de 1/16 de su altura) se mueve una vez por
todas. Si un brazo, la cola, un ala o la cabeza quedan por debajo del punto más bajo del original, esa parte se encoge
un poco hasta que no. Las matrices de la lógica (`AnimationPlayer::jointMatrices`) no cambian.

Coste: una pasada más por los nodos y, por fotograma, los puntos de apoyo de las piernas (entre 86 y 296, sin
repetir) de cada modelo; con la compilación de depuración, el avance de la animación tarda una vez y media más.
Pruebas aisladas en `tests/chibi_tests.cpp` (un bípedo de cajas, las correcciones y, con el contenido extraído, la
forma y el suelo de siete especies de prueba y las correcciones de `models/chibi.yaml`).
