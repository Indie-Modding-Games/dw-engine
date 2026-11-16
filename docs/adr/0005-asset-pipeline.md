# 0005. Assets in three states and layers, without binary fidelity

- **Status:** Accepted
- **Date:** 2026-10-18

## Context

The team must be able to edit all the content, mods must be able to add and replace content without limits, and the
engine must not read PS1 formats.

## Decision

- Every asset goes through **three states**: original (the disc), source (open formats) and cooked (for the GPU).
- The engine mounts **layers**: base, remaster, mods and user. The top one wins.
- Assets are identified with **stable IDs by name**, never with numeric indexes.
- Extraction **only goes one way**. We do not rebuild the disc or look for byte-for-byte equality.

The details are in the [asset pipeline](../architecture/assets-pipeline.md).

## Consequences

- No limits on Digimon, maps or any other content.
- Art is edited with common tools (Blender, Krita, text editors).
- The criterion for closing extraction is coverage: every file on the disc is extracted and the engine loads it.
- A schema must be kept for each asset type, along with the `dwx` tool.

## Alternatives rejected

- **Load the original formats in the engine:** it carries the PS1 limitations over to the runtime.
- **Byte-for-byte round trip:** it is only useful for rebuilding the disc, which we do not need.
