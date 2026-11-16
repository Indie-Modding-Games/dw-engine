# Audio

How `formats/audio` reads the original's sound files (`SOUND/VHB/*.VHB`) and how it writes them in open formats:
SoundFont 2 per bank, WAV per effect sample and MIDI per sequence. The field names are those of the sound library the
decomp uses (`VabHdr`, `ProgAtr`, `VagAtr`); reserved fields are called `reservedN`.

## Files

The original loads seven files into nine VAB slots (`sound.c`, `VHB_HEADER_ADDR`). The table is in
`formats/audio/vhb.hpp` (`soundFiles`):

| File | Layout | Slots | Loaded by |
|---|---|---|---|
| `SS.VHB` | one bank | 0 | `sound.c: initializeMusic`; system and menu sounds |
| `SL.VHB` | one bank | 1 | `sound.c: initializeMusic`; no literal call touches it |
| `SB.VHB` | one bank | 8 | `sound.c: VSLoadSounds`: replaces the map sounds in VS mode and the arenas |
| `FAALL.VHB` | 33 entries of 0x27 sectors | 2 | `sound.c: loadMusicFont`: font `n` is entry `n − 1` |
| `VLALL.VHB` | 16 entries of 15 sectors | 3 | `sound.c: loadPartnerSounds`, by `DIGIMON_VLALL_SOUND_ID[type]` |
| `VBALL.VHB` | 71 entries of 7 sectors | 4–7 | `sound.c: loadDigimonSounds`, by `DIGIMON_VBALL_SOUND_ID[type]` |
| `ESALL.VHB` | 22 chunks of `MAP_SOUND_PARA` | 8 | `sound.c: loadMapSounds`, by the map's sound id |

- Fixed-stride entries are read with `readVHBFileSectors(file, entry × sectors, sectors)`.
- `MAP_SOUND_PARA[id]` is `{u16 sectorId; u16 sectorCount}` in units of 1 KB: `loadMapSounds` divides both by
  2 to get sectors. The id is `MapEntry.flags & 0x1F` (`map.c: getMapSoundId`). Besides each map's own, there are
  loads with a fixed id: 0x10 (`std/std_setup.c: STD_tournamentMain`), 0x12 (`main/evl.c: getEvoSequenceState`), 0x13
  (`main/partner.c: partnerTickDying`, `dooa/dooa.c: DOOA_tickDissolve`), 0x14 and 0x15
  (`main/script_interp.c: scriptInstruction64to7E`, operations 0x34 and 0x33).
- `loadPartnerSounds` uses entry 15 for any type outside 1–65.

### Executable tables

`formats/exe` reads them with the others ([Tables](tables.md)); the extractor uses them to say who uses each bank.

| Table | Entries (US) | Content |
|---|---|---|
| `DIGIMON_VLALL_SOUND_ID` | 66 `u8` | `VLALL` entry per partner type |
| `DIGIMON_VBALL_SOUND_ID` | 180 `u8` | `VBALL` entry per type |
| `MAP_SOUND_PARA` | 22 × 4 | `MapSoundPara {u16 sectorId; u16 sectorCount}` per map sound id |
| `MAIN_D_801303B8` | 64 × 2 `u8` | font and track of each entry of the shop's music list (`shop/script_menu.c`) |

## VHB

A VHB is a `u32` header, the VH (VAB attributes) and the VB (ADPCM samples), one after another:

| Header | Fields | Where |
|---|---|---|
| Sound bank | `{u32 vh = 12; u32 vb; u32 vbEnd}` | `sound.c: readVHBFile` |
| Music font (`FAALL`) | `{u32 vh = 16; u32 vb; u32 seq; u32 seqEnd}` | `sound.c: loadMusicFont` |

`readVhb` checks that `vb − vh` is the size of the VH given by its counters, that the VB fits in the file, that the
third `u32` of a bank is the end of the VB and that a font's SEQ bank starts after the VB and fits.

## VH

