use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use crate::{AssetId, Error, Layer, PackManifest, Result};

/// Where a pack comes from: the engine's own folders (content, team packs) or a mods folder.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PackOrigin {
    Mod,
    Engine,
}

/// Only the engine's own packs of the base layer have the base's rights: adding table entries
/// without a namespace and giving them their index.
pub fn has_base_rights(origin: PackOrigin, layer: Layer) -> bool {
    origin == PackOrigin::Engine && layer == Layer::Base
}

/// A pack folder to mount.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PackLocation {
    pub root: PathBuf,
    pub origin: PackOrigin,
}

/// A mounted pack: its manifest and its files by asset id.
#[derive(Clone, Debug)]
pub struct MountedPack {
    pub manifest: PackManifest,
    pub root: PathBuf,
    pub origin: PackOrigin,
    files: BTreeMap<AssetId, PathBuf>,
}

impl MountedPack {
    /// Reads a pack folder: its manifest and every file, which must give a valid asset id.
    pub fn read(location: &PackLocation) -> Result<Self> {
        let manifest = PackManifest::read(&location.root)?;
        let mut files = BTreeMap::new();
        list_files(&location.root, &location.root, &mut files)?;
        Ok(Self {
            manifest,
            root: location.root.clone(),
            origin: location.origin,
            files,
        })
    }

    pub fn id(&self) -> &str {
        &self.manifest.id
    }

    pub fn has_base_rights(&self) -> bool {
        has_base_rights(self.origin, self.manifest.layer)
    }

    /// The file of an asset in this pack, or None.
    pub fn file(&self, id: &AssetId) -> Option<&Path> {
        self.files.get(id).map(PathBuf::as_path)
    }

    pub fn ids(&self) -> impl Iterator<Item = &AssetId> {
        self.files.keys()
    }
}

/// Lists a pack's files by id, skipping `pack.yaml` at the root and anything whose name starts
/// with a dot. Symbolic links are errors: a pack never reads outside its folder.
fn list_files(root: &Path, folder: &Path, files: &mut BTreeMap<AssetId, PathBuf>) -> Result<()> {
    let entries = std::fs::read_dir(folder).map_err(|error| Error::io(folder, error))?;
    let mut paths = Vec::new();
    for entry in entries {
        paths.push(entry.map_err(|error| Error::io(folder, error))?.path());
    }
    paths.sort();
    for path in paths {
        let name = path.file_name().and_then(|n| n.to_str()).unwrap_or("");
        if name.starts_with('.') {
            continue;
        }
        let metadata = std::fs::symlink_metadata(&path).map_err(|error| Error::io(&path, error))?;
        if metadata.file_type().is_symlink() {
            return Err(Error::invalid(format!(
                "{}: symbolic links are not allowed in a pack",
                path.display()
            )));
        }
        if metadata.is_dir() {
            list_files(root, &path, files)?;
            continue;
        }
        let relative = path
            .strip_prefix(root)
            .map_err(|_| Error::invalid(format!("{} is outside its pack", path.display())))?;
        let relative: Vec<_> = relative
            .components()
            .map(|part| part.as_os_str().to_string_lossy().into_owned())
            .collect();
        let relative = relative.join("/");
        if relative == "pack.yaml" {
            continue;
        }
        let id = AssetId::of_file(&relative)
            .map_err(|error| Error::invalid(format!("{}: {error}", root.display())))?;
        if let Some(other) = files.get(&id) {
            return Err(Error::invalid(format!(
                "'{}' and '{relative}' both give the asset id '{id}'",
                other.display()
            )));
        }
        files.insert(id, path);
    }
    Ok(())
}

/// The packs of a content set stacked as the engine stacks them (virtual_file_system.cpp): by
/// layer, then priority, then dependencies inside a tie, then id. The highest pack that has an id
/// wins it.
#[derive(Clone, Debug, Default)]
pub struct Vfs {
    /// From the bottom of the stack up.
    packs: Vec<MountedPack>,
    /// Each id's packs, as indices into `packs`, from the bottom up.
    sources: BTreeMap<AssetId, Vec<usize>>,
}

impl Vfs {
    pub fn mount(locations: &[PackLocation]) -> Result<Self> {
        let packs = locations
            .iter()
            .map(MountedPack::read)
            .collect::<Result<Vec<_>>>()?;
        Self::from_packs(packs)
    }

