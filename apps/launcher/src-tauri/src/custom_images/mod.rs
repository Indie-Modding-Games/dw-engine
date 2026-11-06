//! The player's own images (docs/guides/imagenes-personalizadas.md): the launcher exports the
//! images the game read from the player's disc (dwx export-images, into <data>/custom-images/
//! originals), and keeps the ones the player loads in one pack of the user layer
//! (<data>/custom-images/pack), which the game mounts with --mods while it is on. Each file the
//! player brings is matched by name to an original and checked as the engine checks it: a PNG of
//! the same aspect ratio, a whole number of times larger (1 to 8).

pub mod picked;
pub mod png;

use base64::Engine;
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::path::{Path, PathBuf};

use crate::failure::{code, Failure, Result};
use picked::Picked;
use png::Size;

pub const FOLDER: &str = "custom-images";
const PACK_FOLDER: &str = "pack";
const ORIGINALS_FOLDER: &str = "originals";
const INCOMING_FOLDER: &str = "incoming";
const STATE_FILE: &str = "state.json";
const STAMP_FILE: &str = "originals.stamp";
const MANIFEST_FILE: &str = "pack.yaml";
const MANIFEST: &str = include_str!("pack.yaml");
/// What dwx export-images writes beside the images, and the names an export gives it and its guide.
const MAPS_FILE: &str = "maps.tsv";
const EXPORTED_MAPS_FILE: &str = "mapas.tsv";
const README_FILE: &str = "LEEME.txt";
const README: &str = include_str!("LEEME.txt");
const EXPORT_FOLDER: &str = "dwe-imagenes";
const PNG_EXTENSION: &str = ".png";
/// The two kinds of image and how their names are made: map/<map>/background_<variant>.png and
/// texture/<folders>/<image>_palette<N>.png.
const MAP_ROOT: &str = "map";
const TEXTURE_ROOT: &str = "texture";
const BACKGROUND_PREFIX: &str = "background_";
const PALETTE_SUFFIX: &str = "_palette";
/// The order of a map's backgrounds; any other variant follows by name.
const VARIANT_ORDER: [&str; 3] = ["day", "dusk", "night"];
const PNG_DATA_URL: &str = "data:image/png;base64,";

/// Why a file the player brought is not used.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(tag = "kind", rename_all = "kebab-case")]
pub enum Problem {
    /// Its name is no image of the game.
    UnknownName,
    /// Another file already gave this image.
    Duplicate,
    NotPng,
    /// Not the original's aspect ratio, or not a whole number of times its size.
    WrongSize { width: u32, height: u32, expected_width: u32, expected_height: u32 },
    TooLarge { scale: u32 },
    /// A map's backgrounds share one scale (the engine draws them mixed).
    MixedScale { scale: u32, other: u32 },
    /// The same bytes as the original: nothing to use.
    Unchanged,
}

