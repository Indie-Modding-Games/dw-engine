# 0025. High definition in the engine, with only deterministic steps offline

- **Status:** Accepted
- **Date:** 2026-11-16

## Context

[ADR 0023](0023-remaster-pipeline.md) planned an offline pipeline to produce the `remaster` layer: recipes with
interchangeable providers (a local command or a cloud image service), a cache per step, a review of the results and a
separate tool. Of all that, only `dwx upscale` with the built-in `index_scale` step was ever built.
Meanwhile the engine learned to do high definition itself, at run time: a filter at the window's resolution for the
field's background and depth layers, a scaler that doubles each background and each model texture once and keeps the
result in a cache, and 4 samples per pixel. The maps and the models already look good, and the pipeline would bring
network code, keys, costs, terms of service and results that are not reproducible, for little gain.

## Decision

High definition is done in the engine, at run time: the scaler, the shader and the filters, with the result cached in
the user's data folder. Offline there is only `dwx upscale`, with built-in deterministic steps (today `index_scale`),
and the bundled recipe that `dwx extract` runs. There are no image providers, no network and no AI services, and no
separate enhancement tool. What is made from the disc is derived from it and is never distributed. The behavior is described in
[High definition](../architecture/high-definition.md).

## Consequences

- The same input gives the same picture on every machine. There are no keys, no costs and no terms of service to
  accept, and neither the game nor `dwx` needs the network for this.
- The scalers do not invent detail: they make edges clean and textures smooth. A player who wants other art brings
  their own images ([custom images](../guides/custom-images.md)), and a mod can ship art with its own provenance.
- The recipe format stays small: version 1, one `index_scale` step. A new built-in deterministic step would need its
  own ADR.
- The cache is never needed and can be deleted at any time; the engine makes it again.
- The page about the pipeline became [High definition](../architecture/high-definition.md) and describes only what
  exists. ADR 0023 is superseded and stays as history.

## Alternatives rejected

- **Build the pipeline of ADR 0023:** a lot of code, plus keys, costs and terms of service, for backgrounds and models
  that already look good.
- **A local machine learning scaler as an option:** third-party weights with their own licenses, results that vary
  between GPUs, and nothing the engine's scaler does not already give at the sizes the game is played at.
- **Pixel art scalers on the indices for the backgrounds:** the backgrounds are 3D renders quantized to 256 colors, in
  which almost no neighbor is equal, so those scalers barely change them (6% to 8% of the pixels in the tests).
- **Distribute generated `remaster` packs:** that is distributing a derived work, which the
  [legal policy](../legal/assets.md) forbids.
