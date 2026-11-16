# Ogg Opus sound from `dwx cook`

`dwx cook` converts each WAV in a package into an [Ogg Opus](https://www.rfc-editor.org/rfc/rfc7845) file with the
same id (`sound/sb/sample_001.wav` → `sound/sb/sample_001.opus`). It is written by `apps/dwx/src/audio_cooker.cpp`
with libopusenc and read by `packages/engine/assets` (`dwe::assets::decodeOpus`) with libopus. The decision is in
[ADR 0017](../adr/0017-cooking-and-pak-files.md).

## What it writes

- One logical Ogg stream with a single Opus stream, of one or two channels (mapping family 0), like the WAV.
- Opus always works at 48 kHz: libopusenc resamples the WAV (in the US extraction they go from 2756 Hz to 44100 Hz)
  and stores the original rate in the `input_sample_rate` field of `OpusHead`, for information only.
- Fixed settings, so the same WAV gives the same bytes: 64 kbps per channel, variable bitrate (VBR), complexity 10,
  no padding in the comments and the Ogg serial number `0x64776531` (`dwe1`; libopusenc would pick a random one).
  The vendor string says the libopus and libopusenc versions, pinned by the vcpkg baseline.
- The comments (`OpusTags`), in frames of the sound decoded at 48 kHz:

| Comment | What it is |
|---|---|
| `LOOPSTART` | First frame of the loop |
| `LOOPLENGTH` | Frames in the loop |
| `DWE_UNITY_NOTE` | The MIDI note the sound plays at its original pitch (from the `smpl` chunk), for the sampler |

`LOOPSTART` and `LOOPLENGTH` are the names many engines use for loops in Ogg. The WAV loop (`smpl`, end included) is
converted to 48 kHz by rounding each end to the nearest frame and without leaving the sound, which once decoded has
`ceil(frames × 48000 / rate)` frames. A WAV without a loop does not carry those two comments.

## What the reader checks

`decodeOpus` reads the Ogg pages with its own reader (`packages/engine/assets/src/ogg.cpp`, without libogg or
opusfile) and rejects the file, with a message that says what fails, if:

- a page does not start with `OggS` version 0, does not match its CRC, is not the next one in the stream (serial and
  sequence number) or is cut off; the first one does not open the stream or the last one does not close it; there is
  something after the end; or a split packet does not continue on the next page;
- the first packet is not an `OpusHead` of version 0.x with one or two channels and family 0, or the second is not a
  well-formed `OpusTags`;
- the final position (granule) is less than the pre-skip, or the sound is longer than an hour (`MaxSoundFrames`), so
  that a forged file cannot ask for unlimited memory;
- a packet does not decode, or the packets give fewer frames than the last page says, or too many more;
- `LOOPSTART`, `LOOPLENGTH` or `DWE_UNITY_NOTE` are not numbers, one of the two loop comments is missing or the loop
  falls outside the sound.

It returns 16-bit PCM at 48 kHz, interleaved by channel, without the encoder's pre-skip or the padding of the last
packet, with the loop, the note and the original rate.

## What remains

- Opus is lossy and works in 20 ms packets: a very short instrument loop may sound with a small jump at the return
  point. The same sounds are lossless inside the `bank.sf2` of each bank, which is copied as is.
- 62 of the 642 WAVs of the US extraction come out larger in Opus (159 KB in total): they are sounds at a very low
  rate (2756 to 5513 Hz) that in PCM take less than 64 kbps. A bitrate that depends on the original rate would avoid
  this.
- libopus picks SIMD routines at run time depending on the CPU. The same binary on the same machine always gives the
  same bytes; on different CPUs the floating-point encoder might not give exactly the same ones.
