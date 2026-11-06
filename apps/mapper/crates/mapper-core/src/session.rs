//! The editor's model: the mounted content, the map shown and how it is shown. It changes only
//! through [`Mapper::apply`].

use std::collections::BTreeMap;
use std::path::PathBuf;

use dwe_map::document::RawVec3;
use dwe_map::places::{Places, plan_places_save};
use dwe_map::save::{Problem, SaveMode, plan_save, write_changes};
use dwe_map::world::{self, Cell};
use dwe_map::{
    Background, CellKind, MapCamera, MapDocument, MapEntry, MapTable, Npc, StoryNames, TriggerBand,
    TriggerBands, Warp,
};
use dwe_pack::{GameContent, GameFolders, Vfs};
use dwe_story::save::plan_story_save;
use dwe_story::{LoadedScript, MapEvents, StoryProblem};
use glam::{Vec2, Vec3};

use crate::action::{Action, PendingChoice};
use crate::browser::{GroupBy, MapGroup, group_maps};
use crate::edit::{CellChange, Drafts, Edit, History};
use crate::layers::{Layer, LayerSet};
use crate::overlay::MapOverlay;
use crate::story::{OpenSection, StoryContent, npc_count_changed};
use crate::targets::{SaveTarget, Settings, create_mod_pack, writable_targets};
use crate::tools::{self, Drag, Modifiers, Selection, Tool};
use crate::view::View;

/// How near a marker the pointer picks it, in canvas pixels.
const PICK_RADIUS: f32 = 10.0;
/// How far from its NPC a new path point goes when no point is given, in world units.
const NEW_POINT_OFFSET: i32 = 200;

/// The mounted content and its map table.
#[derive(Debug)]
pub struct Content {
    pub folders: GameFolders,
    pub game: GameContent,
    pub table: MapTable,
}

impl Content {
    pub fn mount(folders: GameFolders) -> Result<Self, String> {
        let game = GameContent::mount(&folders).map_err(|error| error.to_string())?;
        let table = dwe_map::load_map_table(&game.vfs).map_err(|error| error.to_string())?;
        Ok(Self {
            folders,
            game,
            table,
        })
    }

    pub fn vfs(&self) -> &Vfs {
        &self.game.vfs
    }

    /// Whether the packs have the map's data.
    pub fn has_map(&self, id: &str) -> bool {
        dwe_map::load::map_data_id(id).is_ok_and(|data| self.vfs().contains(&data))
    }
}

/// The map shown and edited.
#[derive(Debug)]
pub struct OpenMap {
    /// The map as edited.
    pub document: MapDocument,
    /// The map as the stack gave it when it was opened or saved last: what a save compares with.
    pub loaded: MapDocument,
    pub history: History,
    pub entry: Option<MapEntry>,
    pub camera: MapCamera,
    pub overlay: MapOverlay,
    pub backgrounds: Vec<Background>,
    /// The story, lines, doors and toilet edited, until a save.
    pub drafts: Drafts,
    /// The doors and the toilet as the table gives them.
    pub places: Places,
    /// What runs the map's sections (with the drafts).
    pub events: MapEvents,
    /// The scripts the map and the section editor show, as they run with the drafts.
    pub scripts: BTreeMap<u16, LoadedScript>,
}

impl OpenMap {
    pub fn background_size(&self) -> Vec2 {
        self.camera.desc().background_size
    }

    /// Whether the map has changes not saved.
    pub fn is_dirty(&self) -> bool {
        self.history.is_dirty()
    }
}

/// What lies under the cursor.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Hover {
    pub background: Vec2,
    /// The point of the ground (y = 0) under the cursor, in world space.
    pub world: Vec3,
    pub cell: Cell,
    pub value: u8,
    pub kind: CellKind,
    pub band: Option<TriggerBand>,
}

impl Hover {
    /// The same point in the original's axes, as the documents store positions.
    pub fn original(&self) -> Vec3 {
        world::from_original_axes(self.world)
    }
}

/// A message for the status bar.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Message {
    Info(String),
    Error(String),
}

#[derive(Debug, Default)]
pub struct Mapper {
    pub content: Option<Content>,
    pub map: Option<OpenMap>,
    pub view: View,
    pub layers: LayerSet,
    /// The trigger bands of the content (`field/triggers`), the original's without one.
    pub bands: TriggerBands,
    /// The labels of the story's triggers and variables (`story/names`).
    pub story_names: StoryNames,
    /// The story modules, patches and lines of the content.
    pub story: StoryContent,
    /// The section open in the section editor.
    pub section: Option<OpenSection>,
    pub variant: String,
    pub hover: Option<Hover>,
    pub filter: String,
    pub group_by: GroupBy,
    /// The maps shown before, for Back.
    pub history: Vec<String>,
    pub messages: Vec<Message>,
    /// Grows whenever the map or its background changes, so the interface knows when to load
    /// the background image again.
    pub revision: u64,

