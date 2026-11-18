# video

The game's movies: reads the video that `dwx extract` leaves in `movie/<name>/` frame by frame and gives the picture that is due
at each moment. It does not draw or play any sound: whoever uses it does that (`MovieScreen` in `apps/game`). It is a module of the
systems layer and only depends on `core` and `assets`. The complete design, with the flow of movies in the game,
is in [movies](../../architecture/movies.md); the on-disk format, in [video](../../formats/video.md).

## Pieces

| Piece | What it does |
|---|---|
| `y4m_reader.hpp` | `Y4mReader`: reads an 8-bit (`C420jpeg`) or 10-bit (`C420p10`) YUV4MPEG2 video (`.y4m`) from an `assets::FileReader`, frame by frame (`read`) or skipping them (`skip`); `Y4mStream` is what its header says |
| `av1_reader.hpp` | `Av1Reader`: the same for the cooked video, 8- or 10-bit AV1 in an IVF (`.ivf`), decoded with libaom; `skip` also decodes, because the following frames are built on top of it |
| `picture.hpp` | `YuvPicture` (a 4:2:0 frame in 16-bit planes, full range, with its `bitDepth` of 8 or 10) and `toRgba`, which converts it to 8-bit RGBA |
| `movie_player.hpp` | `readMovieInfo` (reads `movie.yaml`), `MovieInfo` and `MoviePlayer`: opens a movie, advances with the clock and gives the picture of the moment |

## A movie

Each movie is a folder of the base pack with three files: `video.y4m`, `audio.wav` and `movie.yaml` as
`dwx extract` writes them, or `video.ivf` (AV1) and `audio.opus` in a cooked pack (`dwx cook`). `MoviePlayer` chooses the
reader by the extension of the file that the virtual file system resolves. The YAML says
the size, the frames per second (`video.fps.numerator` and `denominator`), how many frames the file has
(`video.frames`), the number of the first one (`video.first_frame`) and, if the original cuts it earlier, at which frame it
stops (`playback.frame_count`). `MovieInfo::shownFrames` are the frames that are seen: up to that cut, or all of them if the table does not
cut it or asks for more than there are.

## Color

`toRgba` is an engine improvement over the original's decoder, which repeats each chroma sample over the 4
pixels of its block and converts with truncated integers:

- **Interpolated chroma.** Each pixel blends the 4 nearest chroma samples with weights 9, 3, 3 and 1 (out of 16): the
  bilinear for samples at the center of each 2 × 2 block, as `dwx` writes them. No 2-pixel steps on
  color edges.
- **Exact BT.601, full range.** `r = y + 1.402 · cr`, `g = y − 0.344136 · cb − 0.714136 · cr`,
  `b = y + 1.772 · cb`, in floating point, on values converted to 0..255 from their depth (`/ 1023 · 255` with
  10 bits), and each channel rounded and saturated to a byte. Green no longer loses the low bits of its products.

## Usage

```cpp
auto player = video::MoviePlayer::open(vfs, name);       // reads movie.yaml and opens video.y4m or video.ivf
auto sound = video::MoviePlayer::loadSound(vfs, name);   // the whole WAV or Opus, or nothing if it has none
// Every game frame: the time comes from the sound's position (or from the clock, without sound).
if (auto changed = player->advanceTo(seconds); changed && *changed) {
  uploadTexture(player->picture());                      // width × height × 4 bytes, rows from the top
}
if (player->finishedAt(seconds)) { /* the flow continues */ }
```

- **Only one frame in memory.** A movie's video weighs hundreds of MB: `Y4mReader` reads from the file in chunks
  (`VirtualFileSystem::open`, also inside a `.pak`) and never has it whole.
- **The sound leads.** `advanceTo` skips the frames that arrive late, like the original, which never waits for the video.
  Going back is not possible: an earlier time does not change the picture.
- **End.** `finishedAt` is true when the last frame that is seen has already lasted its time.
- **Errors.** A header that is not progressive 4:2:0, a truncated frame or a `movie.yaml` missing a field give a
  `core::Error` with the file name. A movie that the pack does not bring is decided by the caller: `MovieScreen`
  warns in the log and goes on.

## Tests

The module has no tests of its own: they are covered by the `apps/game` routes that see movies (`movies: true` in a route,
[route format](../../guides/vertical-slice.md#route-format)) and `openAsset`, which checks the Y4M header
of each movie when validating a pack ([assets](assets.md)).
