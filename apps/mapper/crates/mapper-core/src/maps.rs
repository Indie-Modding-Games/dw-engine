//! The session's side of the world graph and of the maps a pack adds (docs/modulos/apps/mapper.md,
//! "Mundo" and "Nuevo mapa"): building the graph when it is looked at, the places the user gave
//! maps, connecting exits from the graph, and creating, renaming and deleting a pack's own maps.

use std::collections::BTreeMap;

use dwe_map::Warp;
use dwe_map::new_map::{self, NewMap};
use dwe_map::save::{SaveMode, plan_save, write_changes};
use dwe_pack::MountedPack;
use glam::Vec2;

use crate::edit::{CellChange, Edit};
use crate::session::{Mapper, Message};
use crate::text;
use crate::world::{WorldGraph, WorldSource};

/// An exit that should lead to an exit of another map, and maybe back.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Connect {
    pub from: String,
    pub exit: i32,
    pub to: String,
    pub to_exit: i32,
    /// Whether `to_exit` of `to` should lead back to `exit` of `from`.
    pub back: bool,
}

impl Mapper {
    /// The key of the content's places in the settings: its folder.
    pub fn world_key(&self) -> Option<String> {
        let content = self.content.as_ref()?;
        Some(content.folders.content.display().to_string())
    }

    /// The places the user gave maps in this content.
    pub fn pinned(&self) -> BTreeMap<String, [i32; 2]> {
        self.world_key()
            .and_then(|key| self.settings.world_layouts.get(&key).cloned())
            .unwrap_or_default()
    }

    /// Builds the graph and its layout when the content, a save or the open map changed since
    /// the last time. Returns whether it did.
    pub fn refresh_world(&mut self) -> bool {
        let open = self.map.as_ref().map(|map| map.document.clone());
        if self.world.built_with.as_ref() == Some(&open) {
            return false;
        }
        let Some(content) = &self.content else {
            self.world.graph = WorldGraph::default();
            self.world.layout.clear();
            self.world.built_with = Some(open);
            return true;
        };
        let patches = self.shown_patches();
        let vfs = content.vfs();
        let source = WorldSource {
            vfs,
            table: &content.table,
            bands: &self.bands,
            open: open.as_ref(),
        };
        let story = &mut self.story;
        let mut scripts = BTreeMap::new();
        let graph = WorldGraph::build(&source, |script| {
            scripts
                .entry(script)
                .or_insert_with(|| story.loaded(vfs, script, &patches))
                .clone()
        });
        self.world.layout = graph.layout(&self.pinned());
        self.world.graph = graph;
        self.world.built_with = Some(open);
        true
    }

    /// Puts a map of the graph somewhere and remembers it.
    pub(crate) fn move_world_node(&mut self, id: &str, to: Vec2) {
        let Some(key) = self.world_key() else { return };
        if let Some(place) = self.world.layout.get_mut(id) {
            *place = to;
        }
        self.settings
            .world_layouts
            .entry(key)
            .or_default()
            .insert(id.to_owned(), [to.x.round() as i32, to.y.round() as i32]);
        self.store_settings();
    }

    /// Forgets the places the user gave and lays the graph out again.
    pub(crate) fn reset_world_layout(&mut self) {
        if let Some(key) = self.world_key() {
            self.settings.world_layouts.remove(&key);
            self.store_settings();
        }
        self.world.layout = self.world.graph.layout(&BTreeMap::new());
    }

    /// The target's pack as mounted.
    fn target_pack(&self) -> Option<&MountedPack> {
        let target = self.current_target()?;
        self.content
            .as_ref()?
            .vfs()
            .packs()
            .iter()
            .find(|pack| pack.root == target.root)
    }

    /// The maps the target pack added itself.
    pub fn own_maps(&self) -> Vec<String> {
        self.target_pack()
            .map(new_map::own_maps)
            .unwrap_or_default()
    }

    /// An exit of `map` as it would lead to `to_exit` of `to`.
    fn retargeted(
        &self,
        map: &dwe_map::MapDocument,
        exit: i32,
        to: &str,
        to_exit: i32,
    ) -> Option<Warp> {
        let content = self.content.as_ref()?;
        let entry = content.table.find(to)?;
        let mut warp = map.warp(exit)?.clone();
        warp.target_map_id = Some(to.to_owned());
        warp.target_map = if entry.original_index {
            entry.index
        } else {
            -1
        };
        warp.target_exit = to_exit;
        Some(warp)
    }

