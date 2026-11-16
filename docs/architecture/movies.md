# Movies (FMV)

Status: in code.

The four movies on the disc (`OP1`, `OP2`, `ED2`, `EDR`) are extracted as Y4M + WAV + `movie.yaml`
([format](../formats/video.md)) and the game plays them **as `dwx` writes them** where the original does, with its
skip rules and what comes after. The cooked format (AV1 + Opus) is described at the end.

## What the original does

| Fact | Locator |
|---|---|
| Table of 4 movies: file, first frame and `frameCount`; the movie ends when the frame whose number reaches `frameCount` is shown (OP1 is cut at 3637 of 3719) | `mov.c: MOV_MOVIES`, `MOV_readFrame` |
| Playback: 320 × 176 at 30 fps, centered on a 240-high screen (y = 32), interleaved XA audio at the CD's pace; if the file is not there, it returns without doing anything | `mov.c: MOV_playMovie`, `MOV_initEnvironment` |
| Skipping: the Start button (`0x800`) on any frame, except in movie 3 (EDR), which cannot be skipped | `mov.c: MOV_playMovie` (the `do … while` condition) |
| Before each movie the music is stopped and afterward it is restarted; after it, a black screen and a fade from black (40) | `main.c: main`, `utils2.c: playMovie`, `startMovie` |
| 0 OP1: at startup and every time the "press Start" screen goes 600 frames without a press (demo loop) | `main.c: main`, `runLandingScreen` |
| 1 OP2: new game, after the name scene and before the starting map (`MAPHEAD 0xCC`) | `main.c: main` (`MAIN_STATE` 0), `newGameScene` |
| 2 ED2: when loading a save made after the ending (`mainState` 2 of the save), before entering the map | `main.c: main` (`MAIN_STATE` 2), `main_menu.c: gameClearSave`, `loadSavegame` |
| 3 EDR: when the ending finishes (routine 53 → `MAIN_STATE` 3), after closing the save; then OP1 and the title screen | `script_interp.c: tickScript` (case 53), `main.c: main` |

What is not ported (PS1 machinery): the CD's ring reading, the 2000-read retry, the MDEC buffers, VRAM and double
buffering, the 24-bit mode. The EU version (another table and another position) is not ported.

## Original functions → dwe modules

