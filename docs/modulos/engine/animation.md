# animation

Reproduce las animaciones de los modelos que carga `assets` (`ModelData`): `clip_sampling.hpp` muestrea un clip en un
instante (con los bucles y el movimiento raíz que trae el extractor en `extras`), mezcla poses y calcula las matrices
de los huesos para el skinning; `AnimationPlayer` lleva el tiempo, el clip y el fundido de una instancia y devuelve
el movimiento raíz de cada paso. `finished()` dice si un clip sin bucle se quedó en su última pose; `playedThrough()`
dice si cualquier clip, también uno con bucle, llegó al final de su primera vuelta (`anim.c`: `animFrame` llega a
`frameCount`), que es lo que esperan las escenas de cuidado del campo. No usa la GPU: `render` solo recibe las matrices. Depende de `core` y `assets`.
Detalle en [modelos y animación en el motor](../../architecture/models.md) y en el
[ADR 0021](../../adr/0021-animacion-en-cpu-y-skinning-en-gpu.md).