    pub tool: Tool,
    /// The brush's side in cells.
    pub brush_size: i32,
    /// The raw collision value the tools paint.
    pub paint_value: u8,
    pub selection: Option<Selection>,
    /// The gesture in progress on the canvas.
    pub drag: Option<Drag>,
    /// The folder of the pack saves go to.
    pub target: Option<PathBuf>,
    /// What a save found last (errors stop it).
    pub save_problems: Vec<Problem>,
    /// What the last save found in the story drafts (errors stop it).
    pub story_problems: Vec<StoryProblem>,
    /// Grows with every save written, so the interface can ask a connected game to reload.
    pub saves: u64,
    /// An action that waits for an answer about unsaved changes.
    pub pending: Option<Box<Action>>,
    /// Set when the editor may close.
    pub quit: bool,
    pub settings: Settings,
    /// Where the settings are kept; None keeps them in memory (the tests).
    pub settings_file: Option<PathBuf>,
}

impl Mapper {
    pub fn new() -> Self {
        Self {
            variant: "day".to_owned(),
            brush_size: 1,
            paint_value: tools::WALL,
            ..Self::default()
        }
    }

    /// An editor that keeps its settings in a file.
    pub fn with_settings(file: PathBuf) -> Self {
        let settings = Settings::load(&file);
        Self {
            settings,
            settings_file: Some(file),
            ..Self::new()
        }
    }

    /// Whether the map shown has changes not saved.
    pub fn is_dirty(&self) -> bool {
        self.map.as_ref().is_some_and(OpenMap::is_dirty)
    }

    /// Whether a save makes a connected game read the map again (on unless turned off).
    pub fn reload_on_save(&self) -> bool {
        self.settings.reload_on_save.unwrap_or(true)
    }

    pub fn apply(&mut self, action: Action) {
        if action.leaves_map() && self.is_dirty() && !self.reopens_shown_map(&action) {
            self.pending = Some(Box::new(action));
            return;
        }
        match action {
            Action::OpenContent(folders) => self.open_content(folders),
            Action::ReloadContent => self.reload_content(false),
            Action::OpenMap(id) => {
                if self.open_map(&id) {
                    self.fit();
                }
            }
            Action::OpenMapAtExit { map, exit } => {
                if self.open_map(&map) {
                    self.focus_exit(exit);
                }
            }
            Action::FollowExit(exit) => self.follow_exit(exit),
            Action::Back => {
                if let Some(previous) = self.history.pop()
                    && self.load_map(&previous)
                {
                    self.fit();
                }
            }
            Action::SetLayer(layer, shown) => self.layers.set(layer, shown),
            Action::SetVariant(variant) => {
                if variant != self.variant {
                    self.variant = variant;
                    self.revision += 1;
                }
            }
            Action::SetCanvasSize(size) => self.view.set_canvas(size),
            Action::Pan(delta) => self.view.pan(delta),
            Action::Zoom { factor, anchor } => self.view.zoom_at(factor, anchor),
            Action::FitView => self.fit(),
            Action::Hover(position) => self.hover = position.and_then(|at| self.hover_at(at)),
            Action::Click(position) => {
                if let Some(exit) = self.exit_under(position) {
                    self.follow_exit(exit);
                }
            }
            Action::SetFilter(filter) => self.filter = filter,
            Action::SetGroupBy(group_by) => self.group_by = group_by,
            Action::SetTool(tool) => {
                self.tool = tool;
                self.drag = None;
            }
            Action::SetBrushSize(size) => self.brush_size = size.clamp(1, 5),
            Action::SetPaintValue(value) => self.paint_value = value,
            Action::Select(selection) => self.selection = selection,
            Action::PointerDown { at, modifiers } => self.pointer_down(at, modifiers),
            Action::PointerMove(at) => self.pointer_move(at),
            Action::PointerUp => self.pointer_up(),
            Action::BeginEdit => {
                if let Some(map) = &mut self.map {
                    map.history.begin_group();
                }
            }
            Action::EndEdit => {
                if let Some(map) = &mut self.map {
                    map.history.end_group();
                }
            }
            Action::Undo => self.step_history(true),
            Action::Redo => self.step_history(false),
            Action::SetWarp(warp) => self.set_warp(warp),
            Action::SetNpc { slot, npc } => self.set_npc(slot, npc),
            Action::AddWaypoint { slot, at } => self.add_waypoint(slot, at),
            Action::RemoveWaypoint { slot, part } => {
                self.edit_path(slot, |npc| tools::remove_part(npc, part));
                self.selection = Some(Selection::Npc(slot));
            }
            Action::MoveWaypoint { slot, part, later } => {
                if self.edit_path(slot, |npc| tools::move_part(npc, part, later)) {
                    let part = if later {
                        part + 1
                    } else {
                        part.saturating_sub(1)
                    };
                    self.selection = Some(Selection::Waypoint { slot, part });
                }
            }
            Action::Rotate(angle) => self.rotate(angle),
            Action::CenterSelection => self.center_selection(),
            Action::AddNpc(at) => self.add_npc(at),
            Action::RemoveNpc => self.remove_npc(),
            Action::SetDoor { slot, door } => self.set_door(slot, door),
            Action::SetToilet(toilet) => self.set_toilet(toilet),
            Action::OpenSection { script, section } => self.open_section(script, section),
            Action::CloseSection => self.section = None,
            Action::OpenRoutine(routine) => {
                if let Some(open) = &mut self.section {
                    open.routine = routine;
                    open.selected = None;
                }
            }
            Action::SelectStmt(path) => {
                if let Some(open) = &mut self.section {
                    open.selected = path;
                }
            }
            Action::EditStory(change) => self.edit_story(change),
            Action::SetText { key, text } => self.set_text(&key, &text),
            Action::OpenAdjacentMap { later } => self.open_adjacent_map(later),
            Action::Revert => {
                if let Some(id) = self.map.as_ref().map(|map| map.document.id.clone()) {
                    let view = self.view;
                    self.load_map(&id);
                    self.view = view;
                }
            }
            Action::SetDwxPath(path) => {
                self.settings.dwx = path;
                self.store_settings();
            }
            Action::DeleteSelection => {
                if let Some(Selection::Waypoint { slot, part }) = self.selection {
                    self.apply(Action::RemoveWaypoint { slot, part });
                }
            }
            Action::SetTarget(root) => self.set_target(root),
            Action::CreateModPack { folder, id } => self.create_mod_pack(&folder, &id),
            Action::Save(mode) => {
                self.save(mode);
            }
            Action::SetReloadOnSave(on) => {
                self.settings.reload_on_save = Some(on);
                self.store_settings();
            }
            Action::Quit => self.quit = true,
            Action::Resolve(choice) => self.resolve(choice),
        }
    }