| Part | Size | Content |
|---|---|---|
| `VabHdr` | 0x20 | `"pBAV"`, version 7, id, `fsize`, `ps` (programs), `ts` (tones), `vs` (samples), master volume and pan, `attr1`, `attr2` |
| `ProgAtr[128]` | 128 × 16 | per slot: number of tones, volume, priority, mode, pan, attributes |
| `VagAtr[ps][16]` | `ps` × 512 | a block of 16 tones per program that has tones |
| sizes | 256 × `u16` | size of each sample divided by 8; entry 0 is not used |

- Tone block `k` belongs to the `k`-th **non-empty** program: in two `FAALL` fonts the programs are not
  consecutive, and each tone's `tone.prog` confirms which program it belongs to.
- A tone has `min`/`max` (key range), `center` and `shift` (root key and fine tuning in 1/128 of a semitone),
  `vol`, `pan`, `mode` (bit 2: reverb), `pbmin`/`pbmax` (pitch bend range), vibrato, portamento, `adsr1`, `adsr2`
  and `vag` (the sample).
- Samples follow one another in the VB, in the order of the size table; those of size 0 take no space.
- `readVab` requires: signature, `vs` < 256, VH size inside the buffer, `fsize` ≥ VH, as many non-empty programs as
  `ps`, at most 16 tones per program, `tone.prog` equal to its program and `vag` in 1..`vs` with a non-zero size.
- Some `VBALL` entries have one sample beyond `vs` in the size table; `locateVags` includes it if it fits in the
  VB and marks it (`beyondCount`).

## ADPCM

Each sample is a list of 16-byte blocks with 28 samples: byte 0 = `filter << 4 | shift`, byte 1 = flags, and
14 bytes of nibbles (low one first). Each decoded sample is:

```text
s = (int16(nibble << 12) >> shift) + ((s1 · f0 + s2 · f1 + 32) >> 6)
```

with `(f0, f1)` = (0, 0), (60, 0), (115, −52), (98, −55), (122, −60), clamped to 16 bits. A `shift` greater than 12
behaves like 9 and a filter greater than 4 like 4, as on the hardware; `decodeAdpcm` counts them as reserved.

| Flag | Value | Effect |
|---|---|---|
| end | 1 | last block |
| repeat | 2 | with end: jumps back to the loop start |
| loop start | 4 | the loop starts at this block |

- A sample without a loop ends in a block with flags 1 followed by the closing block that the sound library's tools
  leave: `00 07` and 14 bytes of `77` (a loop onto itself). The channel is released before reaching it, so it is not
  part of the sample.
- One with a loop goes from a block 4 or 6 to a block 3. Without a block 4, the loop starts at the first block
  (`loopStartImplied`).
- Decoding stops after the first block with an end flag; whatever is left behind it is reported.

## SEQ

A font's SEQ bank is a table of `u32` (how many = `offsets[0] / 4`) with the offset of each SEQ from the start of
the bank (`sound.c: playMusic`). A SEQ is:

| Part | Format |
|---|---|
| header (15 bytes, big-endian) | `"pQES"`, version `u32`, resolution `u16` (ticks per quarter note), tempo `u24` (µs per quarter note), time signature `u8 u8` |
| events | delta as a VLQ (up to 4 bytes) and a MIDI channel event, with running status |
| tempo | `FF 51 tt tt tt`: **without** a length byte |
| end | `FF 2F 00` |

- A meta event clears the running status.
- Each SEQ in the bank starts aligned to 4 bytes; the padding after `FF 2F 00` is `CD` bytes (the value a debug heap
  fills uninitialized memory with) or zeros.
- The loop is in NRPN: `CC99 = 20` followed by `CC6 = n` marks the start of a loop of `n` repeats,
  and `CC99 = 30` the end. `readSeq` turns them into `LoopStart` (with the count) and `LoopEnd` events.
- Any other meta event, a sysex or a delta that overflows 32 bits is an error.
- `sound.c: seqPlay` plays the tracks of font 0x1D once except track 1 and those of fonts 0x1E–0x20 except track 2;
  the others, in an infinite loop. It sets the volume to 0x50.

## Writers

The writers use no new dependencies and give the same bytes for the same input.

### WAV

16-bit mono PCM. A sample with a loop carries a `smpl` chunk with a forward loop (the end is inclusive:
`loop.end − 1`) and the unity note.

### SoundFont

One preset per program (bank 0, preset = program number) with an instrument of one zone per tone:

