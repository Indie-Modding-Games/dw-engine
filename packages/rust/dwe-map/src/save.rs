//! Saving an edited map into one pack of the stack (docs/modulos/apps/mapper.md, "Guardar"):
//! what to write so that the game, loading the map with every pack's patches as
//! [`crate::load_map`] does, sees the edit.
//!
//! The edit is what changed between the map as the stack gave it and the map edited. It is put
//! over the target pack's own level of the map (its data, or the data below it with the patches
//! up to and including its own), so the fields the edit did not touch keep what the target pack
//! said. The target then gets the patch from the level below it (its "floor") to that result, or
//! the whole result as its data. Patches of packs above the target still apply over it.

use std::path::{Path, PathBuf};

use dwe_pack::{MountedPack, Vfs};

use crate::diff::{diff_patch, patch_text};
use crate::document::MapDocument;
use crate::load::{map_data_id, map_folder};
use crate::patch::{MapPatchText, check_map_patch, patch_map_document, read_map_patches};
use crate::table::MapTable;
use crate::triggers::TriggerBands;
use crate::{Error, Result, WarpTarget, warp_target};

/// The file name of a map's patch and of its data in a pack folder.
pub const PATCH_FILE: &str = "patch.yaml";
pub const DATA_FILE: &str = "data.yaml";

/// How a map is saved.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SaveMode {
    /// `map/<id>/patch.yaml` with only the differences from the level below the target.
    Patch,
    /// The whole `map/<id>/data.yaml` (the target's own patch, if any, goes away).
    WholeData,
}

/// A file a save writes, or removes when `text` is None.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FileChange {
    pub path: PathBuf,
    pub text: Option<String>,
}

/// Something a save found. Errors stop it; warnings do not.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Problem {
    /// The target pack is below the pack whose data wins: its patch or data would not apply.
    TargetBelowData { data_pack: String },
    /// The map has no data in any pack.
    NoData,
    /// The written patch or data does not load (the engine's message).
    DoesNotLoad(String),
    /// An exit's id and raw index name different maps: the game disables it.
    ExitConflict { exit: i32 },
    /// An exit's id names no map of the table.
    UnknownMap { exit: i32, id: String },
    /// An exit with trigger cells leads nowhere (a dead warp).
    DeadExit { exit: i32 },
    /// An exit leads to a map but not to one of its exits (0-9).
    BadTargetExit { exit: i32, target_exit: i32 },
    /// An NPC's path does not fit the record (8 parts, at most 8 points).
    BadPath { slot: usize },
    /// A pack above the target changes something the edit changed, so the game will not show
    /// the edit there.
    Overridden { packs: Vec<String> },
}

impl Problem {
    pub fn is_error(&self) -> bool {
        !matches!(self, Self::Overridden { .. })
    }
}

/// What a save writes and what it found.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct SavePlan {
    pub changes: Vec<FileChange>,
    pub problems: Vec<Problem>,
    /// The map as the game will load it once the files are written.
    pub result: Option<MapDocument>,
}

impl SavePlan {
    pub fn has_errors(&self) -> bool {
        self.problems.iter().any(Problem::is_error)
    }
}

/// The parts of a map's stack seen from a target pack.
struct Levels {
    data: String,
    /// Whether the target pack's own data is the data that wins.
    target_has_data: bool,
    below: Vec<MapPatchText>,
    own: Option<MapPatchText>,
    above: Vec<MapPatchText>,
}

