# 0004. Lua 5.5 + sol2, sandboxed

- **Status:** Accepted
- **Date:** 2026-10-18

## Context

The story, events, dialogs, quests and game modes must be editable without recompiling, and mods must not be able to
break the engine or touch the user's system.

## Decision

- **Lua 5.5.1**, the most recent version (August 2026).
- **sol2 3.5** for the C++ bindings.
- Scripts run in a **sandbox**: no `io`, no `os`, no loading native code, with memory and instruction limits.
- The Lua API is exposed **only** from the `script` module and is versioned with semver.

## Consequences

- Design and writing can work in text, with hot reload.
- The Lua API is a public contract: breaking changes bump the major version and come with a migration guide.
- Lua 5.5 is recent and sol2 supports it with a compatibility patch; if it causes problems, we pin the version that
  works.

## Alternatives rejected

- **LuaJIT:** faster, but it stayed at Lua 5.1.
- **A language of our own:** it costs a lot to maintain and nobody knows it.