| Original | Destination in dwe |
|---|---|
| `MOV_MOVIES` | The package's `movie.yaml` (`table`, `dwx` already writes it): the final frame is disc data |
| `MOV_playMovie` (loop, end and skip condition) | `packages/engine/video`: `MoviePlayer` (clock, current frame, end); `apps/game`: `MovieScreen` (skip, drawing, voice) |
| `MOV_readFrame`, `MOV_decodeFrame`, `MOV_startStream`, `MOV_startRead`, `MOV_onSliceDecoded`, `MOV_waitForFlip` | `packages/engine/video`: `Y4mReader` (frame by frame, in chunks) + `toRgba`; the rest is machinery and disappears |
| `MOV_initEnvironment` (y = 32 in 240) | `MovieScreen`: a rectangle with the 320 × 240 proportion and the movie centered, scaled to the window; with "Widescreen pictures" (on by default), the 320 × 176 image alone, fitted to the window with its proportion (`movieFrame`; [game](../modules/apps/game.md#widescreen-pictures)) |
| `DecDCTvlc2`/`DecDCTin` + XA audio | already in `formats/video` (`mdec`, `xa_adpcm`); `dwx` uses them when extracting |
| `startMovie`, `playMovie` (stop and restart music, fade) | `apps/game`: `MovieScreen` with `GameAudio` and `ScreenFade` |
| `main` (which movie and when), `runLandingScreen` (600) | `apps/game`: the app's states (`game.cpp`, `AppFlow`); numbers and rules in the ruleset (`movies`) |
| Routines 53 (ending) and 54 (`gameClearSave`) | `PlaySession::requestMovie` asks for ED2 (`ClearedLoad`) and EDR (`Ending`) |

Ruleset, `movies` section: per movie, `file` (`op1`…), `skippable` (EDR `false`) and `trigger` (`boot`,
`new_game`, `cleared_load`, `ending`); and `landing_idle_frames: 600`.

## Player

```text
apps/game (MovieScreen) ──► packages/engine/video ──► packages/engine/assets (VFS in chunks, decodeWav), packages/engine/core
                        ──► packages/engine/render (texture + QuadBatch + ScreenFade), packages/engine/audio (voice on the music bus)
```

- **`packages/engine/video`** ([its page](../modules/engine/video.md)), a systems-layer module: `Y4mReader` (reads the
  Y4M frame by frame from an `assets::FileReader`: it never holds the whole file, which is about 300 MB), `toRgba`
  (from YCbCr to RGBA with interpolated chroma and the exact BT.601;
  [its page](../modules/engine/video.md#color)) and `MoviePlayer`: it reads `movie.yaml`, gives the RGBA frame due at
  a given time and says when it ends (final frame = `playback.frame_count`, or the last one in the file if that is
  larger). `MoviePlayer::loadSound` decodes the whole WAV (about 18 MB). It knows neither `render` nor `audio`.
- **Reading in chunks:** `assets::FileSystem::openFile` (by default it reads the whole file; the disc reads with
  `ifstream` and the `.pak` expands zstd in chunks and checks the SHA-1 when reading the last byte) and
  `VirtualFileSystem::open`.
- **Sync:** the audio is the master. The clock is `Audio::position(VoiceId)` (the clip frame that the audio thread
  last mixed) divided by the WAV's frequency; without audio, game time. If the game stalls, frames are skipped (like
  the original, which never waits for the video).
- **`MovieScreen`** (`apps/game`): creates the 320 × 176 texture once and updates it with `queueTextureUpdate` on each
  new frame, draws it centered in 320 × 240 with black bars (the movie covers the scene and the UI), plays the WAV on
  the music bus and skips with the `pause` action (the gamepad's Start) or `menu` (Tab) if the movie is `skippable`.
  The music stops while the movie plays and the map's song restarts afterwards
  ([below](#cooked-format-and-later-work)); the fade from black after the movie is not done.
- **Missing movie** (package without `movie/`): a warning in the log and the flow continues, like a failed
  `CdSearchFile` in the US.
- **App states** (`game.cpp`): startup → OP1 → title screen; "Press Start" screen `landing_idle_frames` ticks without
  Start → OP1 → title screen. Like `runLandingScreen`, the count starts when entering that screen and only Start cuts
  it (other keys do not restart it); the start menu, the slot lists and the language box never go back to the movie,
  like the original's `runMainMenu`. The menu script signals with `title_landing`; the modern look, without that
  screen, does not go back to the movie;
  new game → OP2 → starting map; `PlaySession::requestMovie` asks for ED2 (after loading a finished save) and for EDR (closes the game → EDR → OP1 → "Press Start" title screen: the original's `main` loops back
  around and puts `playMovie(0, 1)` before `runLandingScreen`; `movieAfter`). An autotest and the direct starts (`--new-game`, `--load`, `--start`) do not see movies.

## Scaling

An engine improvement (`apps/game/src/movie_drawer.cpp`), with the `texture_filter` texture filter. With "Upscaled"
(`upscaled`, the default) the 320 × 176 image is enlarged in two steps, an edge-adaptive upscale and a
contrast-adaptive sharpen, written for this case (there is no third-party code):

- **Edge-adaptive upscale** (`movie_upscale.slang`, in the scene pass): each window pixel is a Lanczos-2 of the 12
  nearest texels. The local luma gradient rotates the kernel along the edge and stretches it in that direction, so
  edges come out straight and crisp instead of stair-stepped or blurry; in flat areas the kernel is round and
  smoother. The result is clamped to the minimum and maximum of the 4 nearest texels: the negative lobes leave no
  halos.
- **Sharpen** (`movie_sharpen.slang`, in the post pass `FrameDesc::post`, over the scene drawn into a window-size
  texture): a 5-tap cross with a weight limited per pixel (without going past the neighbors' minimum or maximum),
  weaker where there is noise; strength 0.8.

With "Smooth" or "Pixels" the movie is stretched with a linear filter, as always. The frame (`showPicture`, the
picture's rectangle) does not change, nor does the clock: the frame is still `floor(t · 30)` with `t` the sound's
position.

## Contract changes

1. **Public API of `packages/engine/audio`**: `Audio::position(VoiceId)`, additive.
2. **Public API of `packages/engine/assets`**: `FileReader`, `FileSystem::openFile` (virtual, with a default
   implementation) and `VirtualFileSystem::open`, additive.
3. **Ruleset**: `movies` section and `landing_idle_frames`, additive.

No changes to the package (`ExtractorVersion` stays the same), the save or the `story` API.

## Cooked format and later work

- **AV1 + Opus cooked format (done, #105).** `dwx cook` cooks each movie: the Y4M into `video.ivf` (AV1 in an IVF,
  with libaom: BSD-2-Clause + AOMedia Patent License 1.0, through vcpkg) and the WAV into `audio.opus` with the usual
  sound cooker; `movie.yaml` is copied. `dwx extract` still writes Y4M + WAV: it is the source that gets cooked, and
  the distributable package is the cooked one (#227). The engine chooses the reader by extension (`video::Av1Reader`
  or `Y4mReader`, `decodeOpus` or `decodeWav`), so it plays both forms. libaom is also used for decoding (a single new
  dependency, which builds in `windows-cross`); IVF instead of WebM, with no container to add. Fixed, deterministic
  settings: constant quality `cq-level` 24, `cpu-used` 6 of the good-quality mode, one thread per movie (cooking
  already spreads the movies across threads), a keyframe at most every 300, full range. The development container's
  `nasm` builds libaom's assembly.
  - **Measurements** (US extraction, 4 movies, debug `dwx` with libaom in release, shared machine): 1171.8 MB of Y4M +
    WAV become 27.4 MB (video 20.7 MB, sound 6.7 MB); the `.pak` of the four, 27.3 MB. Cooking the four in parallel,
    4 min 33 s (a single one, 3 min); `dwx pack`, 3.5 s. ED2 quality against the Y4M: mean PSNR 42.6 dB (Y 41.7;
    minimum 38.1 on one frame).
  - **Quality (done, 2026-11-08).** What was lost along the way: the hardware IDCT (table rounded down, 8 bits, wrap in
    9 bits), the truncated dequantization, the chroma repeated in 2 × 2 squares, the MDEC's approximate matrix and an
    8-bit AV1 at `cq-level` 24. Now `dwx extract` decodes precisely, removes blocks and halos (MPEG-4 annex F.3) and
    writes 10-bit Y4M ([video](../formats/video.md#restoration)); `dwx cook` cooks 10-bit AV1 at `cq-level` 14;
    `toRgba` interpolates the chroma and uses the exact BT.601; with "Upscaled" the image is enlarged and sharpened on
    the GPU ([Scaling](#scaling)). No temporal filter or in-between frames: the movies are real 30 fps (in OP1 539 of
    3719 frames are repeated and in ED2 216 of 2652, and one frame is seen for 2 ticks), and blending frames would
    smear fast motion. The clock and the frame do not change: at the same ticks the same source frame is seen before
    and after.
    - **Measurements** (US extraction, 4 movies): Y4M from 1105.8 MB to 2211.6 MB (double, because of the 10 bits);
      cooked video from 20.7 MB to 37.3 MB (ED2 3.8 → 7.2; EDR 4.9 → 9.2; OP1 6.0 → 10.3; OP2 6.0 → 10.6); the sound
      does not change. Cooking the whole package, 5 min 15 s → 5 min 48 s. Cooked ED2 against its Y4M: mean PSNR
      46.7 dB (before 42.6), worst frame 41.6. In the same test with `cq-level` 24 / 18 / 14 / 10: 3.3 / 5.0 / 7.4 /
      11.0 MB and 43.1 / 44.8 / 46.8 / 49.3 dB (SSIM 0.988 / 0.991 / 0.994 / 0.996); 14 is where the difference stops
      being visible when enlarged.
  - **Music during the movie.** The original stops the music before the movie and starts it again after (`main.c:3441`
    `finalizeMusic`, `playMovie`, `initializeMusic`); dwe did not, so ED2 on a finished save could play with the map's
    song underneath. Now `SessionMusic::holdForMovie` stops it and holds whatever the field asks for while it lasts,
    and `releaseAfterMovie` then plays the map's song from the start.
  - **Remaining:** the field still updates under ED2 (animations and some partner sound effect); the original has not
    loaded the field yet at that moment.
- The EU version (another table and another position).