    /// Whether an action only opens the map already shown again (nothing is lost).
    fn reopens_shown_map(&self, action: &Action) -> bool {
        let shown = self.map.as_ref().map(|map| map.document.id.as_str());
        matches!(action, Action::OpenMap(id) if Some(id.as_str()) == shown)
    }

    /// Answers the question about unsaved changes and runs the action that waited, unless the
    /// save failed or the answer was to stay.
    fn resolve(&mut self, choice: PendingChoice) {
        let Some(action) = self.pending.take() else {
            return;
        };
        match choice {
            PendingChoice::Cancel => return,
            PendingChoice::Save => {
                if !self.save(SaveMode::Patch) {
                    return;
                }
            }
            PendingChoice::Discard => {
                if let Some(map) = &mut self.map {
                    map.history.mark_clean();
                }
            }
        }
        self.apply(*action);
    }

    /// The packs saves may go to.
    pub fn targets(&self) -> Vec<SaveTarget> {
        self.content
            .as_ref()
            .map(writable_targets)
            .unwrap_or_default()
    }

    /// The target pack, when it is one of the packs saves may go to.
    pub fn current_target(&self) -> Option<SaveTarget> {
        let root = self.target.as_ref()?;
        self.targets()
            .into_iter()
            .find(|target| &target.root == root)
    }

    /// The browser's groups for the current filter.
    pub fn groups(&self) -> Vec<MapGroup> {
        let Some(content) = &self.content else {
            return Vec::new();
        };
        group_maps(
            &content.table,
            |id| content.has_map(id),
            self.group_by,
            &self.filter,
        )
    }

    /// The file of the background shown: the chosen variant, or the map's first.
    pub fn background_file(&self) -> Option<PathBuf> {
        let content = self.content.as_ref()?;
        let map = self.map.as_ref()?;
        let background = map
            .backgrounds
            .iter()
            .find(|background| background.variant == self.variant)
            .or_else(|| map.backgrounds.first())?;
        content
            .vfs()
            .resolve(&background.id)
            .map(|(_, file)| file.to_path_buf())
    }

    fn open_content(&mut self, folders: GameFolders) {
        match Content::mount(folders) {
            Ok(content) => {
                for skipped in &content.game.skipped_mods {
                    self.messages.push(Message::Error(format!(
                        "{}: {} ({})",
                        crate::text::SKIPPED_MOD,
                        skipped.path.display(),
                        skipped.reason
                    )));
                }
                if !content.game.archives.is_empty() {
                    self.messages.push(Message::Info(format!(
                        "{}: {}",
                        crate::text::ARCHIVES_NOT_READ,
                        content.game.archives.len()
                    )));
                }
                self.bands = TriggerBands::load(content.vfs()).unwrap_or_else(|error| {
                    self.messages.push(Message::Error(format!(
                        "{error} ({})",
                        crate::text::DEFAULT_BANDS
                    )));
                    TriggerBands::default()
                });
                self.story_names = StoryNames::load(content.vfs()).unwrap_or_else(|error| {
                    self.messages.push(Message::Error(error.to_string()));
                    StoryNames::default()
                });
                self.story = StoryContent::load(content.vfs());
                for problem in &self.story.patches.problems {
                    self.messages.push(Message::Error(problem.clone()));
                }
                self.section = None;
                self.content = Some(content);
                self.map = None;
                self.history.clear();
                self.hover = None;
                self.selection = None;
                self.drag = None;
                self.revision += 1;
                if self.current_target().is_none() {
                    self.target = self.settings.last_target.clone();
                    if self.current_target().is_none() {
                        self.target = None;
                    }
                }
            }
            Err(error) => self.messages.push(Message::Error(error)),
        }
    }

