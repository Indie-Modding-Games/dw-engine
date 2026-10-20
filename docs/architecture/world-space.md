# Espacio del mundo

Las coordenadas que comparten el campo, el render, los modelos y la UI. Las matemáticas son GLM, a través de
`<dwe/core/math.hpp>`: ningún módulo incluye GLM directamente, para que todos usen la misma configuración (ángulos en
radianes y profundidad de recorte de 0 a 1, la de Vulkan).

## Ejes y unidades

- **Mano derecha, +Y hacia arriba.** Es el sistema de glTF, el de los modelos extraídos.
- **Unidades del original, sin escalar.** 100 unidades son una celda de la rejilla de colisión de un mapa.
- **Del original al mundo:** el original tiene la Y hacia abajo y la Z hacia dentro de la pantalla. Un punto o una
  dirección `(x, y, z)` del original es `(x, -y, -z)` en el mundo (`core::fromOriginalAxes`). Es un giro de 180° sobre
  X, así que no cambia la orientación de las caras ni el sentido de los giros.
- **El suelo** es el plano XZ. La lógica del campo se mueve sobre él; la altura (Y) la ponen los datos del mapa.
- **Ángulos:** en radianes en el motor. El original usa 4096 por vuelta (`core::OriginalAnglePerTurn`); los datos que
  vienen del disco se convierten al cargarlos.

Los ficheros extraídos (`map/<id>/data.yaml` y los demás) guardan los números tal como están en el disco: la
conversión se hace al cargarlos, en un solo sitio.
