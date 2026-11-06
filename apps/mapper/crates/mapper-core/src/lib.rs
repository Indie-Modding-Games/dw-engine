//! The map editor's model, without any window: the content it opens, the map shown, its layers and
//! the [`Action`]s that change them. The binary draws it; the tests drive it headless.

pub mod action;
pub mod browser;
pub mod layers;
pub mod overlay;
pub mod session;
pub mod text;
pub mod view;

pub use action::Action;
pub use browser::{GroupBy, MapGroup, MapItem};
pub use layers::{Layer, LayerSet};
pub use overlay::{ExitMark, ExitTarget, MapOverlay};
pub use session::{Content, Hover, Mapper, Message, OpenMap};
pub use view::View;
