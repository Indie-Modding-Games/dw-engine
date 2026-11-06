//! dwe's field maps for the tools: the map document and the map table read from the packs, the
//! collision grid and its trigger bands, world space, and the fixed camera that projects the world
//! on the prerendered background exactly as the engine does
//! (packages/engine/render/src/map_camera.cpp).

pub mod camera;
pub mod collision;
pub mod document;
pub mod load;
pub mod table;
pub mod triggers;
pub mod world;

pub use camera::{BackgroundPoint, MapCamera, MapCameraDesc};
pub use collision::{CellKind, Collision};
pub use document::{MapDocument, Npc, PathPart, Warp};
pub use load::{Background, WarpTarget, backgrounds, load_map, load_map_table, warp_target};
pub use table::{MapEntry, MapTable};
pub use triggers::{TriggerBand, TriggerBands};
pub use world::Cell;

/// What can go wrong reading a map.
#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error(transparent)]
    Pack(dwe_pack::Error),
    #[error("{0}")]
    Invalid(String),
}

pub type Result<T> = std::result::Result<T, Error>;
