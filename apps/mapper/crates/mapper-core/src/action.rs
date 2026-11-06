//! Every change to what the editor shows goes through an [`Action`], so the interface, the command
//! line, the live link and later the editing tools all change the model the same way (and an undo
//! history can plug in here).

use dwe_pack::GameFolders;
use glam::Vec2;

use crate::browser::GroupBy;
use crate::layers::Layer;

#[derive(Clone, Debug, PartialEq)]
pub enum Action {
    /// Mounts a content folder (with the team's packs and mods), as the game does.
    OpenContent(GameFolders),
    /// Shows a map, fitted to the canvas.
    OpenMap(String),
    /// Shows a map centred on where the player arrives through one of its exits.
    OpenMapAtExit {
        map: String,
        exit: i32,
    },
    /// Goes where an exit of the shown map leads.
    FollowExit(i32),
    /// Goes back to the map shown before.
    Back,
    SetLayer(Layer, bool),
    /// Shows a background variant: day, dusk, night...
    SetVariant(String),
    /// The canvas's size, in pixels.
    SetCanvasSize(Vec2),
    /// A drag of the canvas, in canvas pixels.
    Pan(Vec2),
    /// Zooms by a factor around a canvas point.
    Zoom {
        factor: f32,
        anchor: Vec2,
    },
    FitView,
    /// The cursor over the canvas, in canvas pixels, or None when it left.
    Hover(Option<Vec2>),
    /// A click on the canvas: on an exit's trigger cell it goes where the exit leads.
    Click(Vec2),
    SetFilter(String),
    SetGroupBy(GroupBy),
}