    /// Opens a map, remembering the one shown for Back.
    fn open_map(&mut self, id: &str) -> bool {
        let previous = self.map.as_ref().map(|map| map.document.id.clone());
        if !self.load_map(id) {
            return false;
        }
        if let Some(previous) = previous.filter(|previous| previous != id) {
            self.history.push(previous);
        }
        true
    }

    fn load_map(&mut self, id: &str) -> bool {
        let Some(content) = &self.content else {
            return false;
        };
        let document = match dwe_map::load_map(content.vfs(), id) {
            Ok(document) => document,
            Err(error) => {
                self.messages.push(Message::Error(error.to_string()));
                return false;
            }
        };
        let entry = content.table.find(id).cloned();
        let camera = MapCamera::new(document.camera_desc());
        let places = Places::of(&content.table, entry.as_ref());
        let overlay = MapOverlay::build(&document, &places, &content.table, &camera, &self.bands);
        let backgrounds = dwe_map::backgrounds(content.vfs(), id);
        self.map = Some(OpenMap {
            loaded: document.clone(),
            document,
            history: History::default(),
            entry,
            camera,
            overlay,
            backgrounds,
            drafts: Drafts::default(),
            places,
            events: MapEvents::default(),
            scripts: BTreeMap::new(),
        });
        self.hover = None;
        self.selection = None;
        self.drag = None;
        self.revision += 1;
        self.refresh_story();
        true
    }

    fn fit(&mut self) {
        if let Some(map) = &self.map {
            self.view.fit(map.background_size());
        }
    }

    /// Centres the view on where the player arrives through an exit, at a readable zoom.
    fn focus_exit(&mut self, exit: i32) {
        self.fit();
        let Some(map) = &self.map else { return };
        let arrival = map
            .overlay
            .exits
            .iter()
            .find(|mark| mark.exit == exit)
            .and_then(|mark| mark.arrival);
        if let Some(arrival) = arrival {
            self.view.center = arrival.from;
            self.view.zoom = self.view.zoom.max(1.0);
        }
    }

    fn follow_exit(&mut self, exit: i32) {
        let destination = self.map.as_ref().and_then(|map| {
            let mark = map.overlay.exits.iter().find(|mark| mark.exit == exit)?;
            let (id, exit) = mark.target.destination()?;
            Some((id.to_owned(), exit))
        });
        if let Some((map, exit)) = destination {
            self.apply(Action::OpenMapAtExit { map, exit });
        }
    }

    fn hover_at(&self, canvas: Vec2) -> Option<Hover> {
        let map = self.map.as_ref()?;
        let background = self.view.to_background(canvas);
        let world = map.camera.ground_at_background(background, 0.0)?;
        let cell = world::cell_at(world);
        let value = map.document.collision.value(cell);
        let kind = map.document.collision.kind(cell);
        let band = match kind {
            CellKind::Trigger(value) => Some(self.bands.band(value)),
            _ => None,
        };
        Some(Hover {
            background,
            world,
            cell,
            value,
            kind,
            band,
        })
    }

    fn exit_under(&self, canvas: Vec2) -> Option<i32> {
        let hover = self.hover_at(canvas)?;
        let map = self.map.as_ref()?;
        if !hover.cell.is_inside() {
            return None;
        }
        map.overlay
            .exit_at(hover.value, &self.bands)
            .map(|mark| mark.exit)
    }

    /// Mounts the content again and opens the map shown again at the same view. With
    /// `keep_edits` the document and its history stay (after a save, whose files the new mount
    /// reads); without, the map is read again from the packs.
    fn reload_content(&mut self, keep_edits: bool) {
        let Some(folders) = self.content.as_ref().map(|content| content.folders.clone()) else {
            return;
        };
        let shown = self.map.take();
        let (view, history, selection) = (self.view, self.history.clone(), self.selection);
        let section = self.section.clone();
        self.open_content(folders);
        self.section = section;
        let Some(shown) = shown else { return };
        if !self.load_map(&shown.document.id) {
            return;
        }
        self.view = view;
        self.history = history;
        if let Some(map) = &mut self.map
            && keep_edits
        {
            map.document = shown.document;
            map.history = shown.history;
            map.drafts = shown.drafts;
            self.selection = selection;
        }
        self.refresh_overlay();
        self.refresh_story();
    }

    /// Builds the layers again after the document changed.
    fn refresh_overlay(&mut self) {
        let places = self.places();
        let (Some(content), Some(map)) = (&self.content, &mut self.map) else {
            return;
        };
        map.overlay = MapOverlay::build(
            &map.document,
            &places,
            &content.table,
            &map.camera,
            &self.bands,
        );
    }