fn levels(vfs: &Vfs, map_id: &str, target: &str) -> std::result::Result<Levels, Problem> {
    let data_id = map_data_id(map_id).map_err(|error| Problem::DoesNotLoad(error.to_string()))?;
    let Some((data_pack, _)) = vfs.resolve(&data_id) else {
        return Err(Problem::NoData);
    };
    let place = |id: &str| vfs.packs().iter().position(|pack| pack.id() == id);
    if place(target) < place(data_pack.id()) {
        return Err(Problem::TargetBelowData {
            data_pack: data_pack.id().to_owned(),
        });
    }
    let data = vfs
        .read_text(&data_id)
        .map_err(|error| Problem::DoesNotLoad(error.to_string()))?;
    let patches =
        read_map_patches(vfs, map_id).map_err(|error| Problem::DoesNotLoad(error.to_string()))?;
    let target_place = place(target);
    let mut levels = Levels {
        data,
        target_has_data: data_pack.id() == target,
        below: Vec::new(),
        own: None,
        above: Vec::new(),
    };
    for patch in patches {
        match place(&patch.pack).cmp(&target_place) {
            std::cmp::Ordering::Less => levels.below.push(patch),
            std::cmp::Ordering::Equal => levels.own = Some(patch),
            std::cmp::Ordering::Greater => levels.above.push(patch),
        }
    }
    Ok(levels)
}

fn apply(map_id: &str, data: &str, patches: &[MapPatchText]) -> Result<MapDocument> {
    if patches.is_empty() {
        MapDocument::parse(map_id, data)
    } else {
        patch_map_document(map_id, data, patches)
    }
}

/// The edit, as a patch the mapper puts over the target's level.
fn edit_patch(loaded: &MapDocument, edited: &MapDocument) -> Option<MapPatchText> {
    diff_patch(loaded, edited).map(|patch| MapPatchText {
        pack: "(edit)".to_owned(),
        base_rights: false,
        file: "(edit)".to_owned(),
        text: patch_text(&patch),
    })
}

/// Plans the save of an edited map into a pack: `loaded` is the map as the stack gave it when it
/// was opened, `edited` the same map edited. Nothing is written; [`write_changes`] writes.
pub fn plan_save(
    vfs: &Vfs,
    table: &MapTable,
    bands: &TriggerBands,
    target: &MountedPack,
    loaded: &MapDocument,
    edited: &MapDocument,
    mode: SaveMode,
) -> SavePlan {
    let mut plan = SavePlan::default();
    let map_id = &edited.id;
    let levels = match levels(vfs, map_id, target.id()) {
        Ok(levels) => levels,
        Err(problem) => {
            plan.problems.push(problem);
            return plan;
        }
    };
    let failed = |error: Error| Problem::DoesNotLoad(error.to_string());
    let result = (|| -> std::result::Result<_, Problem> {
        // The level below the target, and the target's own level.
        let floor_patches: &[MapPatchText] = if levels.target_has_data {
            &[]
        } else {
            &levels.below
        };
        let floor = apply(map_id, &levels.data, floor_patches).map_err(failed)?;
        let own: Vec<MapPatchText> = levels.own.iter().cloned().collect();
        let mut level = apply(map_id, &floor.to_yaml(), &own).map_err(failed)?;
        if let Some(edit) = edit_patch(loaded, edited) {
            level = patch_map_document(map_id, &level.to_yaml(), &[edit]).map_err(failed)?;
        }
        let folder = target.root.join(map_folder(map_id));
        let patch_path = folder.join(PATCH_FILE);
        let data_path = folder.join(DATA_FILE);
        let (data, patch) = match mode {
            SaveMode::Patch => {
                let patch = diff_patch(&floor, &level).map(|patch| patch_text(&patch));
                if let Some(text) = &patch {
                    check_map_patch(text).map_err(failed)?;
                }
                if patch.is_some() || patch_path.exists() {
                    plan.changes.push(FileChange {
                        path: patch_path.clone(),
                        text: patch.clone(),
                    });
                }
                (levels.data.clone(), patch)
            }
            SaveMode::WholeData => {
                let text = level.to_yaml();
                plan.changes.push(FileChange {
                    path: data_path,
                    text: Some(text.clone()),
                });
                if patch_path.exists() {
                    plan.changes.push(FileChange {
                        path: patch_path.clone(),
                        text: None,
                    });
                }
                (text, None)
            }
        };
        // The map as the game will load it: the patches below drop out under a target's data.
        let mut stack: Vec<MapPatchText> = Vec::new();
        if !levels.target_has_data && mode == SaveMode::Patch {
            stack.extend(levels.below.iter().cloned());
        }
        if let Some(text) = patch {
            stack.push(MapPatchText {
                pack: target.id().to_owned(),
                base_rights: target.has_base_rights(),
                file: patch_path.display().to_string(),
                text,
            });
        }
        stack.extend(levels.above.iter().cloned());
        apply(map_id, &data, &stack).map_err(failed)
    })();
    match result {
        Ok(result) => {
            plan.problems
                .extend(check_edit(table, bands, loaded, &result));
            if result.to_tree() != edited.to_tree() {
                plan.problems.push(Problem::Overridden {
                    packs: levels
                        .above
                        .iter()
                        .map(|patch| patch.pack.clone())
                        .collect(),
                });
            }
            plan.result = Some(result);
        }
        Err(problem) => plan.problems.push(problem),
    }
    plan
}

