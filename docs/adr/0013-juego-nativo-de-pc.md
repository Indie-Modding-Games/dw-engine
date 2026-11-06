# 0013. Un juego nativo de PC

- **Estado:** Aceptada
- **Fecha:** 2026-10-18
- **Nota (2026-11-06):** aceptada porque el código ya la sigue, en el juego (`apps/game`).

## Contexto

La plataforma objetivo es PC. No queremos que se sienta como un juego de PS1 portado, sino **como si hubiera salido
solo en PC**.

## Decisión

Todo el diseño de interfaz, controles y experiencia parte de las convenciones de PC:

| Área | Qué significa |
|---|---|
| Controles | Teclado y ratón al mismo nivel que el mando: menús que se usan con el ratón, *tooltips* al pasar por encima, atajos de teclado y todas las teclas reasignables. Cualquier mando que se conecte al PC ([ADR 0014](0014-mandos-y-entrada.md)) |
| Avisos de botones | Cambian según el dispositivo que se esté usando (tecla, ratón, mando), nunca los símbolos fijos de PS1 |
| Pantalla | Cualquier resolución y proporción (16:9, 16:10, ultrapanorámico), UI escalable, ventana, sin bordes o pantalla completa, varios monitores |
| Rendimiento | Frecuencia de imagen libre, con interpolación sobre la lógica a paso fijo; VSync y límite configurables |
| Guardado | Ficheros con varias ranuras y autoguardado. Nada de tarjetas de memoria ni de bloques |
| Carga | Sin pantallas de carga heredadas del CD ni cambios de disco |
| Texto | Fuentes vectoriales, sin límites de caracteres por línea y con localización completa |
| Opciones | Gráficos, audio, controles y accesibilidad (tamaño de texto, subtítulos, daltonismo) |
| Mods | Carpeta de mods y gestor en el launcher |

## Consecuencias

- La UI se diseña para ratón y para mando a la vez. Esto afecta a cada menú del GDD.
- El estilo visual (fondos prerenderizados con modelos 3D encima) se mantiene, con fondos HD y panorámicos.
- macOS, consolas y móviles no son objetivo ([ADR 0003](0003-vulkan-y-sdl3.md)).

## Alternativas descartadas

- **Un port fiel con la experiencia de consola:** contradice el objetivo.