pub type Checked = std::result::Result<u32, Problem>;

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Matched {
    pub name: String,
    pub target: String,
    pub scale: u32,
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Rejected {
    pub name: String,
    pub problem: Problem,
}

/// What a folder or .zip holds for the pack, before (or after) it is applied.
#[derive(Debug, Default, Serialize)]
pub struct BulkSummary {
    pub matched: Vec<Matched>,
    pub rejected: Vec<Rejected>,
    pub unchanged: usize,
    /// Files that are no PNG (a guide, the map list): not looked at.
    pub ignored: usize,
    pub applied: bool,
}

#[derive(Debug, Serialize)]
pub struct FileRow {
    pub target: String,
    /// day, dusk, night or paletteN for a background; the palette number for an image.
    pub variant: String,
    pub width: u32,
    pub height: u32,
    pub custom: bool,
    pub custom_scale: Option<u32>,
}

#[derive(Debug, Serialize)]
pub struct Entry {
    /// The map's id (mayo01) or the image's path (texture/etcdat/fi_info).
    pub id: String,
    pub name: String,
    pub files: Vec<FileRow>,
}

/// Maps grouped by the letters their id starts with (the game has no zones the launcher can read),
/// named by the name most of them share; images grouped by folder.
#[derive(Debug, Serialize)]
pub struct Group {
    pub key: String,
    pub kind: &'static str,
    pub name: String,
    pub entries: Vec<Entry>,
}

#[derive(Debug, Serialize)]
pub struct Overview {
    pub prepared: bool,
    pub enabled: bool,
    pub custom_count: usize,
    pub groups: Vec<Group>,
}

#[derive(Debug, Default, Serialize, Deserialize)]
#[serde(default)]
struct State {
    enabled: bool,
}

/// The images of the game the player can replace, by their path in the pack, and the map names.
pub struct Catalogue {
    images: BTreeMap<String, Size>,
    names: BTreeMap<String, String>,
}

impl Catalogue {
    /// No images: the originals are not exported yet.
    pub fn empty() -> Self {
        Self { images: BTreeMap::new(), names: BTreeMap::new() }
    }

    /// Reads an export: every PNG's size from its header, and the map names.
    pub fn load(originals: &Path) -> Result<Self> {
        let mut files = Picked::Folder(originals.to_path_buf());
        let mut images = BTreeMap::new();
        for name in files.names()? {
            if !name.ends_with(PNG_EXTENSION) {
                continue;
            }
            if let Some(size) = png::size(&files.header(&name)?) {
                images.insert(name, size);
            }
        }
        let names = fs::read_to_string(originals.join(MAPS_FILE))
            .unwrap_or_default()
            .lines()
            .filter_map(|line| line.split_once('\t'))
            .map(|(id, name)| (id.to_string(), name.trim().to_string()))
            .collect();
        Ok(Self { images, names })
    }

    pub fn size_of(&self, target: &str) -> Option<Size> {
        self.images.get(target).copied()
    }

    /// The image a file names: its path from the first map/ or texture/ folder on, in lowercase,
    /// so a folder or .zip that holds the export inside another folder still matches.
    pub fn target_of(&self, name: &str) -> Option<String> {
        let lower = name.replace('\\', "/").to_lowercase();
        let parts: Vec<&str> = lower.split('/').filter(|part| !part.is_empty()).collect();
        let start = parts.iter().position(|part| *part == MAP_ROOT || *part == TEXTURE_ROOT)?;
        let target = parts[start..].join("/");
        self.images.contains_key(&target).then_some(target)
    }
}

/// The map a background belongs to (map/<map>/...), or nothing for an image of texture/.
fn map_of(target: &str) -> Option<&str> {
    let rest = target.strip_prefix(MAP_ROOT)?.strip_prefix('/')?;
    rest.split_once('/').map(|(map, _)| map)
}

pub struct CustomImages {
    root: PathBuf,
}

impl CustomImages {
    pub fn new(data: &Path) -> Self {
        Self { root: data.join(FOLDER) }
    }

    pub fn pack(&self) -> PathBuf {
        self.root.join(PACK_FOLDER)
    }

    pub fn originals(&self) -> PathBuf {
        self.root.join(ORIGINALS_FOLDER)
    }

    fn state(&self) -> State {
        fs::read(self.root.join(STATE_FILE))
            .ok()
            .and_then(|bytes| serde_json::from_slice(&bytes).ok())
            .unwrap_or(State { enabled: true })
    }

    pub fn enabled(&self) -> bool {
        self.state().enabled
    }

    pub fn set_enabled(&self, enabled: bool) -> Result<()> {
        let text = serde_json::to_vec(&State { enabled }).map_err(|e| Failure::new(code::IO, e.to_string()))?;
        crate::store::write_atomically(&self.root.join(STATE_FILE), &text)
    }

    /// The images in the pack, by their path in it.
    fn custom_targets(&self) -> BTreeSet<String> {
        if !self.pack().is_dir() {
            return BTreeSet::new();
        }
        Picked::Folder(self.pack())
            .names().unwrap_or_default().into_iter().filter(|name| name.ends_with(PNG_EXTENSION)).collect()
    }

    /// The pack the game mounts: on, and with at least one image.
    pub fn active_pack(&self) -> Option<PathBuf> {
        (self.enabled() && !self.custom_targets().is_empty()).then(|| self.pack())
    }

    /// Whether the export matches what is installed now (stamp: the game and the content).
    pub fn prepared(&self, stamp: &str) -> bool {
        fs::read_to_string(self.root.join(STAMP_FILE)).is_ok_and(|saved| saved == stamp)
            && self.originals().join(MAPS_FILE).is_file()
    }

    pub fn mark_prepared(&self, stamp: &str) -> Result<()> {
        crate::store::write_atomically(&self.root.join(STAMP_FILE), stamp.as_bytes())
    }

    pub fn catalogue(&self) -> Result<Catalogue> {
        Catalogue::load(&self.originals())
    }

    /// The scale of an image in the pack, as the game would take it.
    fn custom_scale(&self, catalogue: &Catalogue, target: &str) -> Option<u32> {
        let mut pack = Picked::Folder(self.pack());
        let size = png::size(&pack.header(target).ok()?)?;
        png::whole_scale(catalogue.size_of(target)?, size)
    }

    /// The scale every other background of the map has in the pack, leaving out `except`.
    fn map_scale(&self, catalogue: &Catalogue, map: &str, except: &BTreeSet<String>) -> Option<u32> {
        self.custom_targets()
            .iter()
            .filter(|target| map_of(target) == Some(map) && !except.contains(*target))
            .find_map(|target| self.custom_scale(catalogue, target))
    }

    /// Checks one file for an image: a PNG, the original's aspect ratio, 1 to MAX_SCALE times its
    /// size, not the original itself. `bytes` may be only the header unless the size is the same.
    fn check(&self, catalogue: &Catalogue, target: &str, header: &[u8]) -> Checked {
        let original = catalogue.size_of(target).ok_or(Problem::UnknownName)?;
        let size = png::size(header).ok_or(Problem::NotPng)?;
        let scale = png::whole_scale(original, size).ok_or(Problem::WrongSize {
            width: size.width,
            height: size.height,
            expected_width: original.width,
            expected_height: original.height,
        })?;
        if scale > png::MAX_SCALE {
            return Err(Problem::TooLarge { scale });
        }
        Ok(scale)
    }

    fn same_as_original(&self, target: &str, bytes: &[u8]) -> bool {
        fs::read(self.originals().join(target)).is_ok_and(|original| original == bytes)
    }

    /// What a folder or .zip would change: the files matched to images and checked, those
    /// refused and why, those equal to the originals.
    pub fn inspect(&self, catalogue: &Catalogue, picked: &mut Picked) -> Result<BulkSummary> {
        let mut summary = BulkSummary::default();
        let mut taken = BTreeSet::new();
        for name in picked.names()? {
            if !name.to_lowercase().ends_with(PNG_EXTENSION) {
                summary.ignored += 1;
                continue;
            }
            let Some(target) = catalogue.target_of(&name) else {
                summary.rejected.push(Rejected { name, problem: Problem::UnknownName });
                continue;
            };
            if !taken.insert(target.clone()) {
                summary.rejected.push(Rejected { name, problem: Problem::Duplicate });
                continue;
            }
            match self.check(catalogue, &target, &picked.header(&name)?) {
                Ok(1) if self.same_as_original(&target, &picked.read(&name)?) => summary.unchanged += 1,
                Ok(scale) => summary.matched.push(Matched { name, target, scale }),
                Err(problem) => summary.rejected.push(Rejected { name, problem }),
            }
        }
        self.refuse_mixed_scales(catalogue, &mut summary);
        Ok(summary)
    }

    /// A map whose backgrounds would end with two scales keeps none of the new ones.
    fn refuse_mixed_scales(&self, catalogue: &Catalogue, summary: &mut BulkSummary) {
        let replaced: BTreeSet<String> = summary.matched.iter().map(|m| m.target.clone()).collect();
        let mut scales: BTreeMap<String, u32> = BTreeMap::new();
        let mut mixed: BTreeMap<String, (u32, u32)> = BTreeMap::new();
        for matched in &summary.matched {
            let Some(map) = map_of(&matched.target) else { continue };
            let first = *scales
                .entry(map.to_string())
                .or_insert_with(|| self.map_scale(catalogue, map, &replaced).unwrap_or(matched.scale));
            if first != matched.scale {
                mixed.insert(map.to_string(), (first, matched.scale));
            }
        }
        let (kept, refused): (Vec<_>, Vec<_>) = std::mem::take(&mut summary.matched)
            .into_iter()
            .partition(|m| map_of(&m.target).is_none_or(|map| !mixed.contains_key(map)));
        summary.matched = kept;
        for matched in refused {
            let (scale, other) = mixed[map_of(&matched.target).unwrap_or_default()];
            let problem = Problem::MixedScale { scale: matched.scale, other: if matched.scale == scale { other } else { scale } };
            summary.rejected.push(Rejected { name: matched.name, problem });
        }
    }

    /// Inspects a folder or .zip and copies the files it matched into the pack.
    pub fn apply(&self, catalogue: &Catalogue, picked: &mut Picked) -> Result<BulkSummary> {
        let mut summary = self.inspect(catalogue, picked)?;
        for matched in &summary.matched {
            let bytes = picked.read(&matched.name)?;
            self.store(&matched.target, &bytes)?;
        }
        summary.applied = true;
        Ok(summary)
    }

    /// Sets one image from one file, checked as a bulk load checks it.
    pub fn assign(&self, catalogue: &Catalogue, target: &str, file: &Path) -> Result<Checked> {
        if catalogue.size_of(target).is_none() {
            return Ok(Err(Problem::UnknownName));
        }
        let size = fs::metadata(file)?.len();
        if size > picked::MAX_FILE_BYTES {
            return Err(Failure::new(code::IMAGES_TOO_BIG, file.display().to_string()));
        }
        let bytes = fs::read(file)?;
        let scale = match self.check(catalogue, target, &bytes) {
            Ok(1) if self.same_as_original(target, &bytes) => return Ok(Err(Problem::Unchanged)),
            Ok(scale) => scale,
            Err(problem) => return Ok(Err(problem)),
        };
        if let Some(map) = map_of(target) {
            let except = BTreeSet::from([target.to_string()]);
            if let Some(other) = self.map_scale(catalogue, map, &except).filter(|other| *other != scale) {
                return Ok(Err(Problem::MixedScale { scale, other }));
            }
        }
        self.store(target, &bytes)?;
        Ok(Ok(scale))
    }

    /// Puts an image back to the original: its file leaves the pack.
    pub fn reset(&self, catalogue: &Catalogue, target: &str) -> Result<()> {
        if catalogue.size_of(target).is_none() {
            return Err(Failure::new(code::IMAGES_UNKNOWN, target));
        }
        let file = self.pack().join(target);
        if file.is_file() {
            fs::remove_file(file)?;
        }
        Ok(())
    }

    /// Removes every image the player loaded.
    pub fn remove_all(&self) -> Result<()> {
        if self.pack().exists() {
            fs::remove_dir_all(self.pack())?;
        }
        Ok(())
    }

    /// Writes an image into the pack: through a folder outside it, so the pack never holds a half
    /// written file (whose name would be no asset).
    fn store(&self, target: &str, bytes: &[u8]) -> Result<()> {
        let pack = self.pack();
        if !pack.join(MANIFEST_FILE).is_file() {
            crate::store::write_atomically(&pack.join(MANIFEST_FILE), MANIFEST.as_bytes())?;
        }
        let incoming = self.root.join(INCOMING_FOLDER);
        fs::create_dir_all(&incoming)?;
        let temporary = incoming.join(target.replace('/', "__"));
        fs::write(&temporary, bytes)?;
        let destination = pack.join(target);
        if let Some(parent) = destination.parent() {
            fs::create_dir_all(parent)?;
        }
        fs::rename(&temporary, &destination)?;
        Ok(())
    }

    /// Copies the originals into a new folder inside `destination`, with a guide.
    pub fn export(&self, catalogue: &Catalogue, destination: &Path) -> Result<PathBuf> {
        let folder = (1..)
            .map(|n| if n == 1 { destination.join(EXPORT_FOLDER) } else { destination.join(format!("{EXPORT_FOLDER}-{n}")) })
            .find(|folder| !folder.exists())
            .unwrap_or_else(|| destination.join(EXPORT_FOLDER));
        for target in catalogue.images.keys() {
            let to = folder.join(target);
            if let Some(parent) = to.parent() {
                fs::create_dir_all(parent)?;
            }
            fs::copy(self.originals().join(target), to)?;
        }
        fs::copy(self.originals().join(MAPS_FILE), folder.join(EXPORTED_MAPS_FILE))?;
        fs::write(folder.join(README_FILE), README)?;
        Ok(folder)
    }

    /// An image as a data: URL, the original or the player's.
    pub fn thumbnail(&self, catalogue: &Catalogue, target: &str, custom: bool) -> Result<String> {
        if catalogue.size_of(target).is_none() {
            return Err(Failure::new(code::IMAGES_UNKNOWN, target));
        }
        let folder = if custom { self.pack() } else { self.originals() };
        let bytes = fs::read(folder.join(target))?;
        Ok(format!("{PNG_DATA_URL}{}", base64::engine::general_purpose::STANDARD.encode(bytes)))
    }

    pub fn overview(&self, catalogue: &Catalogue, prepared: bool) -> Overview {
        let custom = self.custom_targets();
        let row = |target: &str, variant: String| {
            let size = catalogue.images[target];
            let is_custom = custom.contains(target);
            FileRow {
                target: target.to_string(),
                variant,
                width: size.width,
                height: size.height,
                custom: is_custom,
                custom_scale: if is_custom { self.custom_scale(catalogue, target) } else { None },
            }
        };
        let mut maps: BTreeMap<String, Vec<FileRow>> = BTreeMap::new();
        let mut images: BTreeMap<String, Vec<FileRow>> = BTreeMap::new();
        for target in catalogue.images.keys() {
            let stem = &target[..target.len() - PNG_EXTENSION.len()];
            if let Some(map) = map_of(target) {
                let variant = stem.rsplit('/').next().unwrap_or_default();
                let variant = variant.strip_prefix(BACKGROUND_PREFIX).unwrap_or(variant).to_string();
                maps.entry(map.to_string()).or_default().push(row(target, variant));
            } else if let Some((image, palette)) = stem.rsplit_once(PALETTE_SUFFIX) {
                images.entry(image.to_string()).or_default().push(row(target, palette.to_string()));
            }
        }
        let mut groups: BTreeMap<(u8, String), Group> = BTreeMap::new();
        for (map, mut files) in maps {
            files.sort_by_key(|file| variant_rank(&file.variant));
            let key = map_prefix(&map);
            let name = catalogue.names.get(&map).cloned().unwrap_or_default();
            groups
                .entry((0, key.clone()))
                .or_insert_with(|| Group { key, kind: "maps", name: String::new(), entries: Vec::new() })
                .entries
                .push(Entry { id: map, name, files });
        }
        for (image, mut files) in images {
            files.sort_by_key(|file| file.variant.parse::<u32>().unwrap_or(u32::MAX));
            let key = image.rsplit_once('/').map_or(image.as_str(), |(folder, _)| folder).to_string();
            groups
                .entry((1, key.clone()))
                .or_insert_with(|| Group { key, kind: "images", name: String::new(), entries: Vec::new() })
                .entries
                .push(Entry { id: image, name: String::new(), files });
        }
        let mut groups: Vec<Group> = groups.into_values().collect();
        for group in groups.iter_mut().filter(|group| group.kind == "maps") {
            group.name = most_common(group.entries.iter().map(|entry| entry.name.as_str()));
        }
        Overview { prepared, enabled: self.enabled(), custom_count: custom.len(), groups }
    }
}

fn variant_rank(variant: &str) -> (usize, String) {
    let rank = VARIANT_ORDER.iter().position(|known| *known == variant).unwrap_or(VARIANT_ORDER.len());
    (rank, variant.to_string())
}

/// The letters a map id starts with (mayo01 → mayo), or the whole id when it starts otherwise.
fn map_prefix(map: &str) -> String {
    let letters: String = map.chars().take_while(char::is_ascii_lowercase).collect();
    if letters.is_empty() { map.to_string() } else { letters }
}

/// The name most maps share; on a tie, the first map's.
fn most_common<'a>(names: impl Iterator<Item = &'a str>) -> String {
    let mut counts: Vec<(&str, usize)> = Vec::new();
    for name in names.filter(|name| !name.is_empty()) {
        match counts.iter_mut().find(|(known, _)| *known == name) {
            Some((_, count)) => *count += 1,
            None => counts.push((name, 1)),
        }
    }
    let most = counts.iter().map(|(_, count)| *count).max().unwrap_or_default();
    counts.into_iter().find(|(_, count)| *count == most).map(|(name, _)| name.to_string()).unwrap_or_default()
}

#[cfg(test)]
mod tests;
