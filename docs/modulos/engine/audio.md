# audio

El sonido del juego: la salida al dispositivo, el mezclador, las voces y los volúmenes. Es un módulo de la capa de
sistemas que depende de `core`, `platform` (SDL3) y `assets`. No conoce `game/` ni `apps/`: no sabe qué es un mapa
ni un script, solo reproduce clips en buses. El diseño completo, los tramos y la fidelidad al original están en
[Audio](../../architecture/audio.md).

## API pública

| Header | Qué da |
|---|---|
| `dwe/audio/audio.hpp` | `Audio`, la fachada del hilo del juego: `play` y `playAll` (todo o nada), `stop`, la música (`playMusic`, `stopMusic`), los volúmenes y `update` |
| `dwe/audio/clip.hpp` | `Clip` (PCM de 16 bits, frecuencia, canales y bucle) y `checkClip` |
| `dwe/audio/voice.hpp` | `VoiceId` (hueco y generación), `PlayRequest` (clip, bus, ganancia, panorama y tono) y `ChipEnvelope` |
| `dwe/audio/mixing.hpp` | `Bus`, `VolumeControl`, el formato de la mezcla y los límites (`MaxVoices`, `CommandQueueCapacity`) |
| `dwe/audio/output.hpp` | `MixSource`, la interfaz `Output`, `NullOutput` y `openDeviceOutput` (SDL) |
| `dwe/audio/effect_bank.hpp` | `EffectBank`: un banco de efectos con sus muestras, cada efecto una voz por tono |
| `dwe/audio/sound_font.hpp` | `SoundFont`: un SF2 cargado en el sintetizador de la música (`SoundFont::load`), que `playMusic` toca en el bus `Music` |

Ningún header público incluye SDL: la salida SDL es privada (`src/sdl_output.cpp`) y abre su propio subsistema de
audio.

## Cómo se usa

- El juego nunca espera al hilo de audio. `Audio` encola órdenes y el mezclador las aplica juntas en el siguiente
  `update`, en el orden en que se dieron.
- `apps/game` crea el `Audio` con `openDeviceOutput` o, si no hay dispositivo, con `NullOutput` y un aviso. `dwe-sim`
  y los tests usan siempre la salida nula.
- `openDeviceOutput` abre el dispositivo en un hilo propio con un plazo (`DeviceOpenTimeout`, 3 s): un servidor de
  sonido que no contesta deja el juego en silencio en vez de colgarlo, y `deviceOpenStuck` dice si ese hilo quedó
  atascado para que el juego termine sin esperarlo ([detalles](../../architecture/audio.md#4-salidas)).
- `game/*` no ve `audio`: `game/story` declara el puerto `SoundPort` y `apps/session` lo implementa sobre `Audio`
  (`session::SessionSound`). Qué banco o qué pista suena en cada momento lo decide el juego, no este módulo.

## Mods

Nada de `engine/audio` es contrato con los mods. Lo que vean los scripts llega por la API `story` cuando funcione y
tenga test ([qué ve un mod](../../architecture/audio.md#8-qué-ve-un-mod)).
