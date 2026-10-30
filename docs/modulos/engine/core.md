# core

Lo que usan todos los módulos: `Result` y errores sin excepciones ([ADR 0007](../../adr/0007-sin-excepciones.md)), log (y `LogRing`/`keepLogTail`, las últimas líneas en memoria para los reportes de bug), `formatMessage` para las plantillas de mensajes de cada `messages.hpp`, `DWE_ASSERT`, `FixedStep`, el reloj de ticks fijos de la lógica, SHA-1, YAML (`parseYaml`/`emitYaml`, [ADR 0016](../../adr/0016-yaml-con-libyaml.md)) y `JsonSchema`, que comprueba documentos YAML contra un JSON Schema
([subconjunto admitido](../../architecture/json-schema.md)), y las matemáticas (GLM, a través de `<dwe/core/math.hpp>`, con los ejes del [espacio del mundo](../../architecture/world-space.md)). Depende de GLM, público, y de libyaml, que no sale de `src/`.
