# `.pak` file

A `.pak` stores an entire package (its `pack.yaml` and all its files) in a single file, to distribute and load it
without thousands of loose files. It is a format of the engine itself, not of the original. It is written by
`dwx pack` and read by `packages/engine/assets` (`dwe::assets::PakFileSystem`). The decision is in
[ADR 0017](../adr/0017-cooking-and-pak-files.md).

All integers are little-endian. Offsets are counted from the start of the file.

## Structure

```text
header (64 bytes)
data of each file, in id order, each aligned to 16 bytes
table of contents (64 bytes per entry), aligned to 16 bytes
names
manifest (pack.yaml, as is)
```

The index (table, names and manifest) goes at the end so that `dwx pack` writes the file in one pass, reading each
file only once. Whoever opens it reads the header and then the index in one go; the data is read when requested.

### Header

| Offset | Type | Field |
|---|---|---|
| 0x00 | `u8[8]` | Magic `89 50 41 4B 0D 0A 1A 0A` (`\x89PAK\r\n\x1A\n`) |
| 0x08 | `u32` | Format version: 2 (the reader also accepts 1) |
| 0x0C | `u32` | Flags: 0 (none are defined) |
| 0x10 | `u32` | Number of entries |
| 0x14 | `u32` | Alignment of the data and the table: 16 |
| 0x18 | `u64` | Offset of the table of contents |
| 0x20 | `u64` | Offset of the names |
| 0x28 | `u64` | Size of the names |
| 0x30 | `u64` | Offset of the manifest |
| 0x38 | `u64` | Size of the manifest |

The magic follows the idea of PNG: the `0x89` byte and the two line endings make a text-mode transfer break it, and
so it is detected.

### Table of contents entry

| Offset | Type | Field |
|---|---|---|
| 0x00 | `u64` | Offset of the stored bytes |
| 0x08 | `u64` | Stored size |
| 0x10 | `u64` | Size of the file uncompressed |
| 0x18 | `u32` | Offset of the name within the names |
| 0x1C | `u32` | Length of the name |
| 0x20 | `u32` | Flags: bits 0-7 compression (0 = uncompressed, 1 = zstd), the rest 0 |
| 0x24 | `u8[20]` | SHA-1 of the uncompressed file |
| 0x38 | `u64` | Reserved: 0 |

- The **name** is the file's path inside the package, with `/` between folders and no NUL: `map/start/background.png`.
  The asset id comes from it as in a folder (the path without the extension, see [VFS](../architecture/vfs.md)).
- The entries are **sorted by asset id**, which is also the order of their paths because `.` goes before any
  character of an id. This way an entry is found with a binary search.
- **Compression** (version 2): with 1, the stored bytes are **a single zstd frame** that decompresses to
  exactly the file's size, with nothing after it; the frame carries that size in its header. With 0, the bytes are
  stored as they are and the two sizes are equal. `dwx pack` compresses each file and stores it as is when the
  compressed one is not smaller (PNGs, which are already compressed, usually stay that way).
- A compressed file cannot exceed 2 GiB uncompressed (`PakMaxEntrySize`).
- The **SHA-1** is used to check each file when it is read and as a content key; it is not a security measure.

## Determinism

The same files give the same `.pak`, byte for byte: entries go in id order, padding is zeros and no dates,
permissions or anything about the machine that writes it are stored. The manifest is copied unchanged.

Compression is also fixed: zstd 1.5.7 (the version pinned by the vcpkg baseline), level 9 (`PakZstdLevel`),
the content size in the frame header, no zstd checksum (the entry's SHA-1 already covers it), no dictionary id and no
zstd threads. `dwx pack` compresses several files at once, each with its own context,
and writes them in order, so the result does not depend on the number of threads. Another zstd version could
compress differently; the reader reads any valid frame the same.

Level 9 comes from measuring the base package of the US version (4619 files, 351 MB) in release:

| Level | `.pak` | `dwx pack` time (with the re-read) |
|---|---|---|
| 3 | 242.1 MB | 2.5 s |
| 9 | 240.7 MB | 2.9 s |
| 19 | 238.8 MB | 19 to 27 s |

From 9 to 19 you gain 0.8% in exchange for about seven times more time, and `dwx pack` runs on each user's machine
the first time they open the game.

## Versions

- **1:** all files uncompressed.
- **2:** each file with zstd or uncompressed. A version 2 file with no compressed files differs from a version 1 file
  only in the version number.

The reader accepts both: a version 1 file is a particular case of 2 (everything uncompressed), so reading it
costs nothing. A compressed entry in a version 1 file is rejected.

## What the reader checks

`PakFileSystem::open` rejects the file, with a message that says what fails, if:

- it is shorter than the header, the magic does not match or the version is neither 1 nor 2;
- the header flags are not 0 or the alignment is not a power of two of at least 16;
- the table, the names and the manifest do not follow one another without gaps up to the end of the file (so the
  file size bounds everything that is reserved);
- an entry uses a compression its version does not have or other flags, its sizes do not match without compression,
  its compressed size exceeds 2 GiB, its data is not aligned or goes outside the data area, or its name goes outside
  the names;
- a name does not give a valid id, is `pack.yaml`, or the entries are not in strict id order (which also rejects
  repeated ids).

When reading a compressed entry it checks that it is a single zstd frame of its size and decompresses it into a
buffer that starts at 1 MiB and doubles as the frame produces bytes, so a forged size does not make it reserve the
memory it claims: it fails when the frame runs out. zstd also limits its window to 128 MiB (its default limit). Then,
with or without compression, its SHA-1 is checked. If anything fails, the read fails and says the file is damaged.

## How it is mounted

`PakFileSystem` is a `FileSystem` with a single folder, the path of the file itself: listing it gives `pack.yaml` and
each entry, and `<file>/<entry>` reads that entry. Each package that is mounted carries its own file system
(`PackLocation`), so a single stack can have folders and files:

```cpp
const assets::DiskFileSystem disk;
auto base = assets::PakFileSystem::open("content/base.pak");
const std::array packs{assets::PackLocation{.fileSystem = &disk, .root = "content/packs/hd"},
                       assets::PackLocation{.fileSystem = base->get(), .root = "content/base.pak"}};
auto vfs = assets::VirtualFileSystem::mount(packs);
```

The paths of assets that come from a file look like `content/base.pak/map/start/background.png`.
