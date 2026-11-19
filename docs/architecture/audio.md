# Audio

Status: **implemented**.

This document says how the game sounds: the `packages/engine/audio` module (output to the device, mixer, voices and
volumes), how the effects (WAV or Opus) and the music (MIDI + SF2) reach it, how scripts request them and what a
mod sees. The citations to the decomp (`dw_decomp`, `src/`, `us` version) are given as `file.c: function`, only for reading the
behavior, never for copying code or tables.

## 1. Scope

The module covers: output through SDL3 or the null output, mixer, voices, volumes (master, music and effects) and the
voice limit; effects (WAV or Opus) with the chip's ADSR envelope; music (MIDI sequencer and SF2 synthesizer) with its
transitions; the chip's reverb (§7.4); and the volume rows of the in-game options
([game.md, Volume](../modules/apps/game.md#volume)). The menu sounds and the story's `play_sound`, `set_bgm` and
`stop_bgm` commands go through the ports described in §2 and §6.

## 2. Modules and dependencies

```text
apps/game ──► packages/engine/audio ──► packages/engine/platform (SDL3), packages/engine/assets, packages/engine/core
packages/session ──► packages/engine/audio (only the public API: request sounds and music)
packages/game/story ──► (SoundPort port, declared in packages/game/story; implemented by packages/session)
```

- **`packages/engine/audio`** is a module of the systems layer (`dwe_add_module(audio LAYER systems ...)`), as the
  [overview](overview.md) already says ("mixing and synthesis"). It depends on `core`, on `platform` (which links SDL3) and
  on `assets` (the `assets::Sound` type and `decodeOpus`). It **does not know `packages/game/` or `apps/`**: it does not know what a
  map, a script or a Digimon is; it only plays clips and sequences on buses.
- **The device (SDL3).** [ADR 0003](../adr/0003-vulkan-and-sdl3.md) already chooses SDL3 for audio. The SDL output
  lives in `packages/engine/audio/src` (as `rhi` uses SDL for the Vulkan surface) and is private: no public header of
  `audio` includes SDL. `platform::Platform` does not change: the output starts its own subsystem
  (`SDL_InitSubSystem(SDL_INIT_AUDIO)`, which SDL counts by references) and closes it when destroyed.
- **`packages/game/*` does not see `audio`** (rule 3 of the [dependency rules](dependency-rules.md): it requests things through the public
  API, and rule 4 requires doing so through interfaces). `packages/game/story` declares a port (`SoundPort`: play a
  sound, set or stop the music) as it already declares `StoryStatePort`, and `packages/session` implements it on top of
  `audio::Audio`. That way the runner can still be tested with a double and `dwe-sim` runs with the null output.
- **`packages/session`** may use `audio` (it is a system). It does not create the device: it receives an `audio::Audio` already made.
  `apps/game` creates it with the SDL output; `dwe-sim` and the tests, with the null output.
- **The resolution of what sounds** (which bank occupies slot 8 on this map, which track corresponds to a battle) belongs
  to the game, not to `audio`: it lives in `packages/session` (or in the `packages/game/` system that needs it, through its port) and reads
  the pack's data (`data/maps`, `data/music`, the `bank_meta.yaml` and `font_meta.yaml` files).

Structure of the module:

| File | Responsibility |
|---|---|
| `include/dwe/audio/audio.hpp` | `Audio`: the game thread's facade (request and stop voices, volumes, `update`) |
| `include/dwe/audio/clip.hpp` | `Clip`: 16-bit PCM, frequency, channels and loop; `checkClip` |
| `include/dwe/audio/voice.hpp` | `VoiceId` (slot + generation) and `PlayRequest` (clip, bus, gain, pan, pitch) |
| `include/dwe/audio/mixing.hpp` | `Bus`, `VolumeControl`, the mix format and the guards (`MaxVoices`, `CommandQueueCapacity`) |
| `include/dwe/audio/output.hpp` | `MixSource`, the `Output` interface, `NullOutput` and `openDeviceOutput` (SDL) |
| `src/mixer.{hpp,cpp}` | The audio thread's side: applies the commands, volume ramps, mixing and clipping |
| `src/voice_player.{hpp,cpp}` | A voice: position, resampling, loop, pan and stop fade |
| `src/spsc_queue.hpp`, `src/orders.hpp` | The lock-free SPSC queue and the commands and notices that go through it |
| `src/sdl_output.cpp`, `src/null_output.cpp` | The two outputs |
| `src/messages.hpp` | The module's log and error messages |

Effects: `include/dwe/audio/effect_bank.hpp` (`EffectBank`: an effects bank with its samples; each effect, one
voice per tone) and `src/envelope.{hpp,cpp}` (the chip's ADSR envelope). In `assets`, `decodeWav` (`sound.hpp`) and
`readSoundBank` (`sound_bank.hpp`).

Music: `include/dwe/audio/sound_font.hpp` (`SoundFont`: an SF2 loaded into the synthesizer, which the game thread
keeps alive), `src/music_player.{hpp,cpp}` (the sequencer and the synthesizer on the audio thread) and `src/tsf.cpp`
(the only unit that compiles TinySoundFont, which comes in as a port of its own in `tooling/vcpkg-ports/tinysoundfont`).

`apps/game/src/game_audio.cpp` opens the device or, if there is none, the null output with a warning, and the loop calls
`Audio::update` with the time of the ticks that advanced. The session receives the `audio::Audio` for its
slots (`session::SessionSound`, the `SoundPort`) and `MenuSounds` plays the menu sounds.

The public API and its use, in summary, are in the [module page](../modules/engine/audio.md).

## 3. Threads

There are two threads: the game's (the `apps/game` loop, at 60 Hz) and the audio one (the SDL callback, which asks for samples
when it needs them, every few milliseconds).

- **Golden rule: the game logic never reads the audio thread's state.** If a wait depends on a sound
  (a command that waits for it to finish, if the original has one), its duration is calculated on the game thread with
  the clip's duration and the game clock, not by asking the device. That way a game is deterministic and saveable
  the same with sound, without it or with the null output, and the autotest routes do not depend on the sound card.
- **From the game to the audio:** each call to `Audio` (play, stop, change a volume) writes a command to a
  fixed-capacity SPSC queue, with no locks or memory allocation. The commands of a tick are **published together** at the end
  of `Audio::update()` (a single `store` with `memory_order_release`), so the audio thread applies all those of a tick
  in the same block and the order of the sounds of a tick is preserved.
- **From the audio to the game:** a second SPSC queue returns notices: "voice finished" (by id). `update()` reads them.
- **Clip memory.** The audio thread never allocates or frees memory, never blocks and never writes to the log.
  Clips are immutable; `Audio` keeps the `std::shared_ptr<const Clip>` of each live voice on the game thread and the
  audio thread only uses a non-owning pointer. The reference is released in `update()` when the notice arrives that
  the voice finished, never inside the SDL callback.
- **Voice ids.** `Audio::play` returns the `VoiceId` at once (it is handed out by the game thread: index + generation),
  so a voice can be stopped in the same tick in which it was requested.
- **Full queue.** The capacity (`CommandQueueCapacity`, 1024 commands per tick) is a generous guard, not a limit
  of the original: if it fills up, the command is discarded and `update()` logs it once per tick (warning, not error).
- **Volumes.** They are commands too. The audio thread applies them with a 10 ms linear ramp so that an
  abrupt change does not click.

## 4. Outputs

`Output` is the boundary with the device. The mixer does not know which one is behind it.

| Output | When | What it does |
|---|---|---|
| SDL3 | `dwe-game` with audio available | `SDL_OpenAudioDeviceStream` on the default device, stereo `float` at 48 kHz. The SDL callback asks for bytes; the output mixes into a buffer reserved beforehand, in chunks of at most `MaxRenderFrames`, and delivers them with `SDL_PutAudioStreamData`. SDL converts to the device's format and follows the default device if it changes |
| Null | tests, `dwe-sim`, `--check-content` and any run without a device | There is no thread: `Audio::update(seconds)` advances the mixer on the game thread by as many frames as are due (48,000 × seconds, accumulating the fraction) and throws the result away. Voices advance, finish and notify just as with sound. Tests can ask for the mixed buffer to check it |

- **No device is not an error.** If SDL cannot open audio (a container with no sound server, the Xvfb of
  the routes, a machine without a sound card), `apps/game` logs a **warning** with the reason and continues with the null output. The
  autotest routes still finish with 0 `[error]`.
- **A hung sound server does not hang the game** (a PC improvement). On some desktops PipeWire accepts the
  connection and never answers: SDL stays waiting inside `SDL_InitSubSystem(SDL_INIT_AUDIO)` and the game
  never got past startup (it did not even handle `SIGTERM`, because the loop that reads it had not started). Now
  `openDeviceOutput` opens the device on a thread of its own and waits at most `DeviceOpenTimeout` (3 s). If it does not
  come back, it logs a warning, leaves that thread behind and the game continues with the null output, silently. On closing, if that
  thread is still stuck (`deviceOpenStuck`), `dwe-game` ends the process with its code without calling `SDL_Quit`, which
  would wait for the thread forever. If the device opens late, a warning is given and the game continues silently until the
  next start. To check it without a broken PipeWire, a socket that accepts and does not answer is enough:
  `PIPEWIRE_REMOTE=<socket> dwe-game ...` still reaches the field.
- **Linux backends.** The static SDL3 from vcpkg ships by default with no Linux audio backend, and the game
  found no device on the desktop. The overlay port `tooling/vcpkg-ports/sdl3` adds the feature
  `system-audio`, which `vcpkg.json` enables on Linux: it compiles the ALSA, PulseAudio and PipeWire backends against the
  system headers (`libasound2-dev`, `libpulse-dev`, `libpipewire-0.3-dev`, already in `.devcontainer/Dockerfile` and in
  the development images) and loads their libraries at startup (`SDL_*_SHARED`). A machine with PipeWire or PulseAudio
  sounds without configuring anything (`SDL_AUDIO_DRIVER` undefined), and one without them still starts, silently and with the
  warning above.
- **Device pause.** When minimized or on losing focus the game continues as today (the loop decides); the output does not
  pause on its own. What the original does on pause (whether the music continues) is applied from the game.

## 5. Mixer and voices

- **Internal format:** stereo `float` at 48 kHz (the frequency of the cooked Opus and the one requested from the device). The
  sum is done in `float` and the output is clipped to [−1, 1]. The original's chip also saturates (16 bits), so a hard
  clip is the faithful choice; no compressor is added.
- **Buses:** `Music` and `Effects`, plus the master. A voice's gain is
  `voice gain × its bus volume × master volume`. The volumes that `audio` receives are linear, from 0 to 1
  (clamped to that range). The curve that goes from the options slider to that gain is Q5's (`volumeGain` of
  `apps/game/src/volume_setting.hpp`: the step from 0 to 10, squared).
- **Pan:** from −1 (left) to 1 (right), per voice. A mono clip uses constant power (Q4: −3 dB per side
  at the center, like TinySoundFont); a stereo one, balance (both sides at full in the center).
- **Resampling:** each voice advances with a step `clip frequency / 48,000 × pitch`, with linear interpolation. The
  cooked effects already come at 48 kHz (step 1); the WAV files of the development path go from 2,756 to 44,100 Hz.
- **Loops:** a looping voice goes back to `loop.start` on reaching `loop.start + loop.length` until it is stopped;
  without a loop, it ends at the end of the clip and notifies.
- **Stopping a voice** lowers it to 0 in 5 ms before releasing it, so that it does not click. (The original also lowers it
  quickly with `SsUtAutoVol` before releasing the note, and then the envelope's release sounds; here the fade
  plays that role, also with the envelope, §6.4.)
- **Voice limit (`MaxVoices` = 256).** The original has 24 channels on the sound chip: effects use only
  14 (10 to 23) and the music can use any, so in busy scenes an effect can cut another or a
  music note (§7.3). Here that limit is **not** reproduced: it belongs to the machine, not to a game rule. 256 is a generous
  **non-original guard** against a script or a mod that request sounds nonstop; the disc does not come close. On
  reaching the limit, the new voice replaces the **oldest** (the original, with no priorities, steals in turn, which is
  the closest thing) and a warning is logged, once per tick. It is tested.
- **Pitch and frequency guards (non-original).** A clip plays at a frequency from 1 to 192,000 Hz
  (`MaxClipSampleRate`) and a voice with a pitch from 1/64 to 64 (`MinPitch`, `MaxPitch`); a pan has to be a
  finite number. Outside of that `Audio::play` fails with the reason, so a voice never jumps more than a few hundred
  frames of its clip per output frame, and the loop wrap is calculated in one go (`fmod`). If a NaN still
  reaches the mix, the output turns it into silence. They are tested.
- **No end is lost.** The mixer only takes a command if the end notice that it can cause fits (the voice that
  it replaces); if it does not fit, the command waits for the next pass. A finished voice whose notice did not fit retries on
  every pass, and its slot is not reused before.
- **Cost.** 256 voices with linear interpolation are a few million operations per second: well below
  one core. The synthesizer has its own voice limit (§6.3).

## 6. Assets

`audio` does not read the file system: it receives clips, the SoundFont and the sequence already read. Whoever
loads them (the session, with the VFS) uses what `dwx` already produces ([Audio](../formats/audio.md),
[Opus](../formats/opus.md)); this is how the packs are already built.

### 6.1 What is in the base pack

| Asset | Extracted (`dwx extract`) | Cooked (`dwx cook`, `.pak`) |
|---|---|---|
| Effect samples: `sound/<bank>/sample_<n>` (`ss`, `sl`, `sb`) and `sound/<bank>/<entry>/sample_<n>` (`esall`, `vball`, `vlall`) | 16-bit mono PCM WAV, at the frequency of its key, with `smpl` if it has a loop | Ogg Opus at 48 kHz with `LOOPSTART`, `LOOPLENGTH` and `DWE_UNITY_NOTE`; **same id** |
| Banks: `bank.sf2` and `bank_meta.yaml` | Lossless SF2 + YAML (programs, tones, envelopes, `sounds`: value → program, note and tones) | Copied as is |
| Music: `music/<source>/font.sf2`, `track_<nn>.mid`, `font_meta.yaml` | SF2 + format 0 SMF with `loopStart`/`loopEnd` markers (meta 06) + YAML (`plays: forever` or `once`, the loop count) | Copied as is |
| `data/music.yaml` | `bgm_tracks` (the jukebox list, with the `font` and the `track` of each song of `MAIN_D_801303B8`), `enemy_music_mapping` and `field_overrides` (the fixed pairs of the modes of variable 245) and `battle` (the battle source and its tracks) | Same |
| `data/maps` | `sound_id` of each map (the `ESALL` chunk of the map) | Same |

Asset ids carry no extension, so the same reference (`sound/ss/sample_002`) gives the WAV on the development path
and the Opus in the cooked pack. `assets` has a PCM WAV reader (`decodeWav`) that
returns the same `assets::Sound` as `decodeOpus`, with its loop and its note.

### 6.2 Effects

An effect of the original is a **note** of a program of a bank: `playSound(slot, v)` plays note 60 + `v % 16`
of program `v / 16` (slots 0, 1 and 8) or note `v` of program 0 (slots 3–7), and all the tones whose
`max` is that note sound. `bank_meta.yaml` already resolves it in `sounds` (value → program, note, tones). On the US disc there are
661 sounds; 15 have more than one tone and 19 samples have a loop.

Each tone that sounds is a voice of `audio` with its sample, its volume (tone × program × bank), its pan, its pitch
(1 unless the sample is shared by a tone with another root: the ratio comes from the YAML) and the tone's `adsr` envelope,
with the same approximation that [Audio](../formats/audio.md#soundfont) documents. The alternative (playing the effect as a
note of `bank.sf2` with the synthesizer) is question Q2.

Which bank occupies each slot is decided by the game, as in the original: 0 `ss`, 1 `sl`, 3 the partner's `vlall`, 4–7
the `vball` files of the loaded Digimon, 8 the map's `esall` (`sound_id`) or `sb` in tournaments and VS mode. The table
type → entry of `VLALL`/`VBALL` comes from the `used_by` of each `bank_meta.yaml` (bank → types); see
question Q3.

### 6.3 Music

- **Own sequencer** (in `audio`): reads the format 0 SMF, applies the tempo, the program changes, the pitch
  bend with its range (RPN 0) and the `loopStart`/`loopEnd` markers; `plays: once` stops at the end. It is small, has no
  dependencies and is deterministic, and no generic MIDI player understands the loop markers.
- **SF2 synthesizer:** a library with a **permissive license**, and only one. The proposal is
  [TinySoundFont](https://github.com/schellingb/TinySoundFont) (`tsf.h`, MIT, a single header, no dependencies): it reads
  any SF2 (also those of a mod or a remaster), mixes to `float` and lets you set the frequency and the number of
  voices. It is not in the vcpkg baseline (checked: there is no other permissive port either; `fluidsynth` is LGPL‑2.1 and
  is ruled out), so it comes in as an **overlay port** of the repository pinned to a commit and its SHA‑512, with its
  license in `share/`. How it comes in is question Q1. No license has to be accepted and there is no click-through step.
- The synthesizer runs **inside the audio thread** as one more source of the `Music` bus, with its own voice
  limit (a non-original guard of 128). The sequencer advances by audio frames, so the tempo does not depend on the game's frames
  (the original's does not either: its sequencer runs at 240 ticks per second, apart from the game's 20 frames).
- Loading a SoundFont (that of a music source takes up to a few hundred KB) is done on the game thread; the
  source change passes to the audio thread as a pointer, and the old source is released in `update()` (§3).

### 6.4 Effects in detail

**`assets`.**
- `decodeWav(bytes) → Result<Sound>`: 16-bit PCM, mono or stereo, with the loop from the `smpl` block and the root note
  (`dwx` writes `sample_<n>.wav` that way). It rejects everything else with the reason (another format, headers that do not add up,
  more than `MaxSoundFrames`). It is the development path; cooked packs bring Opus with the same id.
- `SoundBank` and `readSoundBank(yaml) → Result<SoundBank>`: from `bank_meta.yaml` it reads `vab.master_volume`, the
  `programs` (volume, pan and their tones: `min`/`max`, root, `shift`, volume, pan, sample, `adsr`), the
  `samples` (file id and source frequency) and `sounds` (value → program, note, tones). It validates ranges and
  indices on reading; a broken `bank_meta.yaml` fails when the bank is loaded, not when the sound is played.

**`audio`.**
- **Envelope** (`src/envelope.{hpp,cpp}`): the chip's, from the tone's two `adsr` registers (linear or exponential
  attack, exponential decay down to the sustain level, sustain that rises or falls), counted in 44.1 kHz ticks and
  advanced by mix frames, without allocating memory. It is the chip's public documentation, not the
  game's code. A looping voice that the sustain lowers to 0 ends on its own. There is no release phase: stopping a
  voice is still the 5 ms fade, like the original's quick drop (§7.2).
- `PlayRequest` gains an optional field `envelope` (the two registers); without it the voice sounds without an envelope.
- **`EffectBank`** (`include/dwe/audio/effect_bank.hpp`): a `SoundBank` plus its samples (`EffectSample`: the
  clip and the frequency it was encoded at, loaded on the game thread). `play(Audio&, sound) →
  Result<vector<VoiceId>>` launches one voice per tone with gain = (tone vol./127)·(program vol./127)·(bank
  vol./127), pan = ((tone pan − 64) + (program pan − 64))/64 clamped to ±1, the tone's envelope and
  pitch = chip frequency / the sample's source frequency (it holds the same for WAV and Opus). The chip
  frequency is 44100·2^(steps/192), with steps = (note − root)·16 + (shift >> 3) in sixteenths of a semitone (`shift`
  raises the pitch), clamped to the ceiling of the pitch register, 0x3FFF/0x1000·44100. That is how the original calculates it:
  `note2pitch2` of libsnd (`asm/main/psyq/libsnd.s:8717`, US `0x800D0C7C`), which `SsUtKeyOnV` calls from
  `sound.c:621`. The source frequency is that of the sample's header; that of `bank_meta` only counts if the
  header does not have it, and if they do not match a warning is given once on loading. `play` checks all the voices (and that
  their commands fit) before launching any (`Audio::playAll`): an effect sounds whole or not at all. There is no
  `stop` of its own: each voice is stopped with `Audio::stop`.

**Game.**
- `packages/game/story` declares `SoundPort` (`playSound(slot, value)`), like `StoryStatePort`; the runner knows nothing of banks.
- `packages/session` implements it: slot 0 → `sound/ss`, 1 → `sound/sl`, 8 → `sound/esall/<map's sound_id>`, or
  `sound/sb` from when a battle starts (`btl.c: loadBattleData` → `sound_async.c: loadSB`, `startBattleBank`). When it
  ends, only a victory loads the map's again, also on top of a scene's or routine's
  (`battle_main.c: BTL_handleVictorySequence` → `loadMapSounds2(getMapSoundId)`; measured: `ACTIVE_MAP_SOUND_ID` is -1
  in battle, `11_battle_agumon`, and 0, the mayo00 bank, in the final box after winning, `13_battle_end_box`); after fleeing or losing the original
  reloads nothing and `sound/sb` stays in slot 8 until a map or a scene loads another (`endBattleBank(won)`;
  tournament battles go back to the arena's bank, `std_setup.c: loadMapSounds(0x10)`). The map where the game starts or is loaded also gives its bank (`attachField`). Each bank is loaded the first time its slot requests it, and that of the
  previous map is released when the map changes; a bank that fails to load leaves its slot silent with a single warning. Slots 3–7 are
  not requested by the disc's scripts (§10, Q3), but by the clips (`anim.c: tickAnimation`, instruction 0x4000): 3 and 4 are
  the banks of the partner's species (`sound.c: loadPartnerSounds`: the `VLALL` entry of its type, or 15
  outside 1–65, and the `VBALL` one), which follow the current species (`attachPartner`; the original reloads them on
  starting, on evolving, `evl.c`, and on rebirth, `dooa.c: loadVLALL`), and 5–7 are the `VBALL` files of the enemies of the last
  battle (`btl.c: loadBattleData`: enemy i in slot 4 + i, `useEnemyBanks`), which stay there after it; routine
  51 puts the final boss's in 5 (`readVBALLSection(5, 0x73)`, `useSpeciesBank` by species number). The
  type → entry table comes, as decided in Q3, from `used_by.types` of each `bank_meta.yaml` (and `used_by.fallback`),
  read the first time one of those slots is requested.
- `SoundPort` also has `playHeldSound(slot, value)` → handle and `stopHeldSound(handle)` (`sound.c: playSound2` and
  `thunkStopSoundMask`); `SessionSound` keeps the voices of each handle. By default the port plays it like `playSound` and
  stops nothing.
- **Battle:** hits and techniques sound from their effect scripts (`battle_effect.c: BTL_playEFESound`):
  `session::BattleEffects` loads `effect/<technique>/` the first time it is used, starts it with `AttackStarted` and
  advances it one step per battle frame with `game::battle::EffectRunner` (see [battle](../modules/game/battle.md));
  its `play_sound` calls go to slot 8 and ids 0x12 and 0x20–0x22 as a held sound, which is stopped when its
  sub-effect ends. In the log: `[port:battle] BTL_startEFE ... move=` and `[port:audio] BTL_playEFESound ... move= sound=`.
- **Menus:** `ui::Ui` reports what happened when navigating with the keyboard or gamepad (`takeCues()`: move, confirm,
  cancel; mouse clicks give no cue), without knowing about sound, and like the original: `tickMenuInput`
  (`main_menu.c:4664`) plays sound 2 of slot 0 only if the cursor moves (at the edge it is silent), 3 on
  confirm and 4 on cancel, and opening the menu (`tickMainMenu`, `main_menu.c:3319`) makes no sound. `MenuSounds`
  (`apps/game/src/game_audio.cpp`) reads `ui/ui_sounds.yaml` and loads its banks at startup, and plays the sound of each
  cue. A cue that is missing or invalid, or whose bank does not load, is left silent with a warning in the log at startup
  (Q12). The original's rejection buzz (sound 11, `script_textbox.c:260`, `script_common.c:898`: dialog and shop
  choices) arrives with those menus; the original's battle makes no sound when rejecting a command, so ours
  does not either.
- **Story:** `StoryLuaHost::publishApi` receives the `SoundPort` and publishes `story.play_sound(slot,
  sound)`, command 0x5A of the listing: like `scriptInstruction5Ato5F` (`script_interp.c:1550`), it passes the two
  operands to the slots (`playSound`, `sound.c:641`, which `SessionSound` already follows) and continues without waiting; a
  sound that does not play does not break the script. It is a new function of the Lua API, so `LuaApiVersion` went up from 1.2.0 to **1.3.0**, together with `set_bgm` and `stop_bgm` (§6.5).

### 6.5 Map music in detail

The music that the map script sets. Battle, the day and night change with the time (`updateBGM`, from `tickGameClock`)
and modes 2–10 are in §6.6; the jukebox goes with the music shop.

| Original function | Where it lives now |
|---|---|
| `scriptInstruction5Ato5F` (`script_interp.c:1569`, 0x5D and 0x5E) | `packages/session/src/story_music_api.cpp`: `story.set_bgm(source)`, `story.stop_bgm(operand)` |
| `playBGM` (`script_interp.c:2587`), `resetBGM` (`:2377`), `handleMusicOverride` (`:1584`, modes 0 and 1) | `SessionMusic` (`packages/session`), behind the `MusicPort` port of `packages/game/story` |
| `playMusic` (`sound.c:753`), `stopBGM` (`:779`), `loadMusicFont` (`:738`), `seqOpen` (`:477`) | `SessionMusic::playMusic`, `stopBgm` and `load` |
| `seqPlay` (`sound.c:488`) and the sound library's sequencer | `audio::Audio::playMusic` → `MusicPlayer` (audio thread) with TinySoundFont |
| The SMF and `font_meta.yaml` | `assets::readSong`, `assets::readMusicFont` (`packages/engine/assets/src/music.cpp`) |

- **Like the original:** `set_bgm` restarts the song even if it is the same one (`playBGM` calls `stopBGM` before
  `playMusic`); `0xFF` stops the music; a track that the source does not have plays track 0; the source stays loaded between
  songs; sequence volume 0x50 out of 0x7F; `plays: once` stops at the end and lets the tails ring out; the rest goes back
  from the `loopEnd` marker (or the end) to the `loopStart` (or the beginning). Cuts are abrupt. Each map change
  cuts the music (`set_script` calls `resetBGM`) and the map header sets it again from the start; a
  header that sets no song leaves the map silent.
- **Improvement (option `continuous_music`, on by default):** when changing maps, the song that is playing continues if the
  new map's header asks for the same one (source and track); if it asks for another or none, it changes at once
  (`SessionMusic::enterMap`, `mapHeadDone`).
- **Differences from the original:** the channel volume (CC 7) is applied linearly, like the sound library, not with the
  synthesizer's cubic curve. Modes 2–10 of variable 245: §6.6.
- **Threads:** the SoundFont is loaded on the game thread (`audio::SoundFont`, with the voices and the 16 channels already
  reserved, so that the audio thread does not allocate memory); the music command carries the pointer, and `Audio` keeps the
  source and the song until the mixer accepts a later music command (`musicTaken`).
- `TinySoundFont` comes in as an overlay port (`tooling/vcpkg-ports/tinysoundfont`, commit `853a0a17`, MIT) with
  `VCPKG_OVERLAY_PORTS` in the presets. `SoundFont::load` gives it the bytes with its own `tsf_stream` and `tsf_load`, not
  with `tsf_load_memory`: the latter converts functions of one type to the stream's, and UBSan's `function` check
  flags it on every read (`ci` preset).
- **Who connects it:** `StoryAdapter` publishes `publishStoryMusic` into the story VM (instead of the silent
  markers from before) and, on every tick, passes the commands to `GameSession::music()` and `sound()`; a silent session
  (null port) makes no sound.

### 6.6 Modes, day and night, battle in detail

| Original function | Where it lives now |
|---|---|
| `handleMusicOverride` (`script_interp.c:1584`, modes 2–10) | `SessionMusic::overridden` with `field_overrides` from `data/music.yaml` |
| `updateBGM` (`script_interp.c:2607`), called at the end of `tickGameClock` (`clock.c:171`) | `SessionMusic::updateBgm`, on every `GameSession` tick while the game time runs |
| `START_BATTLE` (`script_interp.c:1913`) and `loadBattleData` (`btl.c:346`) | `SessionMusic::startBattle`, from `BattleScreen::start` (`apps/game`) |
| `stopBGM` of `BTL_handleVictorySequence` and `BTL_deinitializeCombat` (`battle_main.c:887`, `:814`) | `SessionMusic::endBattle`, when the battle ends |
| `playBGM(ACTIVE_BGM_FONT)` after winning (`tamer.c:862`) | `SessionMusic::leaveBattle`, when the end box closes |

- **Where the numbers come from (Q7):** `dwx` reads them from the code of the user's executable
  (`packages/formats/src/exe/music_code.cpp`). The mode pairs come from running the `switch` of `handleMusicOverride`
  with `evaluateConstantSwitch`, which now knows how to read what a function stores through the pointers passed to it
  (`ValueLocation::pointedBy`, the `sb` instruction) and to start after a call with the value in another register. Those of
  battle are immediates of specific instructions of `loadBattleData` (`readCodeConstant`). The addresses are
  in the US version's layout; in the other versions the keys are not written.
- **Contract** (additive): `data/music.yaml` gains `field_overrides` (`mode`, `font`, `track`) and
  `battle` (`font`, `boss`, `boss_track`, `scripted_track`, `default_track`, `first_enemy_type`, `end_enemy_type`);
  the `music` schema describes them (optional) and `ExtractorVersion` was raised for it. A mod can change the song of a
  mode or the battle music by replacing `data/music.yaml`.
- **Like the original:** `updateBGM` only restarts the song if the source or the variant changes (on passing 6:00 and
  21:00, or if a script changes variable 245); it does not act during battle. Battle stops the field music and
  plays the `battle.font` source: `boss_track` if the first enemy is `boss`, `scripted_track` in a scripted
  battle (a story battle with variable 250 at 1, `IS_PREDEFINED_BATTLE` in `loadBattleData`, btl.c; 63 story and boss
  battles, the Kunemon fight among them, measured with `playMusic(0x21, 0)` from `loadBattleData`), `enemy_music_mapping[type − first_enemy_type]` for the types from `first_enemy_type` to
  `end_enemy_type − 1` and `default_track` for the rest. When it ends it stops the music; if you win, on returning to the field the
  map's music starts from the beginning; after fleeing or losing it stays stopped until a script sets it.
- **Differences from the original:** the original looks at the type of the first enemy for the boss and at that of the entity that
  started the battle for the rest; here both are the first enemy of the request. A mode outside 0–10 leaves the
  variant unwritten in the original; here song 0 plays. Without `data/music.yaml` (or without its new keys) modes
  2–10 play song 0 of the requested source and battle stays silent, with a warning.
- The victory fanfare is an effect of slot 0: `BattleScreen` plays 0x15 on winning and 0x18 (the victory
  poses) 0x29 battle frames later (`battle_main.c:889/904`), and the end box requests with
  `battle_end_count` 0x16 for each point and each bit counted and with `battle_end_counted` 0x17 when the bits are already
  added up, also when skipping the count (`battle_ui.c:438/448/609`). The poses are not drawn: the box opens
  on winning, as before.

## 7. Fidelity: what the player hears

What follows comes from the decomp (`main/sound.c`, `main/script_interp.c`, `main/btl.c`, `btl/battle_main.c`,
`main/clock.c`, `shop/script_menu.c`). The game runs at 20 frames per second; the original's sequencer, at 240
ticks per second. What is a game rule stays the same in the `original` ruleset; what belongs to the machine does not.

### 7.1 Music

| Behavior of the original | Locator | Here |
|---|---|---|
| A track is a (source, track) pair: sources 1–0x21; an out-of-range source is ignored; a track beyond those of the source plays 0 | `sound.c: playMusic`, `seqOpen` | Same, with a log warning instead of silence |
| Only one sequence sounds at a time | `sound.c: initializeMusic` | Same (it is what the player sees) |
| **All changes are abrupt cuts**: stop and start the new one from the beginning. There are no fades or crossfades anywhere | `sound.c: seqStop`, `script_interp.c: playBGM` | Same. A fade would be an optional improvement, off by default (question Q8) |
| Changing source reads the CD and freezes the game for a moment | `sound.c: loadMusicFont` | It is **not** reproduced (it belongs to the machine): the source loads without stopping the game |
| Requesting the track that is already playing again restarts it, because every game path stops the music first; only the day and night change (`updateBGM`) checks whether the variant changes | `script_interp.c: playBGM`, `forceUpdateBGM`, `updateBGM` | Same |
| The field music is set by the map script (`set_bgm` with the source: the operand that the listing calls `track` is the source, 1–28 on the disc); the variant comes from variable 245: mode 0 day (6–20 h) or night, mode 1 always 0, modes 2–10 fixed pairs | `script_interp.c: handleMusicOverride` | Same, in the game; the fixed pairs are executable data (question Q7) |
| On entering a map the music is cut and the map header (MAPHEAD) sets its song from the beginning, even if it is the same one that was playing; a map whose header sets no song (tunn10, tunn07_2/_3, tunn08_2/_3) is left silent. There are no fades: cuts are abrupt | `map.c: changeMap` → `runMapHeadScript`; its `set_script` (`script_interp.c: scriptInstructionFBtoFF`) calls `resetBGM`, and `playBGM` calls `stopBGM` (leaves `CURRENT_SEQ_TRACK` at -1) before `playMusic`, so the "same source and track" check of `playMusic` never avoids the restart | Same: `StoryAdapter::runMapHead` surrounds the header with `MusicPort::enterMap` and `mapHeadDone`; `SessionMusic::enterMap` does the `resetBGM`, so a header with no song leaves the map silent (before, that of the previous map continued). Measured in the original (2026-11-03, `14_native_forest_after_battle`): mayo00 → trai00 (source 1 for both) gives `resetBGM` from `scriptInstructionFBtoFF`, `playBGM` → `stopBGM` → `playMusic` → `seqPlay` once and no `loadMusicFont` (the song starts again with the source already loaded); mayo00 → twnb01 gives the same with `loadMusicFont` (source 28). Tests: `every map plays its song from the start, as the original`, `a map whose head picks no song is silent` (`session_music_tests.cpp`) |
| Which source each header sets: 1 the forest at the start (mayo00–mayo11, trai00), 28 the city (twna*, twnb*, room10–12), and so on for the rest (`set_bgm` of MAPHEAD). Source 1 is ambient sound, not melody: its track 0 (day) has 53 loose notes in 44278 ticks (about 46 s in a loop) and 1 (night) as many more; the jukebox calls it "Non-bewildering forest theme" | `shop/script_menu.c: MAIN_D_801303B8` (source 1 tracks 0 and 1 = the first two songs), `sound.c: loadMusicFont` (entry `source − 1` of `FAALL`); measured: in mayo00 `ACTIVE_BGM_FONT` = 1 and `CURRENT_SEQ_FONT` = 1 | Same: dwx extracts `music/001` from entry 0 of `FAALL` (the 75 events of track 0 are those 53 notes plus the controls); rendered with its SF2 it sounds (RMS 0.11 against 0.20 for source 28). That it "does not sound" when walking through the forest comes from the restart on every map of an ambience with few notes (previous row); the `continuous_music` option avoids it (next row) |
| Continuous music: if the new map uses the song that is already playing (same source and same day or night track), it continues without restarting; if it uses another, it cuts and sets the new one from the beginning, which then continues through all the maps that share it | The original does not do it (previous row) | Improvement (option `continuous_music`, pause menu and `settings.yaml`, on by default): `SessionMusic::setContinuousMusic`; in `enterMap` the song continues while the header runs and its `set_bgm` lets it continue if it is the same. A `set_bgm` from a script outside the map change restarts it, like the original. Tests: `continuous music lets the song of the maps that share it go on and changes it once`, `continuous music changes the song when the hour picks the night one`, `a script's set_bgm outside a map change plays the song from its start` |
| On passing 6:00 and 21:00 the music cuts to the other variant from the beginning | `clock.c: tickGameClock` → `updateBGM` | Same |
| Changing map does not touch the music; the script does; `set_script` (0xFB) stops it | `map.c: changeMap`, `script_interp.c: scriptInstructionFBtoFF` | Same |
| Battle: `start_battle` stops the music; battle plays source 0x21: track 3 against the final boss, 0 in scripted battles, `enemy_music_mapping` for types 0x43–0x6F and 1 for the rest | `script_interp.c` (`START_BATTLE`), `btl.c: loadBattleData` | Same (§6.6), with the numbers from `data/music.yaml` |
| Victory: stops music and effects and the fanfare sounds, which is an **effect** (SS 0x15); on returning to the field the map's music starts **from the beginning** (it is not resumed) | `battle_main.c: BTL_handleVictorySequence`, `tamer.c: startBattle` | Same |
| A sequence is never paused or resumed | (there is no `SsSeqPause`) | Same: the field music is not resumed after a battle |
| Loops: the sequence's NRPN markers (in the MIDI, `loopStart`/`loopEnd`); the tournament tracks of source 0x1D (except 1) and of 0x1E–0x20 (except 2) sound once; the rest, endlessly | `sound.c: seqPlay`; the sound library for the markers | Same: `plays` from `font_meta.yaml` and the markers |
| Sequence volume fixed at 0x50/0x7F (≈ 63 %); the master, at maximum | `sound.c: seqPlay`, `initializeMusic` | Same, as gain of the music bus **before** the player's volume: with the sliders at maximum it sounds like the original |
| The jukebox plays (source, track) on confirm, without changing the active music, and on leaving, the map's music returns from the beginning | `script_menu.c: tickJukeboxMenu`, `openJukebox` | Same: `session::Jukebox` with `SessionMusic::playSong` and `replayActive`; the source and track of each song, in `bgm_tracks` of `data/music.yaml` |
| The title screen (the "Press Start" screen and the main menu) has no music: the game ends with `cleanupGame` and `finalizeMusic`, which stop its song before the ending movie and before returning to the title screen | `main.c: main`, `runLandingScreen`, `runMainMenu` | Same: the game's song stops when the game is torn down (`SessionMusic` stops it when destroyed), so "Return to title", the ending and a failed load leave the title screen silent; loading a game from the title screen sets the song of its map from the beginning. Test: `the game's song stops when the game ends, so the title screen is silent` |

### 7.2 Effects

| Behavior of the original | Locator | Here |
|---|---|---|
| An effect is a note of a program of its slot's bank; all the tones of the program whose `max` is the note sound, each on its own voice | `sound.c: playSound`, `startSound` | Same (§6.2) |
| Always at maximum volume and centered: no volume or pan per call and **no 3D position** (distance does not attenuate) | `sound.c: startSound` | Same: the volume and pan are those of the tone and the program. Positional sound would be an optional improvement (not done) |
| Requesting an effect that is sounding again does **not** restart it: it sounds again on top | `sound.c: playSound` | Same |
| Stopping an effect: quick volume drop and the note is released | `sound.c: stopSoundMask` | Same (§5) |
| Some battle and minigame effects sound in a loop until their owner stops them | `battle_effect.c: BTL_playEFESound` | The API allows it (looping voice + stop by id); each system uses it in its package. The bug of `BTL_stopEFESounds` (it only stops the first slot) is not copied: `EffectRunner::stopAll` stops them all |
| Sound script commands do not wait: `play_sound` fires and continues; `set_bgm(0xFF)` and `stop_bgm` stop the music | `script_interp.c: scriptInstruction5Ato5F` | Same. The map bank load waits (routines 0x28, 0x35) end at once: they belong to the story runner |
| Digimon cries are in the animations (animation instruction with bank and sound; only if the entity is on screen) | `anim.c` | With their own system (question Q6) |

### 7.3 What belongs to the machine and is not reproduced

- **14 voices for effects and 24 in total, shared with the music**, stealing in turn: in the original an effect
  can cut another or a music note in very busy scenes. Here the music and the effects do not compete and the
  limit is the guard in §5 (question Q9).
- **The freeze when loading a source** from the CD.
- **The chip's interpolation** (4-point Gaussian): the one here is linear. With the Opus files cooked at 48 kHz it is hardly
  noticeable; with the development path's WAV files at 2–11 kHz, a little.

### 7.4 Reverb

The original turns on the chip's reverb at startup and does not change it: `STUDIO_B` mode, depth 0x7000
(≈ 88 %), on voices 0–22 (`sound.c: initializeMusic`), and each tone decides whether to use it (bit 2 of `mode`, which the SF2 already
carries as `reverbEffectsSend`). It is part of how the game sounds.

**Implementation.** `ChipReverb` (`packages/engine/audio/src/chip_reverb.*`) follows the public description of the
registers and the chip's formula (not the decomp): Schroeder's stereo reverberator at 22,050 Hz over a ring of
16-bit samples the size of the preset's work area (0x4840 bytes), with the same-side and opposite-side
reflections, the four comb filters, the two all-pass filters and the output depth 0x7000, and the 32 registers of the
"Studio Medium" preset (the library's `STUDIO_B`). What it writes into the ring is saturated and rounded to 16 bits,
like the chip, so the tail ends in silence or in a cycle of the last bits, never in a floating-point tail that
does not end.

- **Per-voice send.** `PlayRequest::reverbSend` (0 to 1): `EffectBank` sets 1 on the tones with bit 2 of `mode`
  (`bank_meta.yaml`, which is now read as `SoundBankTone::mode`). In the music, the repository's TinySoundFont port
  keeps the `reverbEffectsSend` generator of each zone (the synthesizer used to read it and throw it away) and the
  `MusicPlayer` mixes each voice separately (`renderSynth`) to send those that have it to the send.
- **Where it is added.** The send is taken after the bus gain (lowering the music also lowers its echo) and the reverb's
  output is added to the mix before clipping, like the chip's.
- **What is approximate.** The step from the mixer's 48 kHz to the chip's 22,050 Hz and back: a two-pole
  low-pass at 10 kHz and linear steps, instead of the chip's 39-coefficient filter. Voice 23 of the chip is left out
  of the reverb in the original; here voices are not chip channels (§7.3) and that is not reproduced.
- **Option.** "Reverb" in the sound tab (`reverb` in `settings.yaml`, `Audio::setReverb`), on
  by default because that is how the original sounds; off, everything sounds dry and the tail is cut at once. It changes the mix
  enough for someone to prefer it without.
- **Tests:** `reverb_tests.cpp` (silence, first echo at 884 ticks of the preset on both sides, the tail that
  dies out to silence, clearing, only the voices with a send leave a tail, on at start),
  `effect_bank_tests.cpp` and `sound_bank_tests.cpp` (the `mode` bit), `volume_setting_tests.cpp` (the options
  row applies and saves).
- **Measured against the original:** there is no way to record DuckStation's audio from the MCP, and the chip's registers
  (1F801DC0h…) cannot be read with it (only main RAM); the original's configuration comes from
  `initializeMusic` in the decomp, and the preset values, from the public documentation.

## 8. What a mod sees

| Surface | What a mod sees | Contract |
|---|---|---|
| Sound and music assets (`sound/**`, `music/**`, their `*_meta.yaml`, `data/music.yaml`) | Written by `dwx`; schemas `sound_bank` and `music`. A mod that replaces a `sample_<n>`, a `font.sf2` or a `track_<nn>.mid` by id is heard | None |
| `field_overrides` and `battle` of `data/music.yaml` | Written by `dwx` (§6.6); a mod changes the song of a mode or that of battle | New keys of the `music` schema (additive) |
| Cooked format (Opus, SF2 and MIDI copied) | ADR 0017 | None |
| `story.play_sound(slot, sound)` | Published on the `SoundPort` | Additive; `LuaApiVersion` 1.3.0 |
| `story.set_bgm(source)`, `story.stop_bgm(operand)` | Published on the `MusicPort` (§6.5) | Additive; same 1.3.0 |
| Which sound each menu cue plays | A document of the `game` pack (`ui/ui_sounds.yaml`: `cursor`, `confirm`, `cancel` → `{bank, sound}`) with its schema | Additive |
| Player volumes | User settings, not the pack's; a mod does not see them | None |
| A general sound API for mods (`sound.play("sound/...")` by id) | Not published; goes to phase 5 | — |

Internal (no contract): all of `packages/engine/audio` (its classes, the queue, the outputs, the limits), the `SoundPort` port of
`packages/game/story` and the session's slot resolution.

## 9. Tests

| Area | What is tested (without a device, with the null output) |
|---|---|
| 1 | Mixing: a voice from a known clip comes out with its gain and its pan; two voices add up; the clip to ±1. Volumes: master × bus × voice, the limit to [0, 1], the ramp without jumps. Voices: they end and notify, the loop returns, stopping lowers to 0. Voice limit: voice 257 replaces the oldest, and the warning. Queue: the order within a tick, the full queue. Null output: game time advances (60 ticks = 48,000 frames) |
| 2 | WAV reader (with and without `smpl`); from `bank + sound` to voices with synthetic fixtures; `story.play_sound` with a port double; and, with `DWE_CONTENT`, that each sound of each `bank_meta.yaml` resolves to samples that can be read |
| 3 | Sequencer: tempo, loops, `once`; the synthesizer with a minimal synthetic SF2; the transitions of §7 with the null output; with `DWE_CONTENT`, that the 74 tracks are read and sound without error |
| 4 | Volume settings saved and read; an autotest route (en and es) that changes the volumes from the options |

## 10. Decisions

Decided on 2026-10-22, the recommended option each time unless noted; the discarded options are in the git history.

- **Q1, SF2 synthesizer:** TinySoundFont (MIT) as a vcpkg overlay port in the repository (`tooling/vcpkg-ports/tinysoundfont`),
  pinned to a commit with its SHA‑512 and its license installed. FluidSynth (LGPL‑2.1) is out because of the license.
- **Q2, effects:** each tone is a voice with its WAV/Opus sample, volume, pan, pitch and `adsr` envelope, not a note of
  `bank.sf2` through the synthesizer.
- **Q3 and Q13, type → entry of `VLALL`/`VBALL`:** read from `used_by.types` and `used_by.fallback` of each
  `bank_meta.yaml`, with no contract change (§6.4).
- **Q4, pan law:** constant power, the same one TinySoundFont uses.
- **Q5, volume curve:** steps from 0 to 10 with the gain as the square of the fraction (5 → −12 dB, 1 → −40 dB, 0 →
  silence); only music and effects, the master stays at 1; default 10 (the original).
- **Q6, which effects:** the scripts' `play_sound` commands and the system and menu sounds; the cries tied to the animations
  and the battle effects go with each system's package, on the same API.
- **Q7, field and battle music data:** `dwx` writes them to `data/music.yaml` (`field_overrides`, `battle`), an additive
  contract change; not as rules of the `original` ruleset.
- **Q8, fades:** abrupt cuts like the original; a fade as an optional improvement is left for phase 5.
- **Q9, the original's 14 effect voices:** not reproduced; a guard of 256 voices (§5).
- **Q10, reverb:** the chip's own (`STUDIO_B`), as a per-voice send (§7.4).
- **Q11, audio in the autotest routes:** a warning and the null output (§4), which is deterministic.
- **Q12, which sound each menu cue plays:** `ui/ui_sounds.yaml` in the `game` pack, an additive contract change; a missing
  or invalid entry leaves that cue silent with a warning. There is no `error` cue: the original's battle makes no sound
  when rejecting a command.
- **Q14, `story.play_sound`:** published on the `story` API on top of the `SoundPort`, with the minor bump of `LuaApiVersion`.