    /// Makes an exit lead to another map's exit: on the open map an edit to save, on any other a
    /// patch written into the target now. Returns whether a file was written.
    fn set_exit_of(
        &mut self,
        map: &str,
        exit: i32,
        to: &str,
        to_exit: i32,
    ) -> Result<bool, String> {
        if let Some(open) = self.map.as_ref().filter(|open| open.document.id == map) {
            let warp = self
                .retargeted(&open.document, exit, to, to_exit)
                .ok_or_else(|| format!("{map}:{exit} → {to}:{to_exit}: {}", text::NO_SUCH_EXIT))?;
            let mut edited = open.document.clone();
            edited.set_warp(warp);
            new_map::place_door(&mut edited, &self.bands, exit)
                .map_err(|error| error.to_string())?;
            let doors: Vec<CellChange> = edited
                .collision
                .cells()
                .filter_map(|(cell, after)| {
                    let before = open.document.collision.value(cell);
                    (before != after).then_some(CellChange {
                        cell,
                        before,
                        after,
                    })
                })
                .collect();
            let warp = edited.warp(exit).cloned();
            let grouped = open.history.is_grouping();
            if !grouped {
                self.apply(crate::Action::BeginEdit);
            }
            if let Some(warp) = warp {
                self.apply(crate::Action::SetWarp(warp));
            }
            self.record(Edit::Cells(doors));
            if !grouped {
                self.apply(crate::Action::EndEdit);
            }
            self.refresh_story();
            return Ok(false);
        }
        let Some(content) = &self.content else {
            return Err(text::NO_CONTENT.to_owned());
        };
        let pack = self
            .target_pack()
            .ok_or_else(|| text::NO_TARGET.to_owned())?;
        let loaded = dwe_map::load_map(content.vfs(), map).map_err(|error| error.to_string())?;
        let mut edited = loaded.clone();
        let warp = self
            .retargeted(&loaded, exit, to, to_exit)
            .ok_or_else(|| format!("{map}:{exit} → {to}:{to_exit}: {}", text::NO_SUCH_EXIT))?;
        edited.set_warp(warp);
        new_map::place_door(&mut edited, &self.bands, exit).map_err(|error| error.to_string())?;
        let plan = plan_save(
            content.vfs(),
            &content.table,
            &self.bands,
            pack,
            &loaded,
            &edited,
            SaveMode::Patch,
        );
        if plan.has_errors() {
            let problems: Vec<String> = plan
                .problems
                .iter()
                .filter(|problem| problem.is_error())
                .map(text::problem)
                .collect();
            return Err(format!("{map}: {}", problems.join("; ")));
        }
        write_changes(&plan.changes).map_err(|error| error.to_string())?;
        Ok(true)
    }

    /// Connects exits from the graph, one way or both.
    pub(crate) fn connect(&mut self, connect: &Connect) -> bool {
        let mut sides = vec![(
            connect.from.as_str(),
            connect.exit,
            connect.to.as_str(),
            connect.to_exit,
        )];
        if connect.back {
            sides.push((
                connect.to.as_str(),
                connect.to_exit,
                connect.from.as_str(),
                connect.exit,
            ));
        }
        let mut written = false;
        for (map, exit, to, to_exit) in sides {
            match self.set_exit_of(map, exit, to, to_exit) {
                Ok(wrote) => written |= wrote,
                Err(error) => {
                    self.messages.push(Message::Error(error));
                    if written {
                        self.after_files_written();
                    }
                    return false;
                }
            }
        }
        if written {
            self.after_files_written();
        }
        self.world.invalidate();
        self.messages.push(Message::Info(format!(
            "{} {}:{} → {}:{}",
            text::CONNECTED,
            connect.from,
            connect.exit,
            connect.to,
            connect.to_exit
        )));
        true
    }

    /// The content read again after files were written outside the open map's save.
    fn after_files_written(&mut self) {
        self.saves += 1;
        self.reload_content(true);
    }

