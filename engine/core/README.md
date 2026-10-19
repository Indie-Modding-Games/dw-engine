# core

Lo que usan todos los módulos: `Result` y errores sin excepciones ([ADR 0007](../../docs/adr/0007-sin-excepciones.md)), log, `DWE_ASSERT`, `FixedStep`, el reloj de ticks fijos de la lógica, SHA-1 y YAML (`parseYaml`/`emitYaml`, [ADR 0016](../../docs/adr/0016-yaml-con-libyaml.md)). Solo depende de libyaml, que no sale de `src/`.
