# shaders

Slang modules shared by all the engine's shaders; each module has its own shaders in
`<module>/shaders/` and compiles them with `dwe_add_shaders` ([ADR 0018](../adr/0018-slang-shaders-with-vcpkg.md)).

- `dwe/bindless.slang` (`import dwe.bindless;`): the texture table and the samplers of the global set
  ([ADR 0019](../adr/0019-bindless-descriptors.md)) and `sampleTexture`.