    /// Applies an edit to the map shown and records it; false when it changed nothing.
    pub(crate) fn record(&mut self, edit: Edit) -> bool {
        let Some(map) = &mut self.map else {
            return false;
        };
        // A brush stroke builds the events again when it ends.
        let story = !matches!(edit, Edit::Cells(_));
        let recorded = map.history.record(&mut map.document, &mut map.drafts, edit);
        if recorded {
            self.refresh_overlay();
            if story {
                self.refresh_story();
            }
        }
        recorded
    }

    fn step_history(&mut self, undo: bool) {
        self.drag = None;
        let Some(map) = &mut self.map else { return };
        let stepped = if undo {
            map.history.undo(&mut map.document, &mut map.drafts)
        } else {
            map.history.redo(&mut map.document, &mut map.drafts)
        };
        if stepped {
            self.refresh_overlay();
            self.refresh_story();
        }
    }

    /// Paints the paint value on cells.
    fn paint(&mut self, cells: &[Cell]) {
        let Some(map) = &self.map else { return };
        let value = self.paint_value;
        let changes = cells
            .iter()
            .filter(|cell| cell.is_inside())
            .map(|&cell| CellChange {
                cell,
                before: map.document.collision.value(cell),
                after: value,
            })
            .collect();
        self.record(Edit::Cells(changes));
    }

    fn pointer_down(&mut self, at: Vec2, modifiers: Modifiers) {
        self.drag = None;
        let Some(hover) = self.hover_at(at) else {
            return;
        };
        let cell = hover.cell;
        let take_value = self.tool == Tool::Eyedropper || (self.tool.paints() && modifiers.alt);
        if take_value {
            if cell.is_inside() {
                self.paint_value = hover.value;
            }
            return;
        }
        match self.tool {
            Tool::Brush => {
                if let Some(map) = &mut self.map {
                    map.history.begin_group();
                }
                self.paint(&tools::brush_cells(cell, self.brush_size));
                self.drag = Some(Drag::Stroke { last: cell });
            }
            Tool::Rectangle => {
                self.drag = Some(Drag::Rectangle {
                    from: cell,
                    to: cell,
                });
            }
            Tool::Fill => {
                let Some(map) = &self.map else { return };
                let collision = &map.document.collision;
                let cells = tools::fill_cells(|cell| collision.value(cell), cell);
                self.paint(&cells);
            }
            Tool::Eyedropper => {}
            Tool::Select => {
                if modifiers.ctrl {
                    if let Some(exit) = self.exit_under(at) {
                        self.follow_exit(exit);
                    }
                    return;
                }
                let picked = self.pick(at);
                self.selection = picked.map(|(selection, _)| selection);
                self.drag = Some(match picked {
                    Some((selection, true)) => {
                        if let Some(map) = &mut self.map {
                            map.history.begin_group();
                        }
                        if modifiers.shift {
                            Drag::Turn(selection)
                        } else {
                            Drag::Move(selection)
                        }
                    }
                    _ => Drag::Pan { last: at },
                });
            }
        }
    }

    fn pointer_move(&mut self, at: Vec2) {
        let Some(drag) = self.drag else { return };
        match drag {
            Drag::Pan { last } => {
                self.view.pan(at - last);
                self.drag = Some(Drag::Pan { last: at });
            }
            Drag::Stroke { last } => {
                let Some(hover) = self.hover_at(at) else {
                    return;
                };
                let cells: Vec<Cell> = tools::line_cells(last, hover.cell)
                    .into_iter()
                    .flat_map(|cell| tools::brush_cells(cell, self.brush_size))
                    .collect();
                self.paint(&cells);
                self.drag = Some(Drag::Stroke { last: hover.cell });
            }
            Drag::Rectangle { from, .. } => {
                if let Some(hover) = self.hover_at(at) {
                    self.drag = Some(Drag::Rectangle {
                        from,
                        to: hover.cell,
                    });
                }
            }
            Drag::Move(selection) => self.move_selection(selection, at),
            Drag::Turn(selection) => self.turn_selection(selection, at),
        }
    }

    fn pointer_up(&mut self) {
        let drag = self.drag.take();
        if let Some(Drag::Rectangle { from, to }) = drag {
            self.paint(&tools::rectangle_cells(from, to));
        }
        if let Some(map) = &mut self.map {
            map.history.end_group();
        }
        if drag.is_some() || self.tool.paints() {
            self.refresh_story();
        }
    }

