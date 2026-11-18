# Disc

How `formats/disc` reads the user's copy: the image, its sectors, the ISO9660 file system and the executable's file
table. Everything can be checked with `dwx extract --list`.

## Reading bytes

All readers use `ByteReader` (`packages/formats/include/dwe/formats/byte_reader.hpp`): it reads little-endian fields at an
offset and returns `std::nullopt` if the field does not fit in the buffer. Structs are never `reinterpret_cast` and
nothing is ever read out of range: a malformed file gives an error with the reason.

## Image

`DiscImage::open` accepts:

| Input | How it is read |
|---|---|
| `.cue` | The sheet is parsed and the BIN it names is opened, relative to the `.cue` folder |
| `.bin` without a sheet | 2352-byte sectors if the first sector starts with the sync pattern |
| `.iso` (or other) | 2048-byte sectors if the size is a multiple of 2048 |

The image is read **sector by sector** from the file: a 700 MB image is never loaded whole. If the size is not a
whole number of sectors, it is an error.

### CUE sheet

Only what a single-track data disc uses is supported:

- A single `FILE "…" BINARY`.
- A single `TRACK 01` in mode `MODE2/2352`, `MODE1/2352` or `MODE1/2048`.
- `INDEX 01 00:00:00`: the track starts at the beginning of the file.
- `REM`, `CATALOG`, `CDTEXTFILE`, `TITLE`, `PERFORMER`, `SONGWRITER`, `FLAGS` and `ISRC` are ignored.

Several tracks or files, audio tracks, `PREGAP`/`POSTGAP` or a different `INDEX` give an error that says the line and
the reason. It accepts CRLF line endings, the UTF-8 BOM and quoted names with spaces and parentheses.

## Sectors

A raw sector is 2352 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 12 | Sync pattern `00 FF×10 00` |
| 12 | 3 | Address (minute, second, sector in BCD); not checked |
| 15 | 1 | Mode: 1 or 2 |
| 16 | 8 | Mode 2 only: subheader (two copies of file, channel, submode, coding) |
| 24 | 2048 or 2324 | Mode 2 only: user data |

- **Mode 1:** 2048 bytes of data at offset 16.
- **Mode 2 form 1:** 2048 bytes at offset 24. This is what normal files use.
- **Mode 2 form 2:** 2324 bytes at offset 24, without error correction. Bit `0x20` of the submode marks form 2.
  Interleaved XA audio uses it; the video sectors of the STR files are form 1.

A sector without the sync pattern or with another mode is an error. In a 2048-byte ISO all sectors are form 1.

## ISO9660

`readIsoVolume` reads the volume following the ECMA-119 standard:

- The **primary descriptor** is at sector 16: type 1, `CD001`, version 1. The volume name (offset 40), the number of
  sectors (80), the block size (128, must be 2048) and the root record (156) are read.
- The **directory records**: length (0), LBA (2), size (10), flags (25; `0x02` = folder) and name (32 and
  33). Numbers come in both endiannesses; the little-endian half is read. A length byte of 0 marks padding up to the
  end of the sector: records do not cross sectors.
- The version (`;1`) and the trailing dot of names without an extension are removed from the name. Paths are given
  with `/`: `MAP/MAP1/NAME.MAP`, sorted by path.

The following is rejected, with an error:

