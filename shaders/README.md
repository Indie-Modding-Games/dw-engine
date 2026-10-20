# shaders

Módulos de Slang que comparten todos los shaders del motor; cada módulo tiene sus propios shaders en
`<módulo>/shaders/` y los compila con `dwe_add_shaders` ([ADR 0018](../docs/adr/0018-shaders-slang-con-vcpkg.md)).

- `dwe/bindless.slang` (`import dwe.bindless;`): la tabla de texturas y los samplers del set global
  ([ADR 0019](../docs/adr/0019-descriptores-bindless.md)) y `sampleTexture`.
