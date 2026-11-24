# Passing a branch's gate

> **Optional since the pull request flow.** What is needed to merge into `main` is the CI checks on each pull request
> ([How to contribute](../contributing.md#workflow)). The gate is still the way to pass locally, with the
> content of your disc, what the CI cannot: the autotest routes ([routes](../modules/tests/autotest.md)), cooking and
> clang-tidy.

`scripts/gate.sh` runs the gate on the commit your worktree is on (the tree has to be clean). It runs it at one of two
levels, under the machine-wide gate lock:

| Level | When | What it runs |
|---|---|---|
| `short` (default) | Before opening a pull request that touches the game | `dev.sh check-short`, the Windows cross build and the smoke routes, plus the package's own routes |
| `daily` | Before a big merge, or by hand on `main` | `dev.sh check-gate` in full mode (the `check-full` steps), the Windows cross build and all the routes: every `route-*.yaml` file in `en` and `es` (the save-and-load pair counts as two) on the extracted content and 4 runs on the cooked `.pak` |

It only works on a Linux with Docker, Xvfb and lavapipe. On Windows there is no gate: the manual steps are still in
the [vertical slice guide](vertical-slice.md).

```sh
# Short gate of a package that adds route-my-route and touches an extractor
DWE_CONTENT=<extracted content> DWE_DISC=<disc image> DWE_IMAGE=dwe-dev-speed DWE_JOBS=6 \
    scripts/gate.sh --routes-extra route-my-route --disc-tests "the user's disc, extracted: mapas"

# Daily run on main
DWE_CONTENT=<extracted content> DWE_DISC=<disc image> DWE_IMAGE=dwe-dev-speed scripts/gate.sh --tier daily
```

When it finishes it writes `summary.txt` in its logs folder and exits with 0 only if everything passed. The options
are these:

| Option | What it does |
|---|---|
| `--tier short\|daily` | The level |
| `--tidy full\|incremental` | `short`: the scope of clang-tidy (default `incremental`). `daily` always runs it whole |
| `--cook full\|incremental` | `daily`: cook from scratch (default) or reuse the cache |
| `--routes-extra a,b` | `short`: `tests/autotest` routes (without `.yaml`) that are also run, in `en` and `es` |
| `--disc-tests <regex>` | `short`: the disc tests that are run with the disc, a ctest expression; if it matches none, it fails. Needs `DWE_DISC`. The other disc tests are skipped |
| `--routes parallel\|sequential` | The routes on several displays at once (default `parallel`) or one after another on a single one. Use `sequential` for a bisect or if the machine is very loaded |
| `--displays N` | How many displays the parallel mode uses, from 1 to 4 (default 4) |
| `--lp-threads N` | Sets `LP_NUM_THREADS` (lavapipe's threads) in each run |
| `--steps check,windows,routes` | Only some steps. Each step that is skipped leaves `gate: <step>=skipped reason=--steps ...`, and the result is `PASS (partial: ... skipped)`: that is not a gate |
| `--repeat N` | Repeats the route list N times, to measure intermittent failures |
| `--lock <folder>` | The lock. By default `_work/logs/p4-locks/full.d` if `_work/logs` exists next to the main checkout |
| `--logs <folder>` | Where the logs go. By default `_work/logs/daily-<date>` (`daily`) or `_work/logs/<branch>/gate-<time>` (`short`) |

## The steps

**The lock.** Before anything else, the gate waits for the lock with an atomic `mkdir` and writes in `owner` the
branch, the tip, the time and its PID. It also waits while there is another `full-*` lock folder next to it (for
example `full-other.d`); a file with that name, like a log, does not count. While it creates the folder and writes
`owner`, it holds signals back, so an interruption at that moment does not leave a lock without an owner. On exit it
releases it, also if it fails or you interrupt it, and only if `owner` is still its own or was never written.

**Free space.** Before the lock, the gate asks for free space on the worktree's disk according to the steps you are
going to run, and stops if there is none:

| Step | GB | Why (measured on 2026-10-22) |
|---|---|---|
| check | 12 | `build/ci` from scratch takes 7.3 GB, cooking 2.0 GB and its cache entry another 2.0 GB |
| windows | 2 | The short one reuses `build/windows-cross` (4.4 GB) and the daily one deletes it before building again: it grows little |
| routes | 1 | The logs and captures of the daily's 42 runs on 2026-10-22, before the save-and-load pair, took about 80 MB |

A complete gate asks for 15 GB; only the routes (`--steps routes`), 1 GB.

**Interruptions.** The steps in Docker run in the background and the gate waits for them with `wait`. This way, a
`SIGTERM` or a `Ctrl+C` in the middle of an hour-long check stops the gate at once: bash does not handle a signal
until the foreground command ends, but it does during a `wait`.

**The preflight check.** With the lock already taken, and before any heavy step, the gate checks that everything the
steps you are going to run need is there:

- For the check and Windows, which run in Docker, it notes in the summary the image and its ID
  (`gate: image=<name> id=<sha256>`). The image is `DWE_IMAGE` or, if you do not give one, `dev.sh`'s default
  (`dev.sh image-name`); the gate sets none. Then it looks inside it, without downloading anything (`--pull=never`),
  for its tools:
  - the check: `cmake`, `ninja`, `ctest`, `ccache`, `clang++`, `clang-format`, `clang-tidy`, `python3`, `stylua`,
    `shfmt`, `luacheck`, `shellcheck`, `node` and `vitepress`;
  - Windows: `cmake`, `ninja`, `clang-cl`, `lld-link`, `llvm-lib`, `llvm-rc` and `llvm-mt`.
- The routes run the game on the host, so it checks on the host for `Xvfb`, `timeout` and the lavapipe driver
  (`/usr/share/vulkan/icd.d/lvp_icd.json`). If you skip the check, also that the game is already built
  (`build/ci/apps/game/dwe-game`).

If something is missing, it stops in seconds with `gate: preflight=fail reason=...`, which says what is missing and
where, instead of failing halfway through the gate. It happens, for example, with an image older than the Windows
cross build: use the one your team says (`DWE_IMAGE=...`).

**The check.**

- `check-short` passes the format, the lint (luacheck and shellcheck), builds the documentation site (it fails with a
  broken link) and passes `no-goto`, configures and builds the `ci` preset without warnings, and runs all the tests,
  without leaving out any label. That pass runs without `DWE_DISC` or `DWE_CONTENT`, so the disc and extracted-content
  tests are skipped at once. At the same time it runs clang-tidy (see below). Then, if you give `--disc-tests`, it
  runs those tests, now with the disc and the content. It does not cook: the log says so with
  `gate: cook=none reason=...`.
- `check-gate` does the same steps as `check-full`, but it decides the scope of clang-tidy and whether to reuse the
  cooking. With both on `full` it is the same as `check-full`, and it also records the complete passes for the
  following gates. The CI uses neither: on a pull request it passes the format and the lint of what changed, builds
  and runs the tests (`.github/workflows/ci.yml`).

Each step's logs are left in `check-logs/`. With `--disc-tests`, `summary.txt` adds one line for each disc test that
ran: its name, the exact filter (`--test-case=...`), the assertions doctest counted and the result
(`disc-test <name> | filter=... | assertions: N | N passed | 0 failed | result=Passed`). A test with
`assertions: NONE` checked nothing: look into it.

**Windows.** `windows-cross` configures and builds with 0 warnings, at all levels. The `daily` level builds in a new
folder. The short one reuses the worktree's `build/windows-cross`, because a new folder costs 30 to 40 minutes.

That an incremental build prints no warnings is not enough to trust it:

- it does not recompile the sources that have not changed, so it does not repeat their warnings;
- `/WX` turns a warning in the project's code into an error, but not one in third-party code or one from the linker.

That is why, after a build with 0 warnings, the gate leaves a mark in the folder with the tip, the image and the hash
of its `.ninja_log`. According to that mark, the next gate builds in one of three ways:

| Mode | When | What it does |
|---|---|---|
| `incremental` | The mark exists, the image is the same and nobody has built in the folder since (same `.ninja_log`) | Incremental build |
| `rebuild` | There is no mark, or the folder was built outside the gate | Cleans what was built of the project (`--target clean`) and builds it all again. It is short thanks to the compiler cache, which also repeats each source's warnings |
| `fresh` | There is no folder, the image changed, or it is the `daily` level | Deletes `build/windows-cross` (only that one, inside the worktree; it refuses if `build/` is a symbolic link) and builds from scratch |

The log says it with `gate: windows-cross=<mode> reason=...`. A build that fails, or that leaves any warning, leaves
no mark.

**The routes.**

- `short` runs the smoke routes of `smoke_routes`, at the top of `scripts/gate/route_jobs.sh` (now
  `route-loop-keyboard`, `route-keyboard` and `route-save-load`), in `en`. Between the three they cover startup, the
  main menu, a new game, the first map, the field, the menu and saving and loading a game. Then it runs those from
  `--routes-extra` in `en` and `es`.
- `daily` runs all the `tests/autotest/route-*.yaml` ones (there is no fixed count: a new route enters on its own),
  with the options each one needs:
  - `route-dev-assets` with `--dev-assets`;
  - `route-mod-map` with the example mod;
  - `route-skins` with `tests/mods/ui_skin`;
  - `route-save-load` with `--saves <run folder>/saves`, and in the same job, only if it passes,
    `route-save-load-check` with the same folder and `--load slot_01` (see below);
  - those in `saves_routes` (`route-sleep-save`, `route-skin-original`, `route-game-modes` and `route-randomizer`), with an empty
    `--saves <run folder>/saves`, because they start a new game in a slot and save into it;
  - the 4 on the cooked `.pak`, in which it also checks that the bitmap map font was loaded.

**Routes that follow another.** Some routes check what another one left and never run alone: they are in
`follow_up_routes`, at the top of `scripts/gate/route_jobs.sh`. Right now there is one: `route-save-load` saves with the
debug command `save("slot_01")` into a new saves folder inside its own (`<language>/route-save-load/saves`), and
`route-save-load-check` starts with `--load slot_01` from that folder and checks the map, the player's position, the
partner, the clock and the bag. Both go in a single job, one after the other on the same display, so they stay
together when the routes run in parallel. Each has its line in the summary; if the first does not pass, the second
comes out as `NOT RUN` and counts as a failure. `--routes-extra route-save-load` runs the pair; the second alone
cannot be requested. When writing the job list, the gate empties that saves folder, so that a slot left by another
run cannot pass the check; it only deletes a `saves` folder inside its own logs, never another one (nor
`_work/saves-play`). The routes header in the summary says `jobs=` (jobs) and `runs=` (runs: one more for each pair).
`scripts/gate/tests/test_follow_up.sh` checks it with simulated runs.

Before the routes it writes `dev-assets.yaml` (in list form) at the repository root and deletes it afterwards. If
there is already one, it does not overwrite it and stops: move it yourself.

## Parallel routes

The routes run in parallel by default, at all levels. The gate:

1. Looks for free displays between `:72` and `:79`. A display is free if neither `/tmp/.X<n>-lock` nor
   `/tmp/.X11-unix/X<n>` exists.
2. Starts its own Xvfb on each. Xvfb takes the display's lock by itself, so if another takes it at that moment, only
   that start fails and the gate moves on to the next. A display counts when the Xvfb is still alive and its socket
   exists.
3. It never uses `:71` (the reference emulator's) or `:61` to `:66`. If there are fewer free displays than requested,
   it runs with the ones there are and says so.
4. Each display has a worker (`worker` in `scripts/gate/route_jobs.sh`) that takes the next job nobody has taken (an
   atomic `mkdir` per job). A pair of routes that follow another (see above) is a single line in the list, a single
   job: its two runs go one after the other on the same display, never split. `test_follow_up.sh` checks it with three
   workers at once. The order is from longest to shortest, according to the previous gate's times (a pair's counts its
   two runs), so that the long ones do not end up last.
5. Each run writes its summary line to a file of its own. When done they are joined in the list's fixed order, so
   `summary.txt` has the same lines, in the same order, as in sequential. You can compare them with
   `diff <(grep ' rc=' a/summary.txt) <(grep ' rc=' b/summary.txt)`.
6. It retries nothing: a route that fails is a failure.

The gate only stops what it started, and never by name:

- each Xvfb and each run is noted by its PID in `pids/`;
- each run is a `timeout` that heads its own process group, with the game inside, and that group is stopped;
- each step in Docker is an `sg docker` that heads its own session (`setsid`), and its group is stopped;
- before stopping anything, it checks with `ps` that the process is still the one it started;
- the order is first the workers, then the runs, then the steps in Docker and last the Xvfbs;
- the Docker containers are named `dwe-gate-<level>-<branch>-<gate PID>-<step>-...`; if the gate is interrupted with
  one running, it stops only those that start with its own prefix, which carries its PID.

`dev.sh` names all its containers: `DWE_CONTAINER_NAME` (or `dwe-<worktree folder>`), the command, the PID of the
shell that starts it and a counter. This way anyone who sees a container knows whose it is and what it does.

Parallel mode is the default since two pairs of runs, sequential and parallel on the same tip, gave identical
summaries, with no intermittent failures, one with `--lp-threads` and one without it (2026-10-22, 42 runs each, before
the save-and-load pair): from 1096 s to 380 s and from 1123 s to 373 s, about 3 times faster on 4 displays.
`--lp-threads` changed nothing measurable, so it is left unset.

## Incremental clang-tidy

With `--tidy incremental` (`DWE_GATE_TIDY=incremental` in `dev.sh`), clang-tidy reviews only what has changed since
the last complete pass that was accepted.

**The reference.** It is the last complete pass that meets all of this:

- it passed less than 24 hours ago;
- it used the same Docker image and the same clang-tidy version;
- its commit is in `main` and below the tip.

If there is none, it is run whole.

**What is reviewed.** First the files that differ between that commit and the working tree are listed. With that
list it is run whole if any of these has changed:

- a header in a public `include/` folder;
- a `.clang-tidy`;
- the build or the toolchain: `.devcontainer/`, `tooling/cmake/`, `tooling/triplets/`, the root `CMakeLists.txt`,
  `CMakePresets.json`, `vcpkg.json` or `scripts/dev.sh` (which is what runs clang-tidy);
- a `*.in` `configure_file` template (like `apps/dwx/src/bundled_recipes.hpp.in`). The header it generates lives in
  the build folder, and the include graph does not see who includes it.

If not, these are reviewed:

- the changed `.cpp` files that are in `compile_commands.json`;
- all the `.cpp` files that include a changed header, directly or through other headers;
- all the `.cpp` files of a module whose `CMakeLists.txt` has changed.

**The include graph.** It comes from the `#include` lines of the repository's sources. Each include is resolved
against the folder of the file that includes it and against all the repository's `-I`, `-isystem` and `-iquote`
folders that appear in `compile_commands.json`. The graph prefers overreaching to falling short:

- an include inside an `#ifdef` counts;
- if a name matches several files, all of them count.

**The log.** `tidy-plan.log` and the beginning of `tidy.log` say:

- the scope and the reason, with `gate: tidy=full reason=...` or `gate: tidy=incremental reason=...`;
- each `.cpp` reviewed and why (`tidy: check <file> (changed)`, or `(includes changed <header> via ...)`);
- each changed file that is not reviewed and why (`tidy: not checked`);
- each `.cpp` that is skipped for not having changed (`tidy: skip`).

**The markers.** A complete pass that passes with a clean tree leaves a marker: the commit, the date, the image and
the version. The following gates start from it. The daily run on `main` is the one that leaves the markers that serve
the short gates. If one day it does not run, the short gates run clang-tidy whole.

## Reusing the cooking

With `--cook incremental` (`DWE_GATE_COOK=incremental`), `check-gate` reuses a previous cooking instead of cooking.

**The key.** It is a sha256 of everything that goes into the cooking, and the log breaks it down part by part
(`gate: cook-key part ...`):

- the extracted content of the `base` pack: the path and the sha256 of each file;
- the git tree of each module `dwx` is built with, which includes the extractor and the cookers. The modules come from
  `ninja -t inputs apps/dwx/dwx` and from the headers `ninja -t deps` lists for its objects; this way a module that
  only has headers also goes in;
- the `CMakeLists.txt` of each folder above those modules (for example `packages/engine/CMakeLists.txt`);
- the git tree of `.devcontainer/`, `tooling/cmake/`, `tooling/triplets/`, `content/enhance/` (the recipes `dwx`
  carries inside), the root `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json` and `scripts/dev.sh`;
- the Docker image and the preset.

The key does not use the hash of the `dwx` binary: it carries the worktree's path inside and would never match
between worktrees.

**When it is reused.** Only if all of this holds:

- there is a saved cooking with that key;
- it is less than 24 hours old;
- its commit is in `main` below the tip;
- the tree is clean;
- nothing that goes into the cooking has changed since that commit;
- the copy restored in `build/gate-cooked` has the sha256 of the `.pak` and of the cooked folder that were noted when
  saving it.

Even when it is reused, the two `dwx validate` runs are run again, because the `game` pack changes with every branch.

**When not.** Any other case is a cache miss: it cooks from scratch, as in `check-full`, with no error loops. The log
says `gate: cook=hit` or `gate: cook=miss`, with the key and the reason.

**How it is saved.** A cooking is saved only after passing both validations. It is first copied to a temporary
folder, which is renamed when it is complete. At most two cookings are kept (about 2 GB each).

## Where the state is kept

The clang-tidy markers, the cooking cache and each route's times are in `dwe-gate/`, inside the repository's common
git folder (`git rev-parse --git-common-dir`). All the worktrees share it and it is never uploaded.
`DWE_GATE_STATE_DIR` changes it.

## What the log says

Each gate's log says, with fixed prefixes:

- the level: `gate: tier=`;
- the clang-tidy scope and its reason: `gate: tidy=`;
- the cooking key with all its parts, and whether it was a hit or a miss: `gate: cook=` and `gate: cook-key`.

## Windows

All this runs on the Linux host or in the container. On Windows there is no `gate.sh`, and the tests in
`scripts/gate/tests` are not registered (`if(NOT WIN32)` in the root `CMakeLists.txt`), so the Windows CI does not
change.

What does affect several platforms:

- **The git paths** are read with `-z` and treated as POSIX.
- **The Windows cross build** is still a mandatory step of both levels.
- **The sources that only Windows builds.** A `.cpp` that is not in the `ci` preset's `compile_commands.json` is
  listed as not reviewed, not accepted. clang-tidy did not review it on Linux before this gate, and neither does it
  now.
