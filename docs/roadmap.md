# Plan por fases

Cada fase tiene un criterio de cierre que se puede comprobar. Una fase no se da por terminada hasta que se cumple.

| Fase | Nombre | Tamaño | Estado |
|---|---|---|---|
| 0 | Base | S | En curso: el código base compila y funciona en Linux |
| 1 | Tubería de assets | M | Pendiente |
| 2 | Prueba vertical | M | Pendiente |
| 3 | Núcleo jugable | L | Pendiente |
| 4 | Contenido y modos | XL, continuo | Pendiente |

Antes de la fase 3, el repositorio de diseño tiene que tener escritos los **pilares de diseño**
([ADR 0010](adr/0010-reglas-propias-con-rulesets.md)).

## Fase 0 — Base

- Esta documentación: arquitectura, ADR, plan y convenciones.
- Repositorio con CMake, presets, vcpkg, clang-format, clang-tidy y CI en Linux y Windows.
- `engine/` mínimo:
  - ventana y dispositivo Vulkan;
  - entrada por acciones con teclado, ratón y mandos, con conexión en caliente ([ADR 0014](adr/0014-mandos-y-entrada.md));
  - bucle con lógica a paso fijo y render desacoplado;
  - VM de Lua con recarga en caliente;
  - log y consola ImGui.
- Comprobación del disco por SHA-1.
- Limpieza de los repositorios actuales según la [política legal](legal/assets.md).

**Hecho cuando:** se abre una ventana, un mando o el teclado mueven algo en pantalla, se ejecuta un script de Lua y la
CI pasa.

**Estado (2026-10-19):**

| Punto | Estado |
|---|---|
| Documentación, ADR, plan y convenciones | Hecho |
| CMake, presets, vcpkg, clang-format, contenedor y CI de Linux | Hecho. Compila sin warnings en `linux-clang-debug` y en `ci` (warnings como errores), tests y formato en verde en local. La CI aún no ha corrido en GitHub |
| CI de Windows | Pendiente |
| clang-tidy limpio | Pendiente: quedan avisos de estilo; la CI todavía no lo pasa |
| Ventana y dispositivo Vulkan | Hecho: swapchain que se rehace al cambiar de tamaño, sin errores de validación |
| Entrada por acciones, conexión en caliente | Hecho con teclado; mandos sin probar en una máquina real |
| Lógica a paso fijo y render desacoplado | Hecho |
| VM de Lua aislada con recarga en caliente | Hecho |
| Log y consola ImGui | Hecho |
| Comprobación del disco por SHA-1 | Pendiente |
| Limpieza de los repositorios actuales | Pendiente: decisión del equipo |

## Fase 1 — Tubería de assets

- Librería `formats/`: sistema de ficheros del CD, TIM, TMD/MMD, mapas, ALLTIM, textos, bytecode de eventos, VAB/SEQ,
  STR y las tablas del ejecutable.
- `dwx extract`, `validate`, `cook` y `pack`.
- Sistema de ficheros virtual con capas y manifiestos de paquete.
- Esquemas de cada tipo en `content/schemas/` y documentación de los formatos en `docs/formats/`.
- Visor ImGui de modelos y mapas.
- Primeras fichas de referencia ([`docs/reference/`](reference/README.md)): cuidado, evolución y batalla.

**Hecho cuando:** cada fichero del disco US se extrae y el motor carga el resultado. El informe de cobertura está a
cero pendientes.

## Fase 2 — Prueba vertical

- Render: fondos prerenderizados con capas de profundidad, modelos con skinning y la cámara de cada mapa.
- Campo: movimiento, colisión, cambio de mapa por warps y el partner siguiendo al jugador.
- RmlUi integrado, con un primer menú nuevo que se maneja con ratón y con mando.
- **Minimapa.**

**Hecho cuando:** se puede caminar de File City a la casa de Jijimon y volver, con minimapa, en cualquier resolución y
con cualquier dispositivo de entrada.

## Fase 3 — Núcleo jugable

Cada sistema sigue el [ciclo de diseño](conventions/design-cycle.md): ficha de referencia, diseño aprobado en el GDD,
implementación con las reglas en el ruleset, tests y pruebas de juego.

- Rulesets: el formato y el primer ruleset, sembrado con los valores del original.
- Partner: stats, cuidado, vida y evolución.
- Batalla.
- Inventario y guardado (ranuras y autoguardado).
- Panel de ajuste en vivo y simulaciones de balance.

**Hecho cuando:** el bucle de criar, entrenar, combatir y evolucionar se juega con el diseño nuevo.

## Fase 4 — Contenido y modos

- Historia: los scripts de eventos originales traducidos a Lua como punto de partida, y reescritos.
- Profundidad de personajes: árboles de diálogo, afinidad y misiones.
- Framework de modos de juego, cada uno con su ruleset.
- API de mods: paquetes que añaden o sobrescriben contenido por ID, y hooks de Lua.
- Más Digimon y mapas, fondos HD.
- El editor de mapas, adaptado al formato fuente.

**Hecho cuando:** se publica la primera versión del mod. A partir de ahí la fase sigue abierta.

## Riesgos

| Riesgo | Cómo se mitiga |
|---|---|
| Alcance enorme | Prueba vertical pronto, criterios de cierre por fase y mejoras visibles desde la fase 2 |
| Perder la identidad del juego sin querer | Pilares de diseño escritos y fichas de referencia antes de cada sistema |
| Un diseño distinto en la cabeza de cada persona | El GDD es la especificación, y una persona responsable de diseño tiene la última palabra |
| El balance | Rulesets editables, ajuste en vivo, simulaciones y pruebas de juego frecuentes |
| Formato de scripts de eventos poco documentado | Investigarlo en la fase 1 con el decomp como referencia |
| Digimon de otros juegos | Importador desde el disco del usuario, o modelos propios del equipo |
| Riesgo legal | [Política legal de assets](legal/assets.md) |
