# Reglas de dependencias

Las capas de la [visión general](overview.md) solo se usan hacia abajo. Estas reglas son las que mantienen el motor
modificable a largo plazo: si se rompen, cambiar una pieza obliga a tocar muchas otras.

## Reglas

1. **Las dependencias solo bajan.** Una capa usa las de abajo, nunca las de arriba ni ninguna en ciclo.
2. **El runtime nunca depende de `formats`.** Solo `apps/dwx` lo usa. Así ninguna limitación de PS1 (IDs fijos,
   tamaños de tabla, paletas) llega al juego.
3. **`game/*` no conoce Vulkan ni SDL.** Pide cosas a `render`, `audio`, `input`, `ui` o `scene` a través de sus APIs públicas.
4. **Los módulos de `game/` no se usan entre sí directamente.** Se comunican por eventos de `scene` o a través de
   interfaces declaradas en el módulo que las necesita. Así `battle` no se rompe cuando cambia `partner`.
   Lo que comparten todos (el ruleset, sus estrategias y el reloj de juego) está en el servicio `ruleset`
   ([rulesets](rulesets.md)). El único sitio que los junta es la librería de app `apps/session`
   ([README](../../apps/session/README.md)): la partida, que el juego y `dwe-sim` usan igual.
5. **Lua solo ve lo que `script` expone.** La API de Lua es un contrato versionado con semver: los cambios
   incompatibles suben la versión mayor.
6. **Solo se incluyen headers públicos**: `<dwe/<módulo>/...>`. Nunca `src/` de otro módulo.

## Qué puede usar cada capa

| Capa | Puede depender de |
|---|---|
| `core` | La librería estándar y dependencias de terceros básicas |
| `platform`, `rhi` | `core` |
| `render`, `audio`, `input`, `assets`, `localization`, `animation`, `scene` | Capas base, `core` y otros sistemas, sin ciclos (`animation`, `render` y `localization` usan `assets`) |
| `script`, `ui` | Sistemas, base y `core` |
| `game/*` | Servicios, sistemas y `core`; **no** `rhi` ni `platform` |
| `apps/session` | `game/*`, servicios, sistemas y `core`; **no** `rhi`, `platform` ni `render` (corre sin ventana en `dwe-sim` y en los tests) |
| `apps/screens` | `apps/session`, `game/*`, `ui`, `localization`, `assets` y `core`; **no** `rhi`, `platform` ni `render` (los modelos de vista de las pantallas, probados sin ventana) |
| `apps/game` | Todo excepto `formats` |
| `apps/content` | `assets`, `platform` y `core` (`platform` para la carpeta de datos del usuario y para ejecutar `dwx`) |
| `apps/dwx` | `formats`, `assets` y `core` |
| `formats` | `core` |

Como `apps/content` usa `platform`, `dwe-sim` enlaza `platform` (y con él SDL3) a través de `apps/content`, aunque
sigue sin abrir ventanas. Los tests de `apps/dwx` enlazan además `platform`, solo para ejecutar `dwx` como lo hace el
juego (`platform::runProcess`); el programa `dwx` no lo enlaza.

## Cómo se hace cumplir

- `dwe_add_module(<nombre> DEPS ...)` declara las dependencias permitidas. CMake no deja incluir los headers de un
  módulo no declarado.
- La CI valida el grafo de dependencias contra esta tabla y falla si aparece una flecha prohibida o un ciclo.
- Una excepción a estas reglas necesita un ADR.
