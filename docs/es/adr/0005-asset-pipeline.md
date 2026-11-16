# 0005. Assets en tres estados y capas, sin fidelidad binaria

- **Estado:** Aceptada
- **Fecha:** 2026-10-18

## Contexto

El equipo tiene que poder editar todo el contenido, los mods tienen que poder añadir y sustituir contenido sin límites
y el motor no debe leer formatos de PS1.

## Decisión

- Cada asset pasa por **tres estados**: original (el disco), fuente (formatos abiertos) y cocinado (para la GPU).
- El motor monta **capas**: base, remaster, mods y usuario. La de arriba gana.
- Los assets se identifican con **IDs estables por nombre**, nunca con índices numéricos.
- La extracción **solo va en un sentido**. No reconstruimos el disco ni buscamos igualdad byte a byte.

El detalle está en la [tubería de assets](../architecture/assets-pipeline.md).

## Consecuencias

- Sin límites de Digimon, mapas ni ningún otro contenido.
- El arte se edita con herramientas comunes (Blender, Krita, editores de texto).
- El criterio de cierre de la extracción es la cobertura: cada fichero del disco se extrae y el motor lo carga.
- Hay que mantener un esquema por cada tipo de asset y la herramienta `dwx`.

## Alternativas descartadas

- **Cargar los formatos originales en el motor:** traslada las limitaciones de PS1 al runtime.
- **Ida y vuelta byte a byte:** solo sirve para reconstruir el disco, cosa que no necesitamos.
