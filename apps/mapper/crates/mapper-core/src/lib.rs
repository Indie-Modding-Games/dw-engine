//! The map editor's model, without any window: the content it opens, the map shown, its layers and
//! the [`Action`]s that change them. The binary draws it; the tests drive it headless.

pub mod action;
pub mod browser;
pub mod edit;
pub mod layers;
pub mod live;
pub mod overlay;
pub mod session;
pub mod story;
pub mod targets;
pub mod text;
pub mod tools;
pub mod view;

pub use action::{Action, PendingChoice};
pub use browser::{GroupBy, MapGroup, MapItem};
pub use edit::{Drafts, Edit, History};
pub use layers::{Layer, LayerSet};
pub use live::{Follow, PreviewStep, PreviewSync};
pub use overlay::{ExitMark, ExitTarget, MapOverlay};
pub use session::{Content, Hover, Mapper, Message, OpenMap};
pub use story::{OpenSection, StoryContent, StoryEdit, TestPlan};
pub use targets::{SaveTarget, Settings, TargetKind};
pub use tools::{Modifiers, Selection, Tool};
pub use view::View;
