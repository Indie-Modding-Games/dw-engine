# content

How the apps mount the packs (`dwe::content::GameContent`): the content folder, its `packs/` folder, the team packs
from the source tree when there is no `game` pack, and the mod folders, all in one VFS
([vfs](../../architecture/vfs.md)). The game and `dwe-sim` both use it, so a simulation sees the same packs as a
playthrough.

`disc_install.hpp` handles the player's own content: on first launch `DiscInstall` reads their disc image with
`dwx identify` and `dwx extract --progress` into `content.partial/` in the user data folder and moves the packs to
`content/` when done, with an `install.yaml` (`InstallRecord`: extractor version and image) so it can read it again
without asking when the extractor changes (`installIsOutdated`). Without `--content`, `defaultContentFolder` uses a
`./content` with a base pack or, if there is none, the installed one; `teamPacksFolder` looks for the team packs next
to the executable (`packs/`, or `Resources/packs/` on macOS) or, in a development build, in the source tree
([Linux beta](../../guides/beta-linux.md)).

The game packs (the content folder, `packs/` and the team packs) must all mount and require a Lua API that the engine
has (`assets::checkPackApi`); otherwise mounting fails. A mod that cannot mount (a broken `pack.yaml`, a missing
dependency, a different API version, Lua files without `api`) is left out and goes to the log, and the others are
mounted anyway (a data-only mod without `api` is mounted with a warning, `warnModsWithoutApi`):
`skippedMods()` gives the list with the reason for each one, for the future player mod list. All the mods are tried
together first; only if that fails are they tried one by one, in rounds, so that a mod that needs another one gets in
even if it appears earlier. With development assets, a mod with the id of a development pack is left out and the
development pack stays, so a mod never stops the game from starting.

The **origin** of each pack (`assets::PackOrigin`) comes from where it was found, never from its `pack.yaml`: the
content folder, its `packs/`, the team packs and the development packs are `Engine`; those from the mod folders and
from `--mods` are `Mod`. Only the code of an `Engine` pack in the `base` or `remaster` layers runs as the engine (see
[script](../engine/script.md)). A mod that declares the `base` or `remaster` layer is left out with the
reason (`assets::checkModLayer`): those layers belong to the engine.

With development assets (`--dev-assets`, off by default) `GameContent::mount` also mounts, below all the normal
packs, the packs from its extracted folders whose id has no normal pack, and the log marks them. `dev_assets.hpp` owns
the option, the `dev-assets.yaml` file (`devAssetsConfigFolders` says where to look; `parseDevAssetsConfig` and
`loadDevAssets` read it, check the roots and register them) and the text key of the notice
([development assets](../../architecture/development-assets.md)).

If the file names a disc image (`disc_image`), `dev_disc.hpp` asks `dwx extract --cache` for it, in another process
and without a shell, and adds the folder it returns, from the development cache in the user data folder, after the
extracted folders from the file. `openDevAssets` (in `dev_assets.hpp`) joins the two steps for the game and
`dwe-sim`, which share a data folder (`user_data.hpp`).

With a user data folder, `user_ui_addons.hpp` mounts its `ui/addons/` as one more mod
(`UserUiAddonsFileSystem`, pack `user_ui_addons`): the interfaces that the player copies as a folder or as a `.zip`
([interfaces](../../modding/ui-addons.md)). `zip_archive.hpp` (`ZipArchive`) reads a `.zip` in place, without
unpacking it to disk: it checks its whole directory on opening (paths, links, encryption, entries, sizes and
compression ratio, `ZipLimits`, the same limits as the launcher) and expands each file only when it is requested,
with `lodepng_inflate` bounded to its declared size and its CRC-32 checked. The zips that do not provide an interface
come out in `GameContent::leftOutUiAddonArchives()`; the ones covered by a folder or another zip with the same id go
to the log on mount, and the game treats the rest as interfaces that are not read.
