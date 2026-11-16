# 0016. YAML with libyaml, wrapped in `core`

- **Status:** Accepted
- **Date:** 2026-10-19

## Context

The project's source format is YAML ([ADR 0005](0005-asset-pipeline.md), [ADR 0010](0010-own-rules-with-rulesets.md)):
pack manifests, map data and game tables. The engine has to read it and `dwx` has to write it deterministically, so
that extracting the same disc twice gives the same files. Everything is built without exceptions
([ADR 0007](0007-no-exceptions.md)), and the files can come from mods, so the input is not trustworthy.

## Decision

- **libyaml** is used (C, vcpkg `libyaml` port), only inside `packages/engine/core/src`. The public header
  `<dwe/core/yaml.hpp>` exposes a tree of its own (`YamlNode`), `parseYaml` and `emitYaml`, and does not include
  `yaml.h`.
- Only a subset is accepted: one document per file, no anchors, aliases or tags, no repeated keys and with scalar
  keys. There is a limit on size and on nesting.
- Scalars are stored as text; whoever reads a field decides what it means.

## Consequences

- Every error is returned with `core::Result` and gives the line and the column.
- Without aliases there are no expansion attacks ("billion laughs") and the nesting limit avoids exhausting the stack.
- The output is stable: insertion key order, double quotes for what is text, LF and a final newline.
- libyaml builds the same on Linux and on static Windows; its C API is wrapped with RAII in a single file.

## Alternatives rejected

- **rapidyaml:** fast, but without exceptions its error callback cannot return; we would have to exit with
  `setjmp`/`longjmp`, which skips the destructors.
- **yaml-cpp:** it reports errors with exceptions.
- **fkYAML:** header-only and it also uses exceptions.
- **A parser of our own:** YAML is big and subtle; it is not worth maintaining.
