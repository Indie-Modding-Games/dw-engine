//! dwe's content packs as the engine mounts them (docs/architecture/vfs.md): a pack's manifest,
//! asset ids, the stack of packs by layer and priority, the files that win, and the content tables
//! composed entry by entry. It mirrors `packages/engine/assets` (virtual_file_system.cpp,
//! table_composition.cpp, content_id.cpp) so the tools read exactly what the game reads.

mod asset_id;
mod compose;
mod content;
mod content_id;
mod error;
mod find;
mod manifest;
mod vfs;

pub use asset_id::AssetId;
pub use compose::{
    ComposedTable, Composition, Contribution, EntrySection, MAP_COMPOSITION, compose_table,
};
pub use content::{GameContent, GameFolders, SkippedMod};
pub use content_id::ContentId;
pub use error::{Error, Result};
pub use find::{FoundPacks, find_packs};
pub use manifest::{Dependency, Layer, PackManifest, Version};
pub use vfs::{MountedPack, PackLocation, PackOrigin, Vfs, has_base_rights};