| SF2 | Comes from |
|---|---|
| `keyRange` | `min`–`max` |
| `overridingRootKey` | `center` |
| `fineTune` | `−round(shift · 100 / 128)`: libsnd computes the pitch as `(note · 128) − (center · 128 + shift)` |
| `initialAttenuation` | `−200 · log10(vol/127 · program mvol/127 · bank mvol/127)`, from 0 to 1440 cB |
| `pan` | `(tone pan − 64) + (program pan − 64)`, limited to ±64 and mapped to ±500 |
| `reverbEffectsSend` | 1000 (100 %) if bit 2 of `mode` is set |
| `sampleModes` | 1 if the sample has a loop |
| volume envelope | see below |

Samples are written at 44 100 Hz (the chip's rate at pitch 0x1000) with 8 guard points for the loop and 46
zeros after, as the format requires.

**Envelope (approximation).** The SPU raises and lowers the level in steps at a rate that depends on the level; SF2
only has times and a sustain level. `approximateEnvelope` simulates the SPU at 44 100 Hz with `adsr1`/`adsr2`:

- attack: the time from 0 to 0x7FFF;
- exponential decay and release: the time they take to drop 60 dB, scaled to SF2's 100 dB; linear
  release, the time down to 0;
- sustain: `200 · log10(0x7FFF / SL)` cB with `SL = (n + 1) · 0x800`;
- a sloped sustain (rate other than 0x7F) that goes down becomes the decay, with a sustain of
  1000 cB; one that goes up is ignored. Both are counted in the YAML (`sloped_sustain_tones`);
- times are limited to [−12000, 8000] timecents.

Vibrato, portamento, priorities and a downward pitch bend different from the upward one do not go into the SF2; the
bank's YAML stores them and says which have values (`not_in_soundfont`).

### MIDI

SMF format 0 with the SEQ's resolution as the division and its tempo at tick 0. Ticks and tempos are
exact. After each program change the pitch bend range (RPN 0) is written with the `pbmax` of the program's first
tone, followed by the null RPN. Notes with velocity 0 are left as they are. The loop goes as `loopStart` and
`loopEnd` markers (meta 06) at their ticks; the repeat count goes to the YAML. No time signature is written: it is
not known what the rhythm field means, so it is left in the YAML as it comes.

## Extraction

The `sound_bank` extractor (`apps/dwx/src/audio_extractors.cpp`) takes the seven files. For each bank:

| Bank | Output |
|---|---|
| `SS`, `SL`, `SB` | `sound/<bank>/bank.sf2`, `sample_<n>.wav` and `bank_meta.yaml` |
| `VBALL`, `VLALL` or `ESALL` entry | the same in `sound/<bank>/<entry with 3 digits>/` |
| `FAALL` font | `music/<font with 3 digits>/font.sf2`, `track_<n>.mid` and `font_meta.yaml` |

- Each WAV is written at the rate at which its sample sounds on the highest key of the first tone that uses it:
  `44100 · 2^(((max · 128) − (center · 128 + shift)) / 1536)`. With `playSound`, the effect banks play note
  60 + `v % 16` of program `v / 16` (slots 0, 1 and 8) or note `v` of program 0 (slots 3–7), and
  `startSound` plays all tones whose `max` is that note: the YAML lists these values in `sounds`.
- The YAML stores all the VH fields, the tones of each program (`adsr` in hexadecimal), the samples (points,
  loop, rate) and who uses the bank according to the executable tables: Digimon types (`VBALL`, `VLALL`), maps
  and fixed loads (`ESALL`), and, for each music track, the entries of the shop's music list
  (`MAIN_D_801303B8`, with the same index as `BGM_TRACK_NAMES`) and the enemies of `ENEMY_MUSIC_MAPPING`.
- It is an unknown (the file is left partial): a bank that cannot be read, non-zero bytes after an entry's data
  or after the VB's samples, a sample without an end block or with reserved values, bytes after the end block that
  are not the closing block, a SEQ that cannot be read or with something after its end that is not alignment
  padding, and a change to an empty program.
- Without the executable tables the banks come out the same but without users (a note), and `ESALL` cannot be split.
