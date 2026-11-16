# 0015. Build inside a Docker container

- **Status:** Accepted
- **Date:** 2026-10-18
- **Note (2026-11-06):** CI no longer runs `check-full`. It runs only on pull requests: format and lint of what changes, build and tests (`linux`) and the cross build (`windows`). clang-tidy and the documentation site stay in `check-full` ([How to contribute](../contributing.md#workflow)).

## Context

The engine needs Clang 22, libc++, a recent CMake and vcpkg. Distributions ship different versions, and installing
them by hand clutters each developer's machine and produces builds that only work on one machine.

## Decision

- The build, tests, formatting and clang-tidy run inside a **Docker image** defined in `.devcontainer/Dockerfile`:
  Ubuntu 24.04, LLVM 22 from apt.llvm.org, vcpkg at a pinned commit and the CMake that pinned vcpkg commit sets (the
  one in Ubuntu 24.04 does not know C++26).
- Everything is launched with **`scripts/dev.sh`**. CI uses the same commands, so a build that passes locally passes
  in CI.
- The container runs as the host user, and the repository is mounted at the same path: the generated files belong to
  the developer and the paths in `compile_commands.json` also work outside.
- The vcpkg binary cache lives in a volume (`dwe-vcpkg-cache`), so dependencies are built once.
- The project's objects go through **ccache**, with its cache in another volume (`dwe-ccache`, or the folder in
  `DWE_CCACHE_DIR`). Paths are stored relative to the repository root, so a worktree benefits from what another one
  built. The presets put whatever `DWE_COMPILER_LAUNCHER` says in front of the compiler, which the image defines as
  `ccache`: with an older image, or on Windows with nothing defined, it builds the same, without a cache.
- The project does not scan the sources for C++ modules (`CMAKE_CXX_SCAN_FOR_MODULES` disabled, [ADR
  0008](0008-headers-before-modules.md)): each source was preprocessed twice, and `compile_commands.json` depended on
  files written by the build. Now clang-tidy only needs the `configure`.
- The checks have **two levels**, both in `dev.sh` and with the `ci` preset (warnings as errors):
  - `check-commit`, what each commit meets: configure, build and the tests except the slow ones (`ctest -LE slow`);
  - `check-full`, what the tip of a branch meets, and what CI passes: also the formatting, all the tests and
    clang-tidy with no diagnostics at all.

  Each step leaves its log in `build/logs/` (or in `DWE_LOG_DIR`) and also fails if the build log has a warning (the
  linker's are not covered by `-Werror`) or the clang-tidy one a diagnostic.
- The **slow** tests are the cases that read the user's disc ("the user's disc: ...") or the content extracted from
  it with `DWE_CONTENT` ("the user's disc, extracted: ..."). Each one is its own ctest test with the `slow` label, so
  they run in parallel with each other; a new case that `configure` has not seen yet runs in `<module>.disc`, so it is
  never skipped.
- Everything uses the machine's cores (`DWE_JOBS` limits it): ninja, ctest, clang-format and clang-tidy, which passes
  one process per file and prints each one's output all at once. In `check-full`, clang-tidy runs at lower priority at
  the same time as the tests, which are almost entirely one long disc case.
- **The game runs on the host**, which has the GPU and the screen. So that it works without installing anything, the
  executable links libc++ and SDL3 statically; it only needs the Vulkan loader and the GPU driver.
- `devcontainer.json` allows opening the same environment from VS Code or CLion.

## Consequences

- The only host dependency is Docker (and the GPU driver to play).
- The first `configure` takes a while, because vcpkg builds all the dependencies; after that it comes from the cache.
- CI departs from this in two ways to take less time without a cache, and neither changes the code that is built:
  - it does not install the Vulkan validation layer, which is the `validation` feature in `vcpkg.json`, on by default
    locally, because no test draws;
  - on Linux it uses the `x64-linux-libcxx-release` triplet, which builds each dependency once instead of twice. That
    triplet builds libdatachannel with AddressSanitizer, like the tests that use it: its `std::vector` code in
    release, uninstrumented, gets mixed with the engine's at link time and writes into the capacity that libc++ marks
    as poisoned, and ASan reports it as an overflow. In debug (`x64-linux-libcxx`) the same happens when the linker
    keeps the uninstrumented copy of a `std::vector` function (the WebSocket client does it when reading the URL), so
    its debug build, which only the presets with ASan use, also carries it.
- The vcpkg binary cache stores each package separately, so a new dependency only builds that one.
- The image cannot be newer than the system where the game is played: the executable links the image's glibc. That is
  why it is Ubuntu 24.04 even though 26.04 already ships LLVM 22 in its repositories.
- On Windows the container is only useful for building for Linux; the native Windows build has its own
  presets (`windows-clang-*`, `windows-ci`).
- From Linux, the `windows-cross` preset builds and links in the container the same thing as `windows-ci`, with the
  image's `clang-cl` and `lld-link` and the Microsoft libraries that xwin downloads (`dev.sh windows-sdk`) into a
  volume. Those libraries require whoever downloads them to accept Microsoft's license, and they never enter the
  repository ([Set up the environment](../getting-started/setup.md#the-windows-build-from-linux)).
- Before, checking a branch (format; build and tests of `linux-clang-debug` and `ci`; clang-tidy in series) took about
  26 minutes on a 12-core machine. `check-full` takes about 7.5 minutes with an empty ccache and about 5.5 with a full
  one; almost all of it is the longest disc case. The `linux-clang-debug` build is left out: it has the same options
  as `ci` except `-Werror`, so it did not check anything more.
- A commit on a branch already built in another worktree passes `check-commit` in half a minute, because almost all
  its objects come from the cache; before, the build took about 2.5 minutes.
- CI saves the ccache (Linux) and sccache (Windows) caches with `actions/cache`. On Windows the objects carry the
  debug information inside (`/Z7`), because a shared PDB (`/Zi`) cannot be cached.
- To share the cache between worktrees, the compiler receives the sources with paths relative to the build folder: its
  errors say `../../engine/...`. And the debug information of an object that comes from the cache may name the
  folder of the worktree that built it first: the files are identical, but the debugger may open them from that
  folder.

## Alternatives rejected

- **Install the toolchain on the host:** quick to start, but each machine ends up different.
- **Nix:** reproducible, but the team would have to learn it.
- **Colima or a VM:** it needs hardware virtualization, which not all machines have enabled.

To speed up the checks, the following were measured and rejected:

- **mold instead of lld:** linking all the executables adds up to about 5 seconds of CPU in the whole build.
- **Precompiled headers:** the standard library costs ~1.1 s per source, ~30% of the CPU of a cold build, but ccache
  already avoids repeating those builds, the PCH hides missing `#include`s and clang-tidy would depend on the build
  again.
- **Unity builds:** 25 test files repeat the same helper function in an anonymous namespace, and merging sources breaks
  the granularity of ccache and incremental builds.
- **A smaller test disc for the slow tests:** they check precisely that every file on the real disc has an extractor
  and that the whole pack validates; a cut-down version would lose that coverage.
- **clang-tidy only on the files changed in each commit:** a change in a header affects sources that do not change, so
  it guarantees nothing; the tip already passes it whole.
- **Fewer container starts:** each one costs less than a second.
- **Splitting Linux CI into several jobs:** each job would have to recreate the image and restore vcpkg, and
  clang-tidy already runs at the same time as the tests.
