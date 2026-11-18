# core

What all the modules use: `Result` and errors without exceptions ([ADR 0007](../../adr/0007-no-exceptions.md)), log (and `LogRing`/`keepLogTail`, the last lines in memory for bug reports), `formatMessage` for the message templates of each `messages.hpp`, `DWE_ASSERT`, `FixedStep`, the logic's fixed-tick clock, SHA-1, YAML (`parseYaml`/`emitYaml`, [ADR 0016](../../adr/0016-yaml-with-libyaml.md)) and `JsonSchema`, which checks YAML documents against a JSON Schema
([supported subset](../../architecture/json-schema.md)), and the math (GLM, through `<dwe/core/math.hpp>`, with the axes of [world space](../../architecture/world-space.md)). It depends on GLM, publicly, and on libyaml, which does not leave `src/`.

## Original trigonometry

`<dwe/core/original_math.hpp>` (`dwe::core::original`) has the original's integer calculations for what must land
on its numbers (orientations, steps, rings and particles): `atan4096` (`math.c: _atan`), `sin512` and `cos512`
(`math.c: _sin`, `_cos`), `rsin`, `rcos` and `ratan2` (those of the console's library) and `lerp`
(`effect_common.c`). Battle, field effects, death, evolution and the boss appearance use these and not a copy
of their own.

| Measure | Original | dwe before | dwe now | Status |
|---|---|---|---|---|
| `SIN_LOOKUP_TABLE` (129 entries, 512 per turn, 4096 = 1) | sine × 4096 rounded down, read from RAM | rounded to nearest: 65 of 129 entries one unit too high (2: 101 instead of 100) | all 129 equal (`original_math_tests.cpp`) | Same |
| `_sin` quadrants | the negative half is the table with the sign changed | same | same | Same |
| `_cos` | `_sin` a quarter turn later (128) | same | same | Same |
| `_atan` | table of 512 steps per octant | calculated table, checked in full against the disc | unchanged | Same |
