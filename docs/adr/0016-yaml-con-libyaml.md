# 0016. YAML con libyaml, envuelto en `core`

- **Estado:** Propuesta
- **Fecha:** 2026-10-19

## Contexto

El formato fuente del proyecto es YAML ([ADR 0005](0005-tuberia-de-assets.md), [ADR 0010](0010-reglas-propias-con-rulesets.md)):
manifiestos de paquetes, datos de mapas y tablas del juego. El motor tiene que leerlo y `dwx` tiene que escribirlo de
forma determinista, para que extraer dos veces el mismo disco dé los mismos ficheros. Todo se compila sin excepciones
([ADR 0007](0007-sin-excepciones.md)), y los ficheros pueden venir de mods, así que la entrada no es de fiar.

## Decisión

- Se usa **libyaml** (C, port `libyaml` de vcpkg), solo dentro de `engine/core/src`. El header público
  `<dwe/core/yaml.hpp>` expone un árbol propio (`YamlNode`), `parseYaml` y `emitYaml`, y no incluye `yaml.h`.
- Solo se acepta un subconjunto: un documento por fichero, sin anclas, alias ni etiquetas, sin claves repetidas y con
  claves escalares. Hay límite de tamaño y de anidamiento.
- Los escalares se guardan como texto; quien lee un campo decide qué significa.

## Consecuencias

- Cada error se devuelve con `core::Result` y dice la línea y la columna.
- Sin alias no hay ataques de expansión ("billion laughs") y el límite de anidamiento evita agotar la pila.
- La salida es estable: orden de claves de inserción, comillas dobles para lo que es texto, LF y salto final.
- libyaml compila igual en Linux y en Windows estático; su API en C se envuelve con RAII en un solo fichero.

## Alternativas descartadas

- **rapidyaml:** rápido, pero sin excepciones su callback de error no puede volver; habría que salir con
  `setjmp`/`longjmp`, que se salta los destructores.
- **yaml-cpp:** informa de los errores con excepciones.
- **fkYAML:** solo headers y también usa excepciones.
- **Un parser propio:** YAML es grande y sutil; no compensa mantenerlo.
