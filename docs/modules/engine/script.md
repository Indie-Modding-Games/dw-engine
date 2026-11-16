# script

The Lua 5.5 virtual machine with sol2 ([ADR 0004](../../adr/0004-lua-and-sol2.md)) and the mods' only door
to the engine: a versioned and isolated Lua API. No other module gives Lua to mods.

## Sandbox

- No `io`, `os`, `package`, `debug`, `load`, `loadfile`, `dofile` or `string.dump`; `collectgarbage` only accepts
  `"count"`. `setmetatable` does not accept `__gc`: Lua runs finalizers outside of calls and without hooks, so
  no budget would stop them.
- Each call from the engine to Lua is a `ScriptCall` (or its internal version): the outermost one receives the full
  budget; the nested ones, such as a script that calls an API function that fires an event, share what
  is left. The Lua state is only reached with `ScriptCall::lua()`: a module that registers its API or calls Lua does it
  always inside a `ScriptCall`.
- Limits per call (`ScriptLimits`): instructions (`instructionsPerCall`, 10 million; Lua's count hook
  counts them every 1000, and each call pays those 1000 in advance, because preparing the hook restarts its count) and a
  time ceiling (`callDeadline`, 1 s). **The instructions decide** where a runaway script is stopped: the count is
  the same on any machine and under any load. The time ceiling is a generous safety net, about ten
  times what the instructions take in a release build (100 to 200 million Lua instructions
  per second), for the work that the count does not see, such as comparing large texts. It counts the thread's
  processor time (`CLOCK_THREAD_CPUTIME_ID`; on Windows still `std::chrono::steady_clock`), so the time the thread
  waits for a core on a loaded machine does not count; the processor clock is only read when the ceiling has already passed in
  `steady_clock`, which is cheaper. Nor does it count the time taken by the functions the engine publishes (loading a
  model, placing a character): that is the engine's work, not the script's, and it postpones the ceiling by as much as it lasted. Before, the
  map entry sections without waits (script 0, sections 0xd7 of room05b and 0xd8 of room06: they load models and
  place NPCs in one go, like the original, which runs them whole in one tick) failed because of the 20 ms ceiling of the
  resume with the machine at load ~40; now they pass with load ~49. What exhausts either one is stopped with "the script ran
  too long and was stopped", followed by the reason: ": it ran all the instructions of its budget" or ": it reached the
  time ceiling of its budget". `ScriptVm::lastBudgetStop()` gives the same reason as `BudgetStop` (`budget_stop.hpp`:
  `Instructions` or `TimeCeiling`) for the last outermost call, and the tests check that reason, never how long
  the stop took. Once stopped, every instruction fails, so `pcall` is no use for continuing. The `xpcall` handlers
  do not run once the script is stopped: Lua would call them with the hooks turned off. `xpcall` and the functions
  that charge by bytes call with a continuation (`lua_callk`), so a coroutine can `coroutine.yield`
  inside `xpcall`.
