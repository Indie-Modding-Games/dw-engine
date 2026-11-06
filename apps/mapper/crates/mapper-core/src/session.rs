//! The editor's model: the mounted content, the map shown and how it is shown. It changes only
//! through [`Mapper::apply`].

use std::path::PathBuf;

use dwe_map::world::{self, Cell};
use dwe_map::{
    Background, CellKind, MapCamera, MapDocument, MapEntry, MapTable, TriggerBand, TriggerBands,
};
use dwe_pack::{GameContent, GameFolders, Vfs};
use glam::{Vec2, Vec3};

use crate::action::Action;
use crate::browser::{GroupBy, MapGroup, group_maps};
use crate::layers::LayerSet;
use crate::overlay::MapOverlay;
use crate::view::View;

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

/// The map shown.
#[derive(Debug)]
pub struct OpenMap {
    pub document: MapDocument,
    pub entry: Option<MapEntry>,
    pub camera: MapCamera,
    pub overlay: MapOverlay,
    pub backgrounds: Vec<Background>,
}

impl OpenMap {
    pub fn background_size(&self) -> Vec2 {
        self.camera.desc().background_size
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
    pub bands: TriggerBands,
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
}

impl Mapper {
    pub fn new() -> Self {
        Self {
            variant: "day".to_owned(),
            ..Self::default()
        }
    }

    pub fn apply(&mut self, action: Action) {
        match action {
            Action::OpenContent(folders) => self.open_content(folders),
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
        }
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
                self.content = Some(content);
                self.map = None;
                self.history.clear();
                self.hover = None;
                self.revision += 1;
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
        let overlay = MapOverlay::build(
            &document,
            entry.as_ref(),
            &content.table,
            &camera,
            &self.bands,
        );
        let backgrounds = dwe_map::backgrounds(content.vfs(), id);
        self.map = Some(OpenMap {
            document,
            entry,
            camera,
            overlay,
            backgrounds,
        });
        self.hover = None;
        self.revision += 1;
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
}
