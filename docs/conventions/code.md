# Code conventions

## Language

- C++26 ([ADR 0002](../adr/0002-cpp26.md)), following the
  [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines).
- Only the features that all the CI compilers support are used. This list is extended with a PR when a compiler adds
  something new.
- No exceptions: recoverable errors are returned with `std::expected` ([ADR 0007](../adr/0007-no-exceptions.md)).
- Headers with `#pragma once` ([ADR 0008](../adr/0008-headers-before-modules.md)).
- No hand-written `new` or `delete`: RAII and smart pointers, or the `core` allocators.

## Names

| What | Style | Example |
|---|---|---|
| Files and folders | `snake_case` | `evolution_rules.cpp` |
| Namespaces | Follow the folder | `dwe::game::partner` |
| Types, classes and enums | `PascalCase` | `EvolutionRule` |
| Functions | `camelCase`, with a verb | `loadPack`, `tickCare`, `isSleeping` |
| Variables and members | `camelCase` | `hunger`, `currentMap` |
| Private members | `camelCase` with a trailing `_` | `tickSeconds_` |
| Constants and enum values | `PascalCase` | `MaxPartySize`, `Weather::Rain` |
| Macros (only if there is no alternative) | `UPPER_SNAKE` with a prefix | `DWE_ASSERT` |

- One word per concept across all the code: if it is `pack`, it is not also `bundle`.
- No magic numbers or strings: they are named with an enum or a constant, in the header of the owning module.
- The name states the responsibility: what the function does or what the variable holds, with no abbreviations or
  generic names (`data`, `tmp`, `helper`, `manager`).
- Names come from the game when they exist (see the [glossary](../glossary.md)).

## Design and readability

- Single responsibility: each module, class and function does one thing. If its name needs an "and", it is split.
- The code reads like prose: short functions, one level of abstraction per function and no one-line tricks.
- Each constant has a single owner: it is declared once and the rest include it, never copy it.
- Error and log messages are not inline in the logic: they live as named constants (text or `std::format` template) in
  the module's `messages.hpp`. A message used in several places comes from a single constant.
- The texts the player sees come from the content (packs and localization), never from the code.
- **No `goto` anywhere:** not in C++, not in the Lua we write, not in the Lua that `dwx` generates. Control flow is
  structured and modern: if/else, loops, `break`, early returns, helper functions and, if needed, loops with state.

## Code that comes from the decomp

- The decomp is the reference for **behavior**, not for code. Nothing is copied as is: it is rewritten with these
  conventions (names by responsibility, own types and enums, no offsets or `unkXX`, short functions).
- What is kept is the result: the same formulas, probabilities and order of effects, with the reference to the decomp
  in a short comment where it helps to verify it.

## Comments

- The code is documented only with good names; comments are few, short and precise.

- Every public function has a short comment that says what it does.
- The remaining comments explain **why**, never repeat what the code says.
- No commented-out code and no TODO in the code: what is pending goes in an issue.
- When a strange behavior of the original is reproduced on purpose, it is said, with the reference to the decomp.

## Format and analysis

- `clang-format` is mandatory: the configuration is in `.clang-format` and the CI checks it.
- `clang-tidy` with the repository profile and warnings as errors in `check-full` and in the gate (the pull request CI
  does not run it).
- `check-commit` and `check-full` fail if a C++ or Lua source in the repository contains `goto` (`no-goto` step), and
  `clang-tidy` applies `cppcoreguidelines-avoid-goto`. The script converter tests check the same in the generated Lua.
- ASan and UBSan in the debug builds.

## Tests

- **Unit:** in each module's `tests/`, with doctest. They carry the ctest label of their package:
  `scripts/dev.sh test --filter <package>` runs those of one and `scripts/dev.sh test --affected` those of what
  changes ([build and test a package](../getting-started/setup.md#build-and-test-one-package)).
- A test's name goes in **a single literal**: ctest registers each slow case by its name, read at `configure`. An
  entry whose `--test-case` filter matches no case (a name split into several literals, a test renamed since the last
  `configure`) fails with a message from `tests/main.cpp`; a case that skips itself (without `DWE_DISC` or
  `DWE_CONTENT`) does match and passes. A case is skipped by **returning** (`return`), not with `doctest::skip()`: with
  that decorator it matches no filter and its entry would fail. A `ctest` that finds no test also fails
  (`noTestsAction` in `CMakePresets.json`).
- Without exceptions a failed `REQUIRE` **does not stop** the case: it is logged and the case runs on. Code after it
  that relies on it (dereferencing an empty `std::optional`, indexing past a vector's end) hits a libc++ hardening
  check, which traps: the case dies with SIGILL and ctest reports `ILLEGAL` instead of the failed check. Where a
  `REQUIRE` guards later code, follow it with `if (!value) { return; }`.
- **Integration:** the autotest routes in `tests/autotest/`, which play the game with simulated input
  ([vertical slice](../guides/vertical-slice.md)), and the example packs in `tests/mods/`.
- **Simulation:** in `apps/sim` (`dwe-sim`), for ruleset balance (for example, a thousand raisings)
  ([simulations](../guides/simulations.md)).
- **Render:** there are no tests against reference screenshots yet; the routes save screenshots for review.
- Every behavior change brings its test, and checks what the GDD says (see [design cycle](design-cycle.md)).