    /// The marker under a canvas point: a path point, an NPC or an exit's arrival (those whose
    /// layer is shown), which a drag moves; else the exit whose trigger cell is there.
    fn pick(&self, at: Vec2) -> Option<(Selection, bool)> {
        let map = self.map.as_ref()?;
        let near = |background: Vec2| self.view.to_canvas(background).distance(at) <= PICK_RADIUS;
        if self.layers.shows(Layer::Waypoints) {
            for npc in &map.overlay.npcs {
                for (&point, &part) in npc.waypoints.iter().zip(&npc.parts) {
                    if near(point) {
                        let selection = Selection::Waypoint {
                            slot: npc.slot,
                            part,
                        };
                        return Some((selection, true));
                    }
                }
            }
        }
        if self.layers.shows(Layer::Npcs) {
            for npc in &map.overlay.npcs {
                if npc.position.is_some_and(|arrow| near(arrow.from)) {
                    return Some((Selection::Npc(npc.slot), true));
                }
            }
        }
        if self.layers.shows(Layer::Doors) {
            for door in &map.overlay.doors {
                if near(door.position.from) {
                    return Some((Selection::Door(door.slot), true));
                }
            }
        }
        if self.layers.shows(Layer::Toilet)
            && let Some(points) = map.overlay.toilet
        {
            for (index, point) in points.into_iter().enumerate() {
                if near(point) {
                    return Some((Selection::Toilet(index), true));
                }
            }
        }
        if self.layers.shows(Layer::Exits) {
            for exit in map.overlay.exits.iter().filter(|exit| exit.is_used()) {
                if exit.arrival.is_some_and(|arrow| near(arrow.from)) {
                    return Some((Selection::Exit(exit.exit), true));
                }
            }
        }
        self.exit_under(at)
            .map(|exit| (Selection::Exit(exit), false))
    }

    /// The original position of what a selection names.
    fn position_of(&self, selection: Selection) -> Option<RawVec3> {
        let document = &self.map.as_ref()?.document;
        match selection {
            Selection::Exit(exit) => document.warp(exit).map(|warp| warp.spawn),
            Selection::Npc(slot) => document.npcs.get(slot).map(|npc| npc.position),
            Selection::Waypoint { slot, part } => document
                .npcs
                .get(slot)
                .and_then(|npc| npc.waypoints.get(part).copied()),
            Selection::Door(slot) => self.places().doors.get(slot).map(|door| door.position),
            Selection::Toilet(index) => self.places().toilet.map(|toilet| {
                let (x, z) = if index == 0 {
                    (toilet.x1, toilet.z1)
                } else {
                    (toilet.x2, toilet.z2)
                };
                RawVec3 { x, y: 0, z }
            }),
        }
    }

    /// The point of the ground under a canvas point at the height of an original position, in
    /// the original's axes.
    pub(crate) fn ground_under(&self, at: Vec2, height_of: RawVec3) -> Option<RawVec3> {
        let map = self.map.as_ref()?;
        let height = world::to_world(height_of.into()).y;
        let ground = map
            .camera
            .ground_at_background(self.view.to_background(at), height)?;
        let original = world::from_original_axes(ground);
        Some(RawVec3 {
            x: original.x.round() as i32,
            y: height_of.y,
            z: original.z.round() as i32,
        })
    }

    fn move_selection(&mut self, selection: Selection, at: Vec2) {
        let Some(position) = self.position_of(selection) else {
            return;
        };
        let Some(point) = self.ground_under(at, position) else {
            return;
        };
        let Some(map) = &self.map else { return };
        match selection {
            Selection::Exit(exit) => {
                if let Some(warp) = map.document.warp(exit) {
                    let mut warp = warp.clone();
                    warp.spawn = point;
                    self.set_warp(warp);
                }
            }
            Selection::Npc(slot) => {
                if let Some(npc) = map.document.npcs.get(slot) {
                    let mut npc = npc.clone();
                    npc.position = point;
                    self.set_npc(slot, npc);
                }
            }
            Selection::Waypoint { slot, part } => {
                if let Some(npc) = map.document.npcs.get(slot) {
                    let mut npc = npc.clone();
                    if let Some(waypoint) = npc.waypoints.get_mut(part) {
                        *waypoint = point;
                        self.set_npc(slot, npc);
                    }
                }
            }
            Selection::Door(slot) => {
                if let Some(door) = self.places().doors.get(slot) {
                    let mut door = door.clone();
                    door.position = point;
                    self.set_door(slot, door);
                }
            }
            Selection::Toilet(index) => {
                if let Some(mut toilet) = self.places().toilet {
                    if index == 0 {
                        (toilet.x1, toilet.z1) = (point.x, point.z);
                    } else {
                        (toilet.x2, toilet.z2) = (point.x, point.z);
                    }
                    self.set_toilet(Some(toilet));
                }
            }
        }
    }

    /// Turns an exit's arrival or an NPC to face the ground under a canvas point.
    fn turn_selection(&mut self, selection: Selection, at: Vec2) {
        let Some(position) = self.position_of(selection) else {
            return;
        };
        let Some(point) = self.ground_under(at, position) else {
            return;
        };
        let direction = world::to_world(point.into()) - world::to_world(position.into());
        if direction.length_squared() < 1.0 {
            return;
        }
        let rotation = world::rotation_facing(direction);
        self.set_rotation(selection, |_| rotation);
    }

    fn rotate(&mut self, angle: i32) {
        if let Some(selection) = self.selection {
            self.set_rotation(selection, |rotation| {
                (rotation + angle).rem_euclid(world::ORIGINAL_ANGLE_PER_TURN)
            });
        }
    }

