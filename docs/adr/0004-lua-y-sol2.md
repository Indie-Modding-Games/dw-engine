# 0004. Lua 5.5 + sol2, en sandbox

- **Estado:** Aceptada
- **Fecha:** 2026-10-18

## Contexto

La historia, los eventos, los diálogos, las misiones y los modos de juego tienen que poder editarse sin recompilar, y
los mods no deben poder romper el motor ni tocar el sistema del usuario.

## Decisión

- **Lua 5.5.1**, la versión más reciente (agosto de 2026).
- **sol2 3.5** para los bindings con C++.
- Los scripts corren en un **sandbox**: sin `io`, sin `os`, sin cargar código nativo, con límites de memoria y de
  instrucciones.
- La API de Lua se expone **solo** desde el módulo `script` y se versiona con semver.

## Consecuencias

- Diseño y guion pueden trabajar en texto, con recarga en caliente.
- La API de Lua es un contrato público: los cambios incompatibles suben la versión mayor y llevan una guía de
  migración.
- Lua 5.5 es reciente y sol2 la soporta con un parche de compatibilidad; si da problemas, se fija la versión que
  funcione.

## Alternativas descartadas

- **LuaJIT:** más rápido, pero se quedó en Lua 5.1.
- **Un lenguaje propio:** cuesta mucho de mantener y nadie lo conoce.
