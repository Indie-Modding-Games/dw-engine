# 0025. Alta definición en el motor, con solo pasos deterministas fuera de línea

- **Estado:** Aceptada
- **Fecha:** 2026-11-16

## Contexto

El [ADR 0023](0023-remaster-pipeline.md) planeó una tubería fuera de línea para producir la capa `remaster`: recetas con
proveedores intercambiables (un comando local o un servicio de imágenes en la nube), una caché por paso, una revisión de
los resultados y una herramienta aparte. De todo eso solo se construyó `dwx upscale` con el paso integrado
`index_scale`. Mientras tanto el motor aprendió a hacer la alta definición él mismo, al ejecutar: un filtro a la
resolución de la ventana para el fondo y las capas de profundidad del campo, un escalador que duplica una vez cada fondo
y cada textura de modelo y guarda el resultado en una caché, y 4 muestras por píxel. Los mapas y los modelos ya se ven
bien, y la tubería traería código de red, claves, costes, términos de servicio y resultados no reproducibles, para
ganar poco.

## Decisión

La alta definición se hace en el motor, al ejecutar: el escalador, el shader y los filtros, con el resultado en una caché
en la carpeta de datos del usuario. Fuera de línea solo hay `dwx upscale`, con pasos integrados deterministas (hoy
`index_scale`), y la receta incluida que ejecuta `dwx extract`. No hay proveedores de imágenes, ni red, ni servicios de
IA, ni herramienta aparte de mejora. Lo que se hace a partir del disco es derivado de él y no se distribuye nunca. El comportamiento
está descrito en [Alta definición](../architecture/high-definition.md).

## Consecuencias

- La misma entrada da la misma imagen en todas las máquinas. No hay claves, ni costes, ni términos de servicio que
  aceptar, y ni el juego ni `dwx` necesitan la red para esto.
- Los escaladores no inventan detalle: dejan los bordes limpios y las texturas suaves. Quien quiera otro arte trae sus
  propias imágenes ([imágenes propias](../guides/custom-images.md)), y un mod puede traer arte con su propia
  procedencia.
- El formato de receta se queda pequeño: versión 1, un paso `index_scale`. Un paso integrado determinista nuevo
  necesitaría su propio ADR.
- La caché nunca hace falta y se puede borrar en cualquier momento; el motor la vuelve a hacer.
- La página de la tubería pasó a ser [Alta definición](../architecture/high-definition.md) y describe solo lo que
  existe. El ADR 0023 queda sustituido y se conserva como historia.

## Alternativas descartadas

- **Construir la tubería del ADR 0023:** mucho código, más claves, costes y términos de servicio, para fondos y modelos
  que ya se ven bien.
- **Un escalador local de aprendizaje automático como opción:** pesos de terceros con sus licencias, resultados que
  varían entre GPU, y nada que el escalador del motor no dé ya a los tamaños en que se juega.
- **Escaladores de pixel art sobre los índices para los fondos:** los fondos son renders 3D cuantizados a 256 colores,
  en los que casi ningún vecino es igual, así que esos escaladores apenas los cambian (del 6% al 8% de los píxeles en las
  pruebas).
- **Distribuir paquetes `remaster` generados:** es distribuir una obra derivada, que la
  [política legal](../legal/assets.md) prohíbe.
