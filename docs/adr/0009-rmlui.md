# 0009. RmlUi para la UI del juego

- **Estado:** Propuesta
- **Fecha:** 2026-10-18

## Contexto

Queremos mejorar los menús, añadir un HUD y minimapas, y que diseño y los mods puedan cambiar la UI sin recompilar.

## Decisión

La UI del juego se hace con **RmlUi**: documentos tipo HTML (RML), estilos tipo CSS (RCSS) y lógica en Lua. Se dibuja
con nuestro módulo `render`. Dear ImGui queda solo para las herramientas de depuración y el editor.

## Consecuencias

- Los menús se editan como texto, con recarga en caliente.
- Hay que escribir el backend de render de RmlUi sobre `render`.

## Alternativas descartadas

- **Una UI propia:** control total, pero mucho trabajo antes de tener el primer menú.
- **Dear ImGui para el juego:** está pensada para herramientas, no para menús con estilo propio.
