# audio

The game's sound: the output to the device, the mixer, the voices and the volumes. It is a module of the systems layer
that depends on `core`, `platform` (SDL3) and `assets`. It does not know `packages/game/` or `apps/`: it does not know what a map
or a script is, it only plays clips on buses. The complete design, the stretches and the fidelity to the original are in
[Audio](../../architecture/audio.md).

## Public API

| Header | What it provides |
|---|---|
| `dwe/audio/audio.hpp` | `Audio`, the game thread's facade: `play` and `playAll` (all or nothing), `stop`, the music (`playMusic`, `stopMusic`), the volumes and `update` |
| `dwe/audio/clip.hpp` | `Clip` (16-bit PCM, frequency, channels and loop) and `checkClip` |
| `dwe/audio/voice.hpp` | `VoiceId` (slot and generation), `PlayRequest` (clip, bus, gain, pan and pitch) and `ChipEnvelope` |
| `dwe/audio/mixing.hpp` | `Bus`, `VolumeControl`, the mix format and the limits (`MaxVoices`, `CommandQueueCapacity`) |
| `dwe/audio/output.hpp` | `MixSource`, the `Output` interface, `NullOutput` and `openDeviceOutput` (SDL) |
| `dwe/audio/effect_bank.hpp` | `EffectBank`: a bank of effects with their samples, each effect one voice per pitch |
| `dwe/audio/sound_font.hpp` | `SoundFont`: an SF2 loaded into the music synthesizer (`SoundFont::load`), which `playMusic` plays on the `Music` bus |

No public header includes SDL: the SDL output is private (`src/sdl_output.cpp`) and opens its own audio
subsystem.

## How it is used

- The game never waits for the audio thread. `Audio` queues commands and the mixer applies them together on the next
  `update`, in the order they were given.
- `apps/game` creates the `Audio` with `openDeviceOutput` or, if there is no device, with `NullOutput` and a warning. `dwe-sim`
  and the tests always use the null output.
- `openDeviceOutput` opens the device in its own thread with a deadline (`DeviceOpenTimeout`, 3 s): a sound
  server that does not answer leaves the game silent instead of hanging it, and `deviceOpenStuck` says whether that thread got
  stuck so the game can finish without waiting for it ([details](../../architecture/audio.md#4-outputs)).
- `packages/game/*` does not see `audio`: `packages/game/story` declares the `SoundPort` port and `packages/session` implements it on `Audio`
  (`session::SessionSound`). Which bank or track plays at each moment is decided by the game, not this module.

## Mods

Nothing in `packages/engine/audio` is a contract with mods. What scripts see arrives through the `story` API when it works and
has a test ([what a mod sees](../../architecture/audio.md#8-what-a-mod-sees)).