/// Checks what an edit touched, as `dwx validate` and the game check a mod's map: the exits whose
/// fields or trigger cells changed lead somewhere real, and the NPCs' paths fit their record.
/// What the edit did not touch is not checked (the original has exits that lead nowhere).
pub fn check_edit(
    table: &MapTable,
    bands: &TriggerBands,
    before: &MapDocument,
    after: &MapDocument,
) -> Vec<Problem> {
    let mut problems = Vec::new();
    let trigger_cells = |map: &MapDocument, exit: i32| {
        let Some(value) = u8::try_from(exit)
            .ok()
            .and_then(|exit| bands.exit_trigger(exit))
        else {
            return 0;
        };
        map.collision
            .values()
            .iter()
            .filter(|&&cell| cell == value)
            .count()
    };
    for warp in &after.warps {
        let touched = before.warp(warp.exit) != Some(warp)
            || trigger_cells(before, warp.exit) != trigger_cells(after, warp.exit);
        if !touched {
            continue;
        }
        if let Some(id) = &warp.target_map_id
            && table.find(id).is_none()
        {
            problems.push(Problem::UnknownMap {
                exit: warp.exit,
                id: id.clone(),
            });
            continue;
        }
        match warp_target(table, warp) {
            WarpTarget::Conflict => problems.push(Problem::ExitConflict { exit: warp.exit }),
            WarpTarget::Nowhere | WarpTarget::UnknownIdFallback(_) => {
                if trigger_cells(after, warp.exit) > 0 {
                    problems.push(Problem::DeadExit { exit: warp.exit });
                }
            }
            WarpTarget::Map(_) => {
                if !(0..crate::triggers::MOST_EXITS).contains(&warp.target_exit) {
                    problems.push(Problem::BadTargetExit {
                        exit: warp.exit,
                        target_exit: warp.target_exit,
                    });
                }
            }
        }
    }
    for (slot, npc) in after.npcs.iter().enumerate() {
        if before.npcs.get(slot) == Some(npc) {
            continue;
        }
        if npc.ai_sections.len() != NPC_PATH_PARTS || npc.waypoints.len() > NPC_PATH_PARTS {
            problems.push(Problem::BadPath { slot });
        }
    }
    problems
}

/// The parts of an NPC's path (`ai_sections`), and the most points it has.
pub const NPC_PATH_PARTS: usize = 8;

/// Writes the files of a plan, each atomically (a temporary file next to it, then a rename), and
/// removes the ones it drops.
pub fn write_changes(changes: &[FileChange]) -> std::io::Result<()> {
    for change in changes {
        match &change.text {
            Some(text) => write_atomically(&change.path, text)?,
            None => match std::fs::remove_file(&change.path) {
                Err(error) if error.kind() != std::io::ErrorKind::NotFound => return Err(error),
                _ => {}
            },
        }
    }
    Ok(())
}