    /// Stacks packs already read, checking ids and dependencies.
    pub fn from_packs(mut packs: Vec<MountedPack>) -> Result<Self> {
        packs.sort_by(|a, b| a.manifest.id.cmp(&b.manifest.id));
        if let Some(pair) = packs.windows(2).find(|pair| pair[0].id() == pair[1].id()) {
            return Err(Error::invalid(format!(
                "pack '{}' is mounted twice: from '{}' and '{}'",
                pair[0].id(),
                pair[0].root.display(),
                pair[1].root.display()
            )));
        }
        check_dependencies(&packs)?;
        check_cycles(&packs)?;
        let packs = stack(packs);
        let mut sources: BTreeMap<AssetId, Vec<usize>> = BTreeMap::new();
        for (index, pack) in packs.iter().enumerate() {
            for id in pack.ids() {
                sources.entry(id.clone()).or_default().push(index);
            }
        }
        Ok(Self { packs, sources })
    }

    /// The packs from the bottom of the stack up.
    pub fn packs(&self) -> &[MountedPack] {
        &self.packs
    }

    /// The pack that wins an id and its file, or None.
    pub fn resolve(&self, id: &AssetId) -> Option<(&MountedPack, &Path)> {
        let pack = &self.packs[*self.sources.get(id)?.last()?];
        Some((pack, pack.file(id)?))
    }

    /// The bytes of the file that wins an id.
    pub fn read(&self, id: &AssetId) -> Result<Vec<u8>> {
        let (_, file) = self
            .resolve(id)
            .ok_or_else(|| Error::invalid(format!("no pack has the asset '{id}'")))?;
        std::fs::read(file).map_err(|error| Error::io(file, error))
    }

    /// The text of the file that wins an id.
    pub fn read_text(&self, id: &AssetId) -> Result<String> {
        let bytes = self.read(id)?;
        String::from_utf8(bytes).map_err(|_| Error::invalid(format!("asset '{id}' is not UTF-8")))
    }

    pub fn contains(&self, id: &AssetId) -> bool {
        self.sources.contains_key(id)
    }

    /// Every id, sorted.
    pub fn ids(&self) -> impl Iterator<Item = &AssetId> {
        self.sources.keys()
    }

    /// The ids that start with a prefix, sorted.
    pub fn ids_with_prefix<'a>(
        &'a self,
        prefix: &'a str,
    ) -> impl Iterator<Item = &'a AssetId> + 'a {
        self.sources
            .range(AssetId::unchecked(prefix)..)
            .map(|(id, _)| id)
            .take_while(move |id| id.as_str().starts_with(prefix))
    }

    /// Every pack that has an id, from the bottom up.
    pub fn sources(&self, id: &AssetId) -> Vec<&MountedPack> {
        self.sources
            .get(id)
            .map(|indices| indices.iter().map(|&index| &self.packs[index]).collect())
            .unwrap_or_default()
    }

    /// The ids more than one pack has, to see what overrides what.
    pub fn overridden_ids(&self) -> Vec<&AssetId> {
        self.sources
            .iter()
            .filter(|(_, packs)| packs.len() > 1)
            .map(|(id, _)| id)
            .collect()
    }
}

fn index_of(packs: &[MountedPack], id: &str) -> Option<usize> {
    packs.iter().position(|pack| pack.id() == id)
}

fn check_dependencies(packs: &[MountedPack]) -> Result<()> {
    for pack in packs {
        for dependency in &pack.manifest.dependencies {
            let Some(found) = index_of(packs, &dependency.id).map(|index| &packs[index]) else {
                return Err(Error::invalid(format!(
                    "pack '{}' needs '{}', which is not mounted",
                    pack.id(),
                    dependency.id
                )));
            };
            if found.manifest.version < dependency.min_version {
                return Err(Error::invalid(format!(
                    "pack '{}' needs '{}' {} or newer, but {} is mounted",
                    pack.id(),
                    found.id(),
                    dependency.min_version,
                    found.manifest.version
                )));
            }
            if found.manifest.layer > pack.manifest.layer {
                return Err(Error::invalid(format!(
                    "pack '{}' ({}) cannot depend on '{}', which is in the higher layer {}",
                    pack.id(),
                    pack.manifest.layer.name(),
                    found.id(),
                    found.manifest.layer.name()
                )));
            }
        }
    }
    Ok(())
}

