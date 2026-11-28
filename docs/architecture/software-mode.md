# Software mode

When no graphics card can run the game, the launcher starts it on lavapipe, Mesa's Vulkan driver that draws with the
processor ([launcher: compatibility mode](../modules/apps/launcher.md#computer-requirements)). The game notices it on
its own, starts from a cheap preset, adjusts the quality to the frame time while it runs and tells the player once per
run. Everything stays an option the player can change.

## When it is on

`apps/game/src/software_mode.hpp`. The game asks the device it created for its type (`rhi::deviceGpu`): a
`VK_PHYSICAL_DEVICE_TYPE_CPU` device (lavapipe, llvmpipe) is software. `--software-mode auto|on|off` decides:

| Value | What it does |
|---|---|
| `auto` (default) | On with a CPU device. Autotests keep it off, so routes keep the look their captures expect |
| `on` | Always on, also on a real GPU: to try the preset, the notice and the tuner, and to measure them |
| `off` | Never |

## The preset

At start the game picks a quality level from the threads lavapipe draws with (`LP_NUM_THREADS` when it is set, else
the machine's): 2 or fewer start at 0, up to 4 at 1, up to 8 at 2, more at 3. Each level sets:

| Level | Render scale | Shadows | Model textures | Enhanced lighting, vivid colors | Weather |
|---|---|---|---|---|---|
| 0 | 50 % | Off | Pixels | Off | Off |
| 1 | 75 % | Off | Pixels | Off | Off |
| 2 | 100 % | Round | Pixels | Off | Off |
| 3 | 100 % | Round | Smooth | Off | On |
| 4 | 100 % | Sun | Smooth | On | On |

At every level antialiasing is off, scenery quality is smooth (the upscaler is not run) and the frame limit is 30.
"Automatic quality" starts on.

**The player's choices win.** The preset only sets the options that `settings.yaml` does not hold, and it never
writes them there: it applies again on every start while the player has not chosen. Which options are automatic is
kept by `GraphicsChanger::automatic()` (`GraphicsKnobs`, built by `unchosenKnobs`); as soon as the player changes
one in the menu it is saved, it stops being automatic and neither the preset nor the tuner touch it again.
`--low-performance` still wins over the preset for the six look options (render scale and frame limit stay
automatic).

## Automatic quality

`QualityTuner` and `decideQuality`, pure and tested in `apps/game/tests/software_mode_tests.cpp`. Only while a field
is on screen (no movie, no load), and only on the automatic options:

- A **window** lasts 3 s and at least 20 frames. It measures each frame's busy time: the frame time without the
  frame limit's wait. A frame of more than 1 s (a load) starts the window again.
- **Down** one level when the window's mean busy time is over the budget (1 / frame limit, or 1/30 without a limit)
  by 8 %, and **two** levels when it is 75 % over (a slow machine reaches a playable level in one window).
- **Up** one level when the 90th percentile leaves 40 % of the budget unused.
- **Hysteresis.** After a change the first 2 s are not counted, and a level the tuner had to leave becomes the
  ceiling for the rest of the run, so it never swings between two levels. Between the two thresholds nothing
  changes.

The option "Automatic quality" (`auto_quality`) turns it on or off; it is on by default only in software mode.

## The new options

They are in Options > Graphics in every mode, after vertical sync, and saved in `settings.yaml`:

| Option | Values | What it does |
|---|---|---|
| Automatic quality (`auto_quality`) | on, off | The tuner above |
| Render scale (`render_scale`) | 100, 75, 50 | The scene pass draws at that fraction of the window (`FrameDesc::sceneScale`, `sceneTargetExtent`) and the post pass stretches it, nearest texel; the UI stays at the window's size. A frame without a post pass gets the renderer's own (a `SceneGrade` with neutral values). Mouse walking scales the pointer to the scene (`FieldScene::pickGround`) |
| Frame limit (`frame_limit`) | off, 60, 30 | The game loop sleeps to keep frames at most that many a second (`FrameLimiter`); a late frame does not make the next ones hurry |

`--graphics scale=100|75|50,fps=off|30|60,auto=on|off` sets them for one run.

## The notice

`ui/software_notice` shows for 12 s at start, over everything and taking no input: "Playing without a graphics
card", that the processor draws the game so it may be slow, that the quality adjusts itself and where to change it,
and how to fix it (install or update the graphics card's driver from its maker's site). It is translated into the
nine languages (`localization/<lang>/software_notice.yaml`). The `software_mode` setting says whether it applies.

## Measurements and minimum requirements

Measured on 2026-11-24 on one machine: an AMD Ryzen 5 3600 (6 cores, 12 threads, up to 4.2 GHz, AVX2), 31 GB of
RAM, Mesa's lavapipe (LLVM 20, 256-bit vectors) under Xvfb at 1280x720, release build. The machine was shared with
other jobs (load average 2 to 4 during the runs), so the numbers are a rough guide, not a benchmark. The route stands
20 s in File City; the steady average skips the first frames. "Before" is the game without software mode
(`--software-mode off`), "after" with it, "preset" with it but without the frame limit nor the tuner.

| `LP_NUM_THREADS` | Before | After | Level the tuner reached | Preset without limit |
|---|---|---|---|---|
| 1 | 8.8 fps | 30.0 fps | 0 (50 %) | 39.2 fps |
| 2 | 15.4 fps | 30.0 fps | 1 (75 %) | 66.8 fps (50 %) |
| 4 | 26.4 fps | 29.8 fps | 3 (100 %) | 77.1 fps (75 %) |

On a longer route with an evolution and 2 threads the tuner went up to level 4, missed the budget and settled at 2
for the rest of the run (27.8 fps average, where the 30 fps limit is the most). The game peaked at 830 MB of memory.

Minimum requirements without a graphics card, from these numbers and leaving room for older processors, which do
less work per core than this one:

- **Processor:** 64-bit with 4 threads (2 cores with two threads each, or 4 cores) at 3 GHz or more, with AVX2
  (most from 2015 on). lavapipe draws with all but what the game itself needs; with one drawing thread this machine
  holds 30 fps at 50 %, so an older processor of this class should stay playable at level 0, with some drops.
- **Memory:** 4 GB (the game uses under 1 GB; the rest is the system's).
- **Better:** 6 threads or more at 3.5 GHz or more reach full scale at 30 fps.

Below that the game still runs, but under 30 fps even at level 0.
