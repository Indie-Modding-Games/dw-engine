# 0008. Headers now, C++ modules later

- **Status:** Accepted
- **Date:** 2026-10-18
- **Note (2026-11-06):** accepted because the code already follows it: the code uses headers; there are no C++ modules.

## Context

C++ modules (`import`) are already supported by CMake, but clangd, clang-tidy and some compilers still have bugs with
them.

## Decision

**Headers** (`#pragma once`) are used, with the split between public API (`include/`) and private (`src/`). The
decision is reviewed in 2027.

## Consequences

- The tools work without surprises.
- The `include/dwe/<module>/` structure allows moving to modules later without reorganizing the repository.

## Alternatives rejected

- **Modules from the start:** the risk with the tools is too high for phase 0.
