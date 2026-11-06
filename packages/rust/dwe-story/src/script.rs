//! A script as the game runs it: its module (`story/<nnn>`, the file that wins) and the story
//! patches of the stack, applied like `StoryLuaHost` does (packages/session/src/story_lua.cpp:
//! patches from the bottom of the stack up and by id within a pack; the last one that gives a
//! section wins it; `original` is always the module's section).

use std::path::PathBuf;

use dwe_pack::{AssetId, Vfs};

use crate::ast::Block;
use crate::file::StoryFile;
use crate::{Error, Result};

/// The folder of the story patches.
pub const PATCH_FOLDER: &str = "story/patches/";

/// The module id of a script: `story/001`.
pub fn module_id(script: u16) -> String {
    format!("story/{script:03}")
}

/// Where a story file comes from.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StoryFileSource {
    pub pack: String,
    pub pack_root: PathBuf,
    pub id: AssetId,
    pub file: PathBuf,
}

impl StoryFileSource {
    /// How the game's log names it: `pack:story/patches/name`.
    pub fn name(&self) -> String {
        format!("{}:{}", self.pack, self.id)
    }
}

/// Every story patch of the stack, read once.
#[derive(Clone, Debug, Default)]
pub struct StoryPatches {
    /// Bottom of the stack first, by id within a pack.
    pub patches: Vec<(StoryFileSource, StoryFile)>,
    /// The patches that do not read (the game leaves them out too).
    pub problems: Vec<String>,
}

impl StoryPatches {
    pub fn load(vfs: &Vfs) -> Self {
        let mut found: Vec<(usize, StoryFileSource)> = Vec::new();
        for id in vfs.ids_with_prefix(PATCH_FOLDER) {
            let Some((pack, file)) = vfs.resolve(id) else {
                continue;
            };
            let index = vfs
                .packs()
                .iter()
                .position(|mounted| mounted.id() == pack.id())
                .unwrap_or(0);
            found.push((
                index,
                StoryFileSource {
                    pack: pack.id().to_owned(),
                    pack_root: pack.root.clone(),
                    id: id.clone(),
                    file: file.to_path_buf(),
                },
            ));
        }
        found.sort_by_key(|(index, _)| *index);
        let mut patches = Self::default();
        for (_, source) in found {
            match read_file(&source) {
                Ok(file) => patches.patches.push((source, file)),
                Err(error) => patches
                    .problems
                    .push(format!("{}: {error}", source.file.display())),
            }
        }
        patches
    }

    /// The patches of one script, bottom first.
    pub fn of_script(&self, script: u16) -> impl Iterator<Item = &(StoryFileSource, StoryFile)> {
        self.patches.iter().filter(move |(_, file)| file.script == script)
    }
}

fn read_file(source: &StoryFileSource) -> Result<StoryFile> {
    let text = std::fs::read_to_string(&source.file)
        .map_err(|error| Error::Invalid(format!("{}: {error}", source.file.display())))?;
    StoryFile::parse(&text)
}

/// Who gives a section.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SectionOrigin {
    Module,
    /// The patch at this position of [`LoadedScript::patches`].
    Patch(usize),
}

/// A script with its module and its patches.
#[derive(Clone, Debug, Default)]
pub struct LoadedScript {
    pub script: u16,
    pub module: Option<(StoryFileSource, StoryFile)>,
    pub patches: Vec<(StoryFileSource, StoryFile)>,
    /// What did not read.
    pub problems: Vec<String>,
}

impl LoadedScript {
    pub fn load(vfs: &Vfs, patches: &StoryPatches, script: u16) -> Self {
        let mut loaded = Self {
            script,
            patches: patches.of_script(script).cloned().collect(),
            ..Self::default()
        };
        let id = AssetId::parse(&module_id(script)).expect("a valid id");
        if let Some((pack, file)) = vfs.resolve(&id) {
            let source = StoryFileSource {
                pack: pack.id().to_owned(),
                pack_root: pack.root.clone(),
                id,
                file: file.to_path_buf(),
            };
            match read_file(&source) {
                Ok(module) if module.script == script => loaded.module = Some((source, module)),
                Ok(module) => loaded.problems.push(format!(
                    "{}: returns script {}, not {script}",
                    source.file.display(),
                    module.script
                )),
                Err(error) => loaded
                    .problems
                    .push(format!("{}: {error}", source.file.display())),
            }
        }
        loaded
    }

    /// Who runs a section: the top patch that gives it, or the module.
    pub fn origin(&self, section: u16) -> Option<SectionOrigin> {
        if let Some(index) = self
            .patches
            .iter()
            .rposition(|(_, patch)| patch.section(section).is_some())
        {
            return Some(SectionOrigin::Patch(index));
        }
        self.module
            .as_ref()
            .and_then(|(_, module)| module.section(section))
            .map(|_| SectionOrigin::Module)
    }

    pub fn file(&self, origin: SectionOrigin) -> Option<&(StoryFileSource, StoryFile)> {
        match origin {
            SectionOrigin::Module => self.module.as_ref(),
            SectionOrigin::Patch(index) => self.patches.get(index),
        }
    }

    /// The file that runs a section and the section's code.
    pub fn section(&self, section: u16) -> Option<(&StoryFileSource, &StoryFile, &Block)> {
        let (source, file) = self.file(self.origin(section)?)?;
        Some((source, file, file.section_block(section)?))
    }

    /// The patch that runs a section, as the game's log names it.
    pub fn patched_by(&self, section: u16) -> Option<String> {
        match self.origin(section)? {
            SectionOrigin::Patch(index) => Some(self.patches[index].0.name()),
            SectionOrigin::Module => None,
        }
    }

    /// Every section: the module's in its order, then those only patches add.
    pub fn section_ids(&self) -> Vec<u16> {
        let mut ids: Vec<u16> = self
            .module
            .iter()
            .flat_map(|(_, module)| module.sections.iter().map(|s| s.id))
            .collect();
        for (_, patch) in &self.patches {
            for section in &patch.sections {
                if !ids.contains(&section.id) {
                    ids.push(section.id);
                }
            }
        }
        ids
    }
}
