# 0007. No exceptions, with `std::expected`

- **Status:** Accepted
- **Date:** 2026-10-18
- **Note (2026-11-06):** accepted because the code already follows it: the code builds with `-fno-exceptions` (`tooling/cmake/DweModule.cmake`) and returns errors with `std::expected`.

## Context

Game engines usually disable exceptions: the error flow is explicit, performance is predictable and it is easier to
cross the boundary with Lua and with C libraries.

## Decision

- It is built with `-fno-exceptions`.
- Recoverable errors are returned with `std::expected<T, Error>`.
- What must never happen is checked with asserts.

## Consequences

- Every error is visible in the function signature.
- Dependencies that throw exceptions are wrapped at the edge of the module that uses them.

## Alternatives rejected

- **Exceptions:** the error flow is hidden and the cost is less predictable.