    /// Creates a map in the target pack, connects it when asked and opens it.
    pub(crate) fn create_map(&mut self, new: &NewMap) -> bool {
        let (Some(content), Some(pack)) = (&self.content, self.target_pack()) else {
            self.messages
                .push(Message::Error(text::NO_TARGET.to_owned()));
            return false;
        };
        let plan =
            match new_map::plan_new_map(content.vfs(), &content.table, &self.bands, pack, new) {
                Ok(plan) => plan,
                Err(error) => {
                    self.messages
                        .push(Message::Error(format!("{}: {error}", text::NEW_MAP_FAILED)));
                    return false;
                }
            };
        if let Err(error) = new_map::apply_ops(&plan.ops) {
            self.messages
                .push(Message::Error(format!("{}: {error}", text::NEW_MAP_FAILED)));
            return false;
        }
        self.after_files_written();
        self.messages.push(Message::Info(format!(
            "{}: {}",
            text::NEW_MAP_CREATED,
            plan.id
        )));
        if let Some(connection) = &new.connect {
            self.connect(&crate::maps::Connect {
                from: connection.map.clone(),
                exit: connection.exit,
                to: plan.id.clone(),
                to_exit: connection.own_exit,
                back: false,
            });
        }
        self.apply(crate::Action::OpenMap(plan.id));
        true
    }

    /// The ways into a map from maps whose files the target cannot change: an exit of a map the
    /// pack neither added nor patches, or any script warp.
    fn outside_references(&mut self, id: &str) -> Vec<String> {
        self.refresh_world();
        let pack = self.target_pack();
        self.world
            .graph
            .references_to(id)
            .into_iter()
            .filter(|edge| {
                let Some(pack) = pack else { return true };
                if !edge.is_exit() {
                    return true;
                }
                let own = |asset: dwe_map::Result<dwe_pack::AssetId>| {
                    asset.is_ok_and(|asset| pack.file(&asset).is_some())
                };
                !own(dwe_map::load::map_data_id(&edge.from))
                    && !own(dwe_map::patch::map_patch_id(&edge.from))
            })
            .map(|edge| edge.label())
            .collect()
    }

    /// Removes a map the target pack added, when nothing else leads to it.
    pub(crate) fn delete_map(&mut self, id: &str) -> bool {
        if !self.own_maps().iter().any(|own| own == id) {
            self.messages
                .push(Message::Error(format!("{}: {id}", text::NOT_OWN_MAP)));
            return false;
        }
        self.refresh_world();
        let references: Vec<String> = self
            .world
            .graph
            .references_to(id)
            .iter()
            .map(|edge| edge.label())
            .collect();
        if !references.is_empty() {
            self.messages.push(Message::Error(format!(
                "{}: {}",
                text::MAP_IS_REFERENCED,
                references.join(", ")
            )));
            return false;
        }
        let Some(pack) = self.target_pack() else {
            return false;
        };
        let plan = match new_map::plan_delete_map(pack, id) {
            Ok(plan) => plan,
            Err(error) => {
                self.messages.push(Message::Error(error.to_string()));
                return false;
            }
        };
        if self.map.as_ref().is_some_and(|map| map.document.id == id) {
            self.map = None;
            self.selection = None;
        }
        if let Err(error) = new_map::apply_ops(&plan.ops) {
            self.messages.push(Message::Error(error.to_string()));
            return false;
        }
        self.after_files_written();
        self.messages
            .push(Message::Info(format!("{}: {id}", text::MAP_DELETED)));
        true
    }

    /// Renames a map the target pack added, with the exits of the pack's maps that lead to it.
    pub(crate) fn rename_map(&mut self, id: &str, name: &str) -> bool {
        if !self.own_maps().iter().any(|own| own == id) {
            self.messages
                .push(Message::Error(format!("{}: {id}", text::NOT_OWN_MAP)));
            return false;
        }
        let outside = self.outside_references(id);
        if !outside.is_empty() {
            self.messages.push(Message::Error(format!(
                "{}: {}",
                text::MAP_IS_REFERENCED,
                outside.join(", ")
            )));
            return false;
        }
        let (Some(content), Some(pack)) = (&self.content, self.target_pack()) else {
            return false;
        };
        let plan = match new_map::plan_rename_map(content.vfs(), &content.table, pack, id, name) {
            Ok(plan) => plan,
            Err(error) => {
                self.messages.push(Message::Error(error.to_string()));
                return false;
            }
        };
        let shown = self.map.as_ref().is_some_and(|map| map.document.id == id);
        if shown {
            self.map = None;
            self.selection = None;
        }
        if let Err(error) = new_map::apply_ops(&plan.ops) {
            self.messages.push(Message::Error(error.to_string()));
            return false;
        }
        self.after_files_written();
        self.messages.push(Message::Info(format!(
            "{}: {id} → {}",
            text::MAP_RENAMED,
            plan.id
        )));
        if shown {
            self.apply(crate::Action::OpenMap(plan.id));
        }
        true
    }
}