- Records that do not fit in their sector, shorter than 33 bytes or with the name outside the record.
- Extended attributes, multi-extent or interleaved files (no game disc uses them).
- Empty names, `.`, `..` or names with `/`, `\` or control characters: this way an extraction never writes outside
  its folder.
- Folders or files that go outside the image.
- **Loops:** a folder that points to one already visited. More than 32 levels of folders or more than 65536 files.
- Two files with the same path.

### Content and SHA-1 of a file

- `readIsoFile` reads a file of form 1 sectors, cut to its size.
- `hashIsoFile` computes the SHA-1 with the data of each sector according to its form: 2048 bytes per form 1 sector
  (the last one, cut to the size) and 2324 per form 2 sector. For a normal file it is the SHA-1 of its bytes; the
  STR files, which mix video (form 1) and XA audio (form 2), are hashed whole, because the ISO size counts 2048 bytes
  per sector.

## Executable

The disc boots the executable named by `SYSTEM.CNF` (`BOOT = cdrom:\NAME;1`); `findBootExecutable` looks for it in
the volume without distinguishing case.

`readPsxExeHeader` reads the PS-X EXE header: the `PS-X EXE` mark, `pc0` (0x10), `gp0` (0x14), `t_addr` (0x18, load
address) and `t_size` (0x1C). The text segment starts at offset 0x800 of the file and must fit in it. A memory
address is converted to a file offset with `0x800 + (address − t_addr)`, always inside the segment. No load address
is assumed: it is read from the header.

## File table

The original opens almost all files by their name without a folder, looking it up in the executable's
`FILE_OFFSET_TABLE` (`src/main/file.c: lookupFileTable`, `readFile`; `include/dw/file_table.h: FileEntry`):

| Offset | Type | Field |
|---|---|---|
| 0 | `s32` | `sector`: LBA of the first sector |
| 4 | `s32` | `size`: size in bytes |
| 8 | `char*` | `filename`: pointer to a name inside the executable |

The table ends at the first entry with `size` 0. `readFileTable` reads it from each version's address and
follows each pointer to its NUL (64 characters at most), checking that everything is inside the text segment.

| Version | Address | Entries (with the final one) | Source |
|---|---|---|---|
| US | `0x8011FE1C` | 819 | `config/us/symbols.txt`, `src/main/file_table_us.c` |
| EU | `0x80125F44` | 851 | `config/eu/symbols.txt`, `src/main/file_table_eu.c` |
| JP | `0x80125E18` | 816 | `config/jp/symbols.txt`, `src/main/file_table_jp.c` |
| JP rev. 1 | `0x80126730` | 816 | `config/jp_rev1/symbols.txt`, `src/main/file_table_jp_rev1.c` |

Only the US one has been checked against a disc.

### Check against ISO9660

`checkFileTable` looks up each entry in the volume:

1. By **name** (without a folder) with the same sector and size.
2. If no file has that name, by **sector and size**: it is an alias. On the US disc, `PSX.EXE` is another
   name for the executable `SLUS_010.32`.
3. If nothing matches, the entry points to data that the folders hide ("only in the table").

A size also matches if it counts **2336 bytes per sector**: the original reads the STR videos as whole mode 2
sectors (subheader, data and error detection), and the table stores their size that way. An entry with a file's name
but another sector or another size is a discrepancy.

### Files opened by path

Some files are opened with `CdSearchFile` and their full path instead of through the table. **In the US they are also in
the table**:

| File | Where |
|---|---|
| `SCN\DG.SCN` | `src/main/script_interp.c: readFileSection` |
| `ETCHI\EFEDAT.EFE` | `src/main/efe.c` |
| `ETCDAT\FEEL_EF.TMD`, `ETCDAT\EX.TIM`, `ETCDAT\EX.TMD` | `src/main/efe_table.c: EFEX_PATH_ARRAY` |
| `MOVIE\OP1.STR`, `OP2.STR`, `ED2.STR`, `EDR.STR` | `src/mov/mov.c: MOV_MOVIES` |

In addition, the read queue (`src/main/file_queue.c`) looks up by path any request with a negative size.

## Checked US disc

With the US image (executable `31ec666b987d5b007dec40eecc4092d27ed9085b`):

- 818 files in the ISO9660 and 818 entries in the table (819 with the final one).
- All files in the volume are in the table: 817 by name and the executable by its alias `PSX.EXE`.
- No entry only in the table and no discrepancy. The four STR files match the 2336-byte rule.

## `dwx extract --list`

```bash
build/<preset>/apps/dwx/dwx extract --list [--verify-image] <image.cue|image.bin|image.iso>
```

It opens the image, reads the volume, identifies the version by the executable's SHA-1 (`known_releases` table) and
lists each file **in disc order** (LBA and, on a tie, path): LBA, size, SHA-1, whether it is in the table (`yes`, `no`,
`hidden` if it is only in the table, `-` if the version is not known) and path. Then it summarizes the files outside
the table, those opened by path, the aliases and the discrepancies. The output is deterministic.

- `--verify-image` also computes the SHA-1 of the image file (slow; there are no image hashes in the table yet, so it
  is only shown).
- Exit codes, like `dwx verify`: 0 known version and consistent table, 1 unknown version or discrepancies,
  2 error.

It writes nothing: without `--list`, [`dwx extract`](extract.md) writes the base package.

## Tests

- Unit (`packages/formats/tests/`): synthetic fixtures made by the test itself (`disc_fixtures.hpp`): raw sectors
  with their sync and subheader, a small ISO9660 volume with nested folders, CUE sheets and a fake executable with its
  table. They cover the malformed cases: truncated images, offsets outside the image, folder loops, dangerous names,
  tables without an end and pointers outside the executable.
- With the disc (`real_disc_tests.cpp`): only if `DWE_DISC` points to the image
  ([setting up the environment](../getting-started/setup.md)); otherwise it is skipped. It checks the version, the
  table against the ISO9660 and that the number of files is reasonable.
