# animation

Reproduce las animaciones de los modelos que carga `assets` (`ModelData`): `clip_sampling.hpp` muestrea un clip en un
instante (con los bucles y el movimiento raíz que trae el extractor en `extras`), mezcla poses y calcula las matrices
de los huesos para el skinning; `AnimationPlayer` lleva el tiempo, el clip y el fundido de una instancia y devuelve
el movimiento raíz de cada paso. No usa la GPU: `render` solo recibe las matrices. Depende de `core` y `assets`.
Detalle en [modelos y animación en el motor](../../architecture/models.md) y en el
[ADR 0021](../../adr/0021-animacion-en-cpu-y-skinning-en-gpu.md).