    fn set_rotation(&mut self, selection: Selection, turn: impl Fn(i32) -> i32) {
        let Some(map) = &self.map else { return };
        match selection {
            Selection::Exit(exit) => {
                if let Some(warp) = map.document.warp(exit) {
                    let mut warp = warp.clone();
                    warp.rotation = turn(warp.rotation);
                    self.set_warp(warp);
                }
            }
            Selection::Npc(slot) => {
                if let Some(npc) = map.document.npcs.get(slot) {
                    let mut npc = npc.clone();
                    npc.rotation.y = turn(npc.rotation.y);
                    self.set_npc(slot, npc);
                }
            }
            Selection::Door(slot) => {
                if let Some(door) = self.places().doors.get(slot) {
                    let mut door = door.clone();
                    door.rotation = turn(door.rotation);
                    self.set_door(slot, door);
                }
            }
            Selection::Waypoint { .. } | Selection::Toilet(_) => {}
        }
    }

    /// Centres the view on the selection, at a readable zoom.
    fn center_selection(&mut self) {
        let Some(selection) = self.selection else {
            return;
        };
        let Some(position) = self.position_of(selection) else {
            return;
        };
        let Some(map) = &self.map else { return };
        let world = world::to_world(position.into());
        if let Some(point) = map.camera.project_to_background(world) {
            self.view.center = point.pixel;
            self.view.zoom = self.view.zoom.max(1.0);
        }
    }

    /// Opens the map before or after the one shown in the browser's list.
    fn open_adjacent_map(&mut self, later: bool) {
        let ids: Vec<String> = self
            .groups()
            .into_iter()
            .flat_map(|group| group.maps)
            .map(|map| map.id)
            .collect();
        if ids.is_empty() {
            return;
        }
        let shown = self.map.as_ref().map(|map| map.document.id.as_str());
        let next = match shown.and_then(|shown| ids.iter().position(|id| id == shown)) {
            Some(at) if later => (at + 1) % ids.len(),
            Some(at) => (at + ids.len() - 1) % ids.len(),
            None => 0,
        };
        let id = ids[next].clone();
        if self.open_map(&id) {
            self.fit();
        }
    }

    fn set_warp(&mut self, warp: Warp) {
        let Some(before) = self
            .map
            .as_ref()
            .and_then(|map| map.document.warp(warp.exit).cloned())
        else {
            return;
        };
        self.record(Edit::Warp {
            before,
            after: warp,
        });
    }

    fn set_npc(&mut self, slot: usize, npc: Npc) {
        let Some(before) = self
            .map
            .as_ref()
            .and_then(|map| map.document.npcs.get(slot).cloned())
        else {
            return;
        };
        self.record(Edit::Npc {
            slot,
            before: Box::new(before),
            after: Box::new(npc),
        });
    }

    /// Changes an NPC's path; false when the change does not apply.
    fn edit_path(&mut self, slot: usize, change: impl Fn(&Npc) -> Option<Npc>) -> bool {
        let Some(npc) = self
            .map
            .as_ref()
            .and_then(|map| map.document.npcs.get(slot))
            .and_then(change)
        else {
            return false;
        };
        self.set_npc(slot, npc);
        true
    }

    fn add_waypoint(&mut self, slot: usize, at: Option<Vec2>) {
        let Some(npc) = self
            .map
            .as_ref()
            .and_then(|map| map.document.npcs.get(slot))
        else {
            return;
        };
        let point = at
            .and_then(|at| self.ground_under(at, npc.position))
            .unwrap_or(RawVec3 {
                x: npc.position.x + NEW_POINT_OFFSET,
                ..npc.position
            });
        let part = tools::path_length(npc);
        if self.edit_path(slot, |npc| tools::add_part(npc, point)) {
            self.selection = Some(Selection::Waypoint { slot, part });
        }
    }

    fn set_target(&mut self, root: Option<PathBuf>) {
        self.target = root;
        if self.target.is_some() {
            self.settings.last_target = self.target.clone();
            self.store_settings();
        }
    }

    fn store_settings(&mut self) {
        if let Some(file) = &self.settings_file
            && let Err(error) = self.settings.store(file)
        {
            self.messages
                .push(Message::Error(format!("{}: {error}", file.display())));
        }
    }

    fn create_mod_pack(&mut self, folder: &std::path::Path, id: &str) {
        let root = match create_mod_pack(folder, id) {
            Ok(root) => root,
            Err(error) => {
                self.messages.push(Message::Error(error));
                return;
            }
        };
        if let Some(content) = &mut self.content {
            let mods = &mut content.folders.mods;
            if !mods.iter().any(|known| known == folder) {
                mods.push(folder.to_path_buf());
            }
        }
        self.reload_content(true);
        self.set_target(Some(root.clone()));
        if self.current_target().is_none() {
            self.messages.push(Message::Error(format!(
                "{}: {}",
                crate::text::PACK_NOT_MOUNTED,
                root.display()
            )));
        }
    }