fn check_cycles(packs: &[MountedPack]) -> Result<()> {
    #[derive(Clone, Copy, PartialEq)]
    enum Mark {
        New,
        Open,
        Done,
    }
    fn visit(
        packs: &[MountedPack],
        index: usize,
        marks: &mut [Mark],
        path: &mut Vec<usize>,
    ) -> Result<()> {
        marks[index] = Mark::Open;
        path.push(index);
        for dependency in &packs[index].manifest.dependencies {
            let Some(next) = index_of(packs, &dependency.id) else {
                continue;
            };
            match marks[next] {
                Mark::Open => {
                    let start = path.iter().position(|&at| at == next).unwrap_or(0);
                    let cycle: Vec<_> = path[start..].iter().map(|&at| packs[at].id()).collect();
                    return Err(Error::invalid(format!(
                        "dependency cycle: {} -> {}",
                        cycle.join(" -> "),
                        dependency.id
                    )));
                }
                Mark::New => visit(packs, next, marks, path)?,
                Mark::Done => {}
            }
        }
        path.pop();
        marks[index] = Mark::Done;
        Ok(())
    }
    let mut marks = vec![Mark::New; packs.len()];
    for index in 0..packs.len() {
        if marks[index] == Mark::New {
            visit(packs, index, &mut marks, &mut Vec::new())?;
        }
    }
    Ok(())
}

/// Orders packs sorted by id from the bottom of the stack up: by layer, then priority; the packs
/// of a tie go after the packs of the tie they depend on, and otherwise by id.
fn stack(mut packs: Vec<MountedPack>) -> Vec<MountedPack> {
    let key = |pack: &MountedPack| (pack.manifest.layer, pack.manifest.priority);
    packs.sort_by_key(key);
    let mut stacked = Vec::with_capacity(packs.len());
    let mut rest = packs.into_iter().peekable();
    while let Some(first) = rest.next() {
        let mut group = vec![first];
        while rest.peek().is_some_and(|next| key(next) == key(&group[0])) {
            group.extend(rest.next());
        }
        let mut placed = vec![false; group.len()];
        let mut order = Vec::with_capacity(group.len());
        // Without cycles, every round places one pack.
        while order.len() < group.len() {
            let next = (0..group.len()).find(|&index| {
                !placed[index]
                    && group[index].manifest.dependencies.iter().all(|dependency| {
                        index_of(&group, &dependency.id).is_none_or(|at| placed[at])
                    })
            });
            let Some(next) = next else { break };
            placed[next] = true;
            order.push(next);
        }
        let mut slots: Vec<Option<MountedPack>> = group.into_iter().map(Some).collect();
        stacked.extend(order.into_iter().filter_map(|index| slots[index].take()));
    }
    stacked
}

#[cfg(test)]
mod tests {
    use super::*;

    fn write_pack(root: &Path, folder: &str, manifest: &str, files: &[&str]) -> PackLocation {
        let pack = root.join(folder);
        std::fs::create_dir_all(&pack).unwrap();
        std::fs::write(pack.join("pack.yaml"), manifest).unwrap();
        for file in files {
            let path = pack.join(file);
            std::fs::create_dir_all(path.parent().unwrap()).unwrap();
            std::fs::write(path, folder).unwrap();
        }
        PackLocation {
            root: pack,
            origin: PackOrigin::Engine,
        }
    }

    fn ids_of(vfs: &Vfs) -> Vec<&str> {
        vfs.packs().iter().map(MountedPack::id).collect()
    }

