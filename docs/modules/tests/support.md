# Test support

What the tests of all modules share, linked with `dwe_test_main`.

`<dwe/test_support/user_assets.hpp>` is the only place that reads the variables holding the user's own assets that
`scripts/dev.sh` mounts read-only: `userDisc()` (`DWE_DISC`, the disc image), `userContent()`
(`DWE_CONTENT`, a folder extracted with `dwx`) and `userDecomp()` (`DWE_DECOMP`, a copy of the decomp). CI has
none of them: a test that needs them is skipped when they are missing and its name starts with "the user's disc" so it runs as a
slow test.

`<dwe/test_support/temp_folder.hpp>` is `TempFolder`, a folder with a unique name under the system temporary folder
that is deleted with everything in it at the end of the test. The game and the tools do not read these variables: in development they use `--dev-assets`
([development assets](../../architecture/development-assets.md)).

`<dwe/test_support/zip_writer.hpp>` writes `.zip` archives in tests (`zipArchive`, `writeZip`, `zipFilesOf` for
a whole folder), with headers that can be forged (method, declared size, encryption, link attributes) to
test the rejections of the `content` reader.

`<dwe/test_support/script_warp_fixtures.hpp>` is the `field/script_warps` of a synthetic script with a recursion: the
`formats` tests check that extraction produces it and the `field` ones that the field takes that warp and registers it
once.