- The C functions that work in proportion to the length of their texts pay by bytes (64 bytes cost one
  instruction): the patterns of `string.find`, `match`, `gmatch` and `gsub` (`pattern_matcher.cpp`, the same
  behavior as Lua's, without `goto`), `string.rep` (before building the result), `upper`, `lower`,
  `reverse`, `sub`, `byte`, `char`, `format`, `pack`, `packsize`, `string.unpack`, `utf8.len`, `codepoint`, `char` and
  `offset`, each step of the `utf8.codes` iterator (by the bytes it covers), `table.concat`, `unpack`, `insert`,
  `remove`, `move` and `sort` without a comparator. The allocator also charges the bytes it reserves, so building large
  texts with `..` in a loop gets stopped. Coroutines share the budget: `coroutine.resume` and `wrap` prepare
  the coroutine's hook before resuming it. Preparing the hook restarts its count, so a coroutine that yields
  before reaching it would run for free: that is why its hook checks the budget every 64 instructions and each
  resume pays those 64 in advance. The hook belongs to each thread and its count continues across yields and resumes;
  each check charges the count it was prepared with (`lua_gethookcount`). This way, a coroutine that the engine creates and
  resumes from C++ (`lua_newthread` and `lua_resume` inside a `ScriptCall`, as the story runner does with
  `script_coroutine.hpp`)
  pays for what it runs even if it inherits the count of 1000 from its creator, and spends the budget of the call that resumes it.
- Comparing two texts (`<`, `<=`, `==`) is VM work and does not go through any function that could be charged by
  bytes. It is an accepted decision: each comparison covers at most one block (`largestBlockBytes`, 4 MiB), and a
  loop that compares large texts is stopped by the call's time ceiling (`callDeadline`).
- Memory: the total limit (`memoryBytes`, 64 MiB) keeps an eighth for the engine's own work outside of
  calls; each pack has its quota (`packMemoryBytes`, 16 MiB) and no block of a call exceeds
  `largestBlockBytes` (4 MiB). Each block carries in a header who paid for it, to give it back when it is freed. A
  script that goes over gets a memory error, it never stops the engine.
- The engine's and the interface's global scripts (`runAsset`, `runString`, `ScriptCall::lua()`) and every `ScriptCall` run in the
  engine's name, without a pack quota. It is an accepted decision: they are trusted code of the base and
  remaster layers, and the VM's total limit, the instruction budget and the time budget still apply to them.
- Code that runs in the engine's name only comes from a pack that the engine found in its own folders
  (`assets::PackOrigin::Engine`: the content folder, the team packs, the development ones) and that declares the
  base or remaster layer. Where the pack was found decides, never its `pack.yaml` alone, which the pack itself writes:
  a mod that declares `layer: base` is still a mod. `engine_code.hpp` (`isEngineCode`, `checkEngineScript`,
  `checkEngineDocument`) is the sole owner of that rule. `runAsset` uses it, and so does the UI: a mod's document is
  loaded, but its inline scripts and its event attributes (`onclick`…) do not run, and a `<script src>` only runs if
  the file is provided by an engine pack, whatever the document. Letting a mod
  program its own UI in its environment is for phase 5.
- The strings' metatable is locked (`getmetatable("")` gives "locked by the engine") and its `__index` is a
  read-only copy of the `string` library: what a script does to its `string` table does not change anyone's methods
  (`("a"):upper()`).
- `print` writes at most 100 lines per call, and the log cuts at 4096 bytes any text that comes from a
  script (lines, handler errors, reload errors).
- What each pack writes (its `print` calls and the errors of its handlers) also has a byte bucket
  (`pack_logs.hpp`): 16 KiB per second, with bursts of up to 64 KiB. What does not fit is discarded, with a warning when
  it starts and the count of discarded lines when it writes again. An error identical to the pack's last one is not
  repeated: it is counted ("the last error repeated N times") when a different one arrives, or every 10 s if it stays the same. If the
  errors stop, the pending count is written on the next frame after those 10 s (`beginFrame`), and all
  that remains on closing the VM, even if the pack's bucket is empty.

## A script's budgets, for mod authors

> **CONTRACT CHANGE (2026-10-22).** A pack's event handlers have fewer instructions than before:
> 250,000 per fired event (before 1 million) and 500,000 per frame (before 2 million). The time ceilings rise
> to 20 ms per event (before 5 ms) and 40 ms per frame (before 8 ms). A stop's error adds the reason. The
> calls (`instructionsPerCall`, 10 million; `callDeadline`, 1 s) do not change. Because the default budgets were lowered, the
> Lua API version (`LuaApiVersion`) goes from 1.0.0 to **1.1.0**. When `docs/modding/` has a
> budgets page, this section moves there.

Each call from the engine to your script has a budget, so that a broken or looping mod does not hang the game:

| What | Lua instructions | Time ceiling |
|---|---|---|
| A call (loading a script, an API function) | 10,000,000 | 1 s |
| Your pack's handlers for a fired event | 250,000 | 20 ms |
| All your pack's handlers in one frame | 500,000 | 40 ms |

- **What stops a script is the instruction count.** It is the same on any machine and under any load:
  a loop that stops on a slow PC stops at the same point on a fast one. Library functions that
  walk large texts or tables also charge for what they walk (64 bytes, one instruction).
- **The clock is only a generous ceiling**, about ten times what those instructions take in a release
  build. Only work that the count does not see reaches it, such as comparing large texts with `<` or `==` in a loop.
  Neither the machine's load nor the time taken by the engine's functions brings it closer. Do not count
  on it: a handler that needs more than 250,000 instructions should spread the
  work across frames.
- When it runs out, the script is stopped with an error that says the reason:
  - `the script ran too long and was stopped: it ran all the instructions of its budget`: it spent its instructions;
  - `the script ran too long and was stopped: it reached the time ceiling of its budget`: it reached the time ceiling.
- Once stopped, every instruction fails: `pcall` is no use for continuing. If a pack exhausts the budget of an event
  or of the frame, the rest of its handlers are skipped until the next one, and the log says so.
- Coroutines spend from the budget of the call that resumes them, also across yields.

## API version

> **CONTRACT CHANGE (2026-11-07).** `LuaApiVersion` goes from 1.3.0 to **1.4.0**: events can carry a versioned
> table (`HookTable`), the game publishes its events for mods and runs its `scripts/main.lua`
> ([Lua API](../../modding/lua-api.md)), and `api` is mandatory in the `mods` and `user` layers.

`LuaApiVersion` (`api_version.hpp`) is the semantic version of the whole Lua API. It is an alias of
`assets::SupportedLuaApi` (`packages/engine/assets`, `lua_api.hpp`), which owns it: the version is a data contract with
`pack.yaml`, and this way `dwx validate` checks it without depending on Lua. A pack declares in its `pack.yaml` the version that
its scripts ask for ([VFS](../../architecture/vfs.md)): `api: "1.x"`, any 1.y, or `api: "1.3"`, 1.3 or a
later one. `assets::checkPackApi` rejects another major version or a minor newer than the engine's. `packages/content` checks it on mounting and leaves out, with the reason, the mod that does not meet it.

A pack of the `mods` or `user` layers must declare `api` (since 1.4.0, #115): `checkPackApi` and
`modEnvironment` reject it without one, and `packages/content` leaves out, with the reason, the mod with Lua files that
does not declare it. A mod without `api` that brings no Lua files (data only, like the packs that the launcher and the mapper wrote before this change) is still mounted, with a warning in the log; a mod with Lua and no `api` is left out. The engine's packs (`base`, `remaster`) may omit it. The documentation for mod authors is
in [modding](../../modding/index.md).

Bumping the version: the minor when something is added (a function, an event, an optional argument); the major when
something a mod already uses is removed or changed.

## Mod environments

Each pack has its own environment (`ScriptVm::modEnvironment`, `ModEnvironment`):

- copies of the standard tables (`string`, `table`, `math`...): what a mod changes in them is not seen by another;
- `print` writes to the log as `mod <pack>: ...`;
- globals are strict: reading a global that does not exist or creating a new one is an error, so the mod's code
  uses `local`. `rawset(_G, ...)` does not create globals either;
- the API spaces (`hooks` and those the engine publishes) are read-only views: writing to them, `rawset`
  and `setmetatable` fail, and `getmetatable` returns "locked by the engine".

`runModScript(files, id)` runs the Lua asset `id` from the VFS in the environment of the pack that provides it, with the chunk
name `@<pack>:<id>` in errors. Only `.lua` files are accepted. `runAsset`, which runs with the engine's
globals, only accepts assets from the engine packs of the `base` and `remaster` layers (`isEngineCode`): a mod that
replaced that asset would have the engine's privileges, like the console commands.

`ModEnvironment::loadModule(files, id)` loads an asset of the pack as a module (`ScriptModule`), what the
Lua returns, without remembering it for reload. From the module, `coroutine(path)` makes a coroutine that the engine resumes with the
function found at that path (raw reads, without metatables), `value(path)` reads a simple value and
`functionKeys(path)` lists the integer keys of a table whose values are functions. `coroutine(path, function)` passes
the function, as its first argument, a function from **another** module of the same VM (or `nil` if there is none): this way
a story patch receives the section it wraps. That function keeps its module's environment, and the whole
coroutine is paid for by the pack of the module that creates it.

## Hot reload

`reloadChanged()` runs again the disk files (`runFile`) and the assets (`runAsset`) that changed. For a
mod it is a transaction: it creates a new environment, runs all its assets again and only if all go well does it swap the
environment and its handlers; if any fails, the previous one stays and the error goes to the log. The mods are walked by index,
because a script can create another pack's environment while reloading.

## Publishing functions: `ApiRegistry`

The owner of a C++ system publishes functions under a named space:

```cpp
vm.api().publish("field", "map_id", [](const script::ApiCall& call) -> core::Result<script::ApiValue> { ... });
```

In Lua it is `field.map_id()`. `ApiCall` gives the calling pack (`pack()`) and the arguments, which are scalars
(`nil`, boolean, integer, number, text); the result too. Tables will arrive as a minor bump when a
system needs them. An error comes back to Lua as a script error. The
names are `[a-z][a-z0-9_]*`; `hooks` is reserved and a standard global cannot be covered.
A value published with `publishValue` can also be named `[A-Z][A-Z0-9_]*`, like Lua constants
(`story.ENDED`).

## Events: `HookRegistry`

The engine declares `space.name` events and fires them with scalar arguments:

```cpp
vm.hooks().declare("field.map_entered");
vm.hooks().fire("field.map_entered", arguments);
```

An event is also fired with a table (`fire(event, HookTable)`, since 1.4.0): its scalar fields and
`version`, the table's version; each handler receives a new table, so what one changes is not seen by the
next. The game's events (`session::GameHooks`, `game_hooks.hpp`) are fired this way
([Lua API](../../modding/lua-api.md#game-events)).

A mod subscribes with `hooks.on("field.map_entered", function(t) ... end)`. Handlers run in the
order of the packs and, within each one, in the order they were registered. A pack's handlers
share a call, with its memory quota and a budget smaller than a call's: `instructionsPerHook`
(250,000, about 2 ms in release) and the ceiling `hookDeadline` (20 ms) per fired event. In addition, all of a
pack's handlers share a per-frame budget, `hookInstructionsPerFrame` (500,000) and the ceiling `hookTimePerFrame`
(40 ms); as with calls, the instructions stop first and the clock is only the generous ceiling. The module that carries
the frames calls `vm.hooks().beginFrame()` once per frame to refill it. When a pack exhausts that of
an event or that of the frame, the rest of its handlers are skipped, and the log says so at most once every 10 s.
If `fire` is called from inside a call (a script that calls an API function that fires an event),
each pack receives the smaller of its event budget and what is left of the call (and the deadline that
expires first), and the call pays for what it spent; a handler stopped by time does not count as having spent all its
instructions. A pack without handlers for the event does not open a call or pay anything. A pack registers at most 64
handlers per event. If one fails, the error goes to the log with the pack ("`mod <pack>: a handler of <event>
failed: ...`") and the others continue. `fire` returns how many ran, how many failed and how many were skipped.

## Console and tests

The global environment remains for the debug console and the autotest routes: the `input` table, the player's
texts as `t(key[, values])` and `texts.language()` ([localization](../../architecture/localization.md)), and
`bindCommand`, which adds global functions with a text argument for the console commands
(`text_font("original")`).