    #[test]
    fn stacks_by_layer_priority_dependencies_and_id() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        let locations = vec![
            write_pack(root, "m2", "id: mod_b\nversion: 1.0.0\nlayer: mods\n", &[]),
            write_pack(
                root,
                "m1",
                "id: mod_a\nversion: 1.0.0\nlayer: mods\ndependencies:\n  - id: mod_b\n    min_version: 1.0.0\n",
                &[],
            ),
            write_pack(
                root,
                "hi",
                "id: high\nversion: 1.0.0\nlayer: mods\npriority: 5\n",
                &[],
            ),
            write_pack(root, "b", "id: base\nversion: 1.0.0\nlayer: base\n", &[]),
            write_pack(
                root,
                "g",
                "id: game\nversion: 1.0.0\nlayer: base\npriority: 1\n",
                &[],
            ),
            write_pack(
                root,
                "u",
                "id: user_pack\nversion: 1.0.0\nlayer: user\n",
                &[],
            ),
        ];
        let vfs = Vfs::mount(&locations).unwrap();
        // mod_a depends on mod_b, so it goes above it despite its id.
        assert_eq!(
            ids_of(&vfs),
            vec!["base", "game", "mod_b", "mod_a", "high", "user_pack"]
        );
    }

    #[test]
    fn the_highest_pack_wins_whatever_the_extension() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        let locations = vec![
            write_pack(
                root,
                "hd",
                "id: hd\nversion: 1.0.0\nlayer: remaster\n",
                &["map/area_a/background_day.ktx2"],
            ),
            write_pack(
                root,
                "base",
                "id: base\nversion: 1.0.0\nlayer: base\n",
                &[
                    "map/area_a/background_day.png",
                    "map/area_a/data.yaml",
                    ".git/x",
                ],
            ),
        ];
        let vfs = Vfs::mount(&locations).unwrap();
        let background = AssetId::parse("map/area_a/background_day").unwrap();
        let (pack, file) = vfs.resolve(&background).unwrap();
        assert_eq!(pack.id(), "hd");
        assert!(file.ends_with("background_day.ktx2"));
        assert_eq!(vfs.read_text(&background).unwrap(), "hd");
        assert_eq!(vfs.sources(&background).len(), 2);
        assert_eq!(vfs.overridden_ids(), vec![&background]);
        assert_eq!(vfs.ids().count(), 2);
        assert_eq!(
            vfs.ids_with_prefix("map/area_a/").count(),
            2,
            "both files of the map"
        );
    }

    #[test]
    fn refuses_twice_mounted_missing_and_cyclic_dependencies() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        let a = write_pack(root, "a", "id: same\nversion: 1.0.0\nlayer: base\n", &[]);
        let b = write_pack(root, "b", "id: same\nversion: 1.0.0\nlayer: base\n", &[]);
        assert!(Vfs::mount(&[a, b]).is_err());

        let missing = write_pack(
            root,
            "c",
            "id: c\nversion: 1.0.0\nlayer: mods\ndependencies:\n  - id: nowhere\n    min_version: 1.0.0\n",
            &[],
        );
        assert!(Vfs::mount(&[missing]).is_err());

        let old = write_pack(root, "d", "id: d\nversion: 1.0.0\nlayer: base\n", &[]);
        let wants_new = write_pack(
            root,
            "e",
            "id: e\nversion: 1.0.0\nlayer: mods\ndependencies:\n  - id: d\n    min_version: 2.0.0\n",
            &[],
        );
        assert!(Vfs::mount(&[old.clone(), wants_new]).is_err());

        let higher = write_pack(root, "f", "id: f\nversion: 1.0.0\nlayer: mods\n", &[]);
        let base_needs_mod = write_pack(
            root,
            "g",
            "id: g\nversion: 1.0.0\nlayer: base\ndependencies:\n  - id: f\n    min_version: 1.0.0\n",
            &[],
        );
        assert!(Vfs::mount(&[higher, base_needs_mod]).is_err());

        let x = write_pack(
            root,
            "x",
            "id: x\nversion: 1.0.0\nlayer: mods\ndependencies:\n  - id: y\n    min_version: 1.0.0\n",
            &[],
        );
        let y = write_pack(
            root,
            "y",
            "id: y\nversion: 1.0.0\nlayer: mods\ndependencies:\n  - id: x\n    min_version: 1.0.0\n",
            &[],
        );
        let error = Vfs::mount(&[x, y]).unwrap_err().to_string();
        assert!(error.contains("cycle"), "{error}");
    }

    #[test]
    fn refuses_files_without_a_valid_id() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        let bad = write_pack(
            root,
            "a",
            "id: a\nversion: 1.0.0\nlayer: base\n",
            &["map/Area/data.yaml"],
        );
        assert!(Vfs::mount(&[bad]).is_err());
        let twice = write_pack(
            root,
            "b",
            "id: b\nversion: 1.0.0\nlayer: base\n",
            &["model.gltf", "model.glb"],
        );
        assert!(Vfs::mount(&[twice]).is_err());
    }
}