    /// Saves the map shown into the target pack, with its story drafts, lines, doors and toilet;
    /// true when it was written.
    pub fn save(&mut self, mode: SaveMode) -> bool {
        self.save_problems.clear();
        self.story_problems.clear();
        let Some(target) = self.current_target() else {
            self.messages
                .push(Message::Error(crate::text::NO_TARGET.to_owned()));
            return false;
        };
        let (Some(content), Some(map)) = (&self.content, &mut self.map) else {
            return false;
        };
        map.history.end_group();
        let Some(pack) = content
            .vfs()
            .packs()
            .iter()
            .find(|pack| pack.root == target.root)
        else {
            return false;
        };
        // Patches name NPC slots by place: a list that grew or shrank goes as the whole data.
        let mode = if mode == SaveMode::Patch && npc_count_changed(&map.loaded, &map.document) {
            self.messages
                .push(Message::Info(crate::text::NPCS_SAVE_WHOLE.to_owned()));
            SaveMode::WholeData
        } else {
            mode
        };
        let plan = plan_save(
            content.vfs(),
            &content.table,
            &self.bands,
            pack,
            &map.loaded,
            &map.document,
            mode,
        );
        let story = plan_story_save(
            content.vfs(),
            &self.story.patches,
            pack,
            &map.drafts.scripts,
            &map.drafts.texts,
            dwe_story::texts::ENGLISH,
        );
        let places = match &map.drafts.places {
            Some(after) => {
                plan_places_save(content.vfs(), pack, &map.document.id, &map.places, after)
            }
            None => Ok(None),
        };
        self.save_problems = plan.problems.clone();
        self.story_problems = story.problems.clone();
        let places = match places {
            Ok(places) => places,
            Err(error) => {
                self.story_problems
                    .push(StoryProblem::Error(error.to_string()));
                None
            }
        };
        if plan.has_errors() || self.story_problems.iter().any(StoryProblem::is_error) {
            self.messages
                .push(Message::Error(crate::text::SAVE_REFUSED.to_owned()));
            return false;
        }
        let story_saved = !story.changes.is_empty() || places.is_some();
        let mut changes = plan.changes;
        changes.extend(story.changes);
        changes.extend(places);
        if let Err(error) = write_changes(&changes) {
            self.messages.push(Message::Error(format!(
                "{}: {error}",
                crate::text::SAVE_FAILED
            )));
            return false;
        }
        map.history.mark_clean();
        if story_saved {
            // The drafts are the packs' files now; their undo steps would not undo the files.
            map.drafts = Drafts::default();
            map.history = History::default();
        }
        self.saves += 1;
        let files: Vec<String> = changes
            .iter()
            .map(|change| change.path.display().to_string())
            .collect();
        let written = if files.is_empty() {
            crate::text::NOTHING_TO_SAVE.to_owned()
        } else {
            files.join(", ")
        };
        self.reload_content(true);
        self.messages.push(Message::Info(format!(
            "{} {}: {written}",
            crate::text::SAVED_IN,
            target.id
        )));
        for problem in &self.story_problems {
            if let StoryProblem::Warning(warning) = problem {
                self.messages.push(Message::Info(warning.clone()));
            }
        }
        if !story.warps_changed.is_empty() {
            self.make_script_warps(&target.root);
        }
        true
    }

    /// The dwx command that makes the target's `field/script_warps` again from the packs' Lua.
    pub fn script_warps_command(&self, target: &std::path::Path) -> Option<Vec<String>> {
        let content = self.content.as_ref()?;
        let folders = &content.folders;
        let dwx = self
            .settings
            .dwx
            .clone()
            .unwrap_or_else(|| PathBuf::from("dwx"));
        let mut command = vec![
            dwx.display().to_string(),
            "script-warps".to_owned(),
            folders.content.display().to_string(),
        ];
        for folder in folders.team_packs.iter().chain(&folders.mods) {
            command.push("--with".to_owned());
            command.push(folder.display().to_string());
        }
        command.push("-o".to_owned());
        command.push(target.join("field/script_warps.yaml").display().to_string());
        Some(command)
    }

    /// Runs dwx to make `field/script_warps` again (when it is set), or says the command.
    fn make_script_warps(&mut self, target: &std::path::Path) {
        let Some(command) = self.script_warps_command(target) else {
            return;
        };
        let line = command.join(" ");
        if self.settings.dwx.is_none() {
            self.messages.push(Message::Error(format!(
                "{}: {line}",
                crate::text::WARPS_BY_HAND
            )));
            return;
        }
        let ran = std::process::Command::new(&command[0])
            .args(&command[1..])
            .output();
        match ran {
            Ok(output) if output.status.success() => {
                self.messages.push(Message::Info(format!(
                    "{}: {}",
                    crate::text::WARPS_MADE,
                    command.last().map_or("", String::as_str)
                )));
                self.reload_content(true);
            }
            Ok(output) => self.messages.push(Message::Error(format!(
                "{} ({line}): {}",
                crate::text::WARPS_FAILED,
                String::from_utf8_lossy(&output.stderr).trim()
            ))),
            Err(error) => self.messages.push(Message::Error(format!(
                "{} ({line}): {error}",
                crate::text::WARPS_FAILED
            ))),
        }
    }
}