fn write_atomically(path: &Path, text: &str) -> std::io::Result<()> {
    if let Some(folder) = path.parent() {
        std::fs::create_dir_all(folder)?;
    }
    let mut temporary = path.as_os_str().to_owned();
    temporary.push(".tmp");
    let temporary = PathBuf::from(temporary);
    std::fs::write(&temporary, text)?;
    std::fs::rename(&temporary, path)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::patch::tests::{Packs, fixture_map, stacked};
    use crate::world::Cell;

    fn table() -> MapTable {
        let value = serde_yaml_ng::from_str(
            "maps:\n- {id: testa01, index: 0}\n- {id: room08, index: 118}\n- {id: room09, index: 119}\n",
        )
        .unwrap();
        MapTable::from_value(value, &Default::default()).unwrap()
    }

    fn pack<'a>(vfs: &'a Vfs, id: &str) -> &'a MountedPack {
        vfs.packs().iter().find(|pack| pack.id() == id).unwrap()
    }

    fn plan_for(
        vfs: &Vfs,
        target: &str,
        edit: impl Fn(&mut MapDocument),
        mode: SaveMode,
    ) -> SavePlan {
        let loaded = crate::load_map(vfs, "testa01").unwrap();
        let mut edited = loaded.clone();
        edit(&mut edited);
        plan_save(
            vfs,
            &table(),
            &TriggerBands::default(),
            pack(vfs, target),
            &loaded,
            &edited,
            mode,
        )
    }

    fn text_of(plan: &SavePlan, file: &str) -> Option<String> {
        plan.changes
            .iter()
            .find(|change| change.path.ends_with(file))
            .and_then(|change| change.text.clone())
    }

    #[test]
    fn a_patch_in_the_top_pack_keeps_its_own_changes_and_adds_the_edit() {
        let packs = stacked();
        let vfs = packs.mount(&["base", "low", "high"]);
        let plan = plan_for(
            &vfs,
            "high",
            |map| {
                map.warps[0].target_exit = 3;
                map.collision.set(Cell::new(30, 40), 128);
            },
            SaveMode::Patch,
        );
        assert!(!plan.has_errors(), "{:?}", plan.problems);
        assert!(plan.problems.is_empty(), "{:?}", plan.problems);
        let text = text_of(&plan, "high/map/testa01/patch.yaml").unwrap();
        assert_eq!(
            text,
            "camera:\n  projection: 1234\nwarps:\n- exit: 0\n  target_exit: 3\ncollision:\n  cells:\n  \
             - column: 30\n    row: 40\n    value: 128\n"
        );
        write_changes(&plan.changes).unwrap();
        let again = crate::load_map(&packs.mount(&["base", "low", "high"]), "testa01").unwrap();
        assert_eq!(again.warps[0].target_exit, 3);
        assert_eq!(again.collision.value(Cell::new(30, 40)), 128);
        assert_eq!(again.camera.projection, 1234);
        assert_eq!(Some(again), plan.result);
    }

    #[test]
    fn a_pack_above_the_target_wins_and_the_save_says_so() {
        let packs = stacked();
        let vfs = packs.mount(&["base", "low", "high"]);
        let plan = plan_for(
            &vfs,
            "low",
            |map| {
                map.camera.projection = 1500;
                map.warps[0].target_exit = 1;
            },
            SaveMode::Patch,
        );
        assert!(!plan.has_errors());
        assert_eq!(
            plan.problems,
            [Problem::Overridden {
                packs: vec!["high".to_owned()]
            }]
        );
        let text = text_of(&plan, "low/map/testa01/patch.yaml").unwrap();
        assert_eq!(
            text,
            "camera:\n  projection: 1500\nwarps:\n- exit: 0\n  target_exit: 1\n"
        );
    }

    #[test]
    fn undoing_a_patch_removes_its_file() {
        let packs = stacked();
        let vfs = packs.mount(&["base", "low", "high"]);
        let plan = plan_for(
            &vfs,
            "low",
            |map| map.warps[0].target_exit = 2,
            SaveMode::Patch,
        );
        assert_eq!(plan.changes.len(), 1);
        assert_eq!(plan.changes[0].text, None);
        write_changes(&plan.changes).unwrap();
        assert!(!packs.dir.path().join("low/map/testa01/patch.yaml").exists());
        // Nothing edited in a pack without a patch: nothing to write.
        let packs = Packs::new();
        packs.pack("base", "base", 0);
        packs.file("base/map/testa01/data.yaml", &fixture_map("null"));
        packs.pack("mine", "mods", 0);
        let vfs = packs.mount(&["base", "mine"]);
        let plan = plan_for(&vfs, "mine", |_| {}, SaveMode::Patch);
        assert!(plan.changes.is_empty());
        assert!(plan.problems.is_empty());
    }

    #[test]
    fn a_whole_save_writes_the_data_and_drops_the_own_patch() {
        let packs = stacked();
        let vfs = packs.mount(&["base", "low", "high"]);
        let plan = plan_for(
            &vfs,
            "high",
            |map| map.warps[0].rotation = 1024,
            SaveMode::WholeData,
        );
        assert!(plan.problems.is_empty(), "{:?}", plan.problems);
        let data = text_of(&plan, "high/map/testa01/data.yaml").unwrap();
        let written = MapDocument::parse("testa01", &data).unwrap();
        assert_eq!(written.warps[0].rotation, 1024);
        assert_eq!(written.warps[0].target_exit, 7);
        assert_eq!(written.camera.projection, 1234);
        assert!(plan.changes.iter().any(|change| {
            change.path.ends_with("high/map/testa01/patch.yaml") && change.text.is_none()
        }));
        write_changes(&plan.changes).unwrap();
        let again = crate::load_map(&packs.mount(&["base", "low", "high"]), "testa01").unwrap();
        assert_eq!(again.patches, Vec::<String>::new());
        assert_eq!(again.warps[0].rotation, 1024);
    }

    #[test]
    fn a_target_below_the_data_is_refused() {
        let packs = stacked();
        packs.file("high/map/testa01/data.yaml", &fixture_map("null"));
        let vfs = packs.mount(&["base", "low", "high"]);
        let plan = plan_for(
            &vfs,
            "low",
            |map| map.warps[0].target_exit = 1,
            SaveMode::Patch,
        );
        assert_eq!(
            plan.problems,
            [Problem::TargetBelowData {
                data_pack: "high".to_owned()
            }]
        );
        assert!(plan.changes.is_empty());
    }

    #[test]
    fn touched_exits_must_lead_somewhere() {
        let packs = stacked();
        let vfs = packs.mount(&["base", "low", "high"]);
        let bands = TriggerBands::default();
        let errors = |edit: &dyn Fn(&mut MapDocument)| {
            plan_for(&vfs, "high", edit, SaveMode::Patch).problems
        };
        // Exit 1 leads nowhere: painting its cells makes a dead warp.
        let value = bands.exit_trigger(1).unwrap();
        assert_eq!(
            errors(&|map| map.collision.set(Cell::new(40, 40), value)),
            [Problem::DeadExit { exit: 1 }]
        );
        assert_eq!(
            errors(&|map| map.warps[1].target_map_id = Some("nowhere".to_owned())),
            [Problem::UnknownMap {
                exit: 1,
                id: "nowhere".to_owned()
            }]
        );
        assert_eq!(
            errors(&|map| map.warps[0].target_map_id = Some("room09".to_owned())),
            [Problem::ExitConflict { exit: 0 }]
        );
        assert_eq!(
            errors(&|map| {
                map.warps[1].target_map_id = Some("room09".to_owned());
                map.warps[1].target_exit = 12;
            }),
            [Problem::BadTargetExit {
                exit: 1,
                target_exit: 12
            }]
        );
        // A pack's map, by id only, is fine.
        assert!(
            errors(&|map| {
                map.warps[1].target_map_id = Some("room09".to_owned());
                map.warps[1].target_exit = 0;
                map.collision.set(Cell::new(40, 40), value);
            })
            .is_empty()
        );
    }
}
