//! Every change to the editor goes through an [`Action`], so the interface, the command line, the
//! live link and the tests change the model the same way. The edits of the map document among
//! them are recorded in the map's undo history ([`crate::edit`]).

use std::path::PathBuf;

use dwe_map::save::SaveMode;
use dwe_map::table::{MapDoor, MapToilet};
use dwe_map::{Npc, Warp};
use dwe_pack::GameFolders;
use dwe_story::edit::StmtPath;
use glam::Vec2;

use crate::browser::GroupBy;
use crate::layers::Layer;
use crate::story::StoryEdit;
use crate::timeline::NewCue;
use crate::tools::{Modifiers, Selection, Tool};

#[derive(Clone, Debug, PartialEq)]
pub enum Action {
    /// Mounts a content folder (with the team's packs and mods), as the game does.
    OpenContent(GameFolders),
    /// Mounts the same content again, to see files written since (the map shown stays open).
    ReloadContent,
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

    // Editing.
    SetTool(Tool),
    /// The brush's side, in cells (1, 3 or 5).
    SetBrushSize(i32),
    /// The raw collision value the tools paint.
    SetPaintValue(u8),
    Select(Option<Selection>),
    /// The primary button pressed on the canvas, with the tool chosen.
    PointerDown {
        at: Vec2,
        modifiers: Modifiers,
    },
    /// The pointer moved with the button held.
    PointerMove(Vec2),
    /// The button released: the gesture ends (one undo step).
    PointerUp,
    /// The edits until [`Action::EndEdit`] are one undo step (a field dragged in the inspector).
    BeginEdit,
    EndEdit,
    Undo,
    Redo,
    /// Replaces an exit's record (by its `exit`).
    SetWarp(Warp),
    /// Replaces an NPC slot.
    SetNpc {
        slot: usize,
        npc: Npc,
    },
    /// Adds a walk at the end of an NPC's path: to the ground under a canvas point, or next to the
    /// NPC.
    AddWaypoint {
        slot: usize,
        at: Option<Vec2>,
    },
    RemoveWaypoint {
        slot: usize,
        part: usize,
    },
    /// Swaps a part of a path with the next one or the previous one.
    MoveWaypoint {
        slot: usize,
        part: usize,
        later: bool,
    },
    /// Turns the selected exit's arrival or NPC by an angle (4096 a turn).
    Rotate(i32),
    /// Removes the selected path point.
    DeleteSelection,
    /// Centres the view on what is selected.
    CenterSelection,
    /// Adds an NPC slot (a copy of the selected NPC or of the last) on the ground under a canvas
    /// point, spawned by the map's entry section.
    AddNpc(Option<Vec2>),
    /// Removes the last NPC slot and its spawns.
    RemoveNpc,
    /// Replaces a door slot of the map's set.
    SetDoor {
        slot: usize,
        door: MapDoor,
    },
    /// Sets the toilet's points, or takes the map's toilet away.
    SetToilet(Option<MapToilet>),

    // The story.
    /// Opens a section of a script in the section editor.
    OpenSection {
        script: u16,
        section: u16,
    },
    CloseSection,
    /// Shows a routine of the open section's file, or the section's code again.
    OpenRoutine(Option<String>),
    /// Selects a statement of the code shown.
    SelectStmt(Option<StmtPath>),
    /// Changes the code shown (one undo step).
    EditStory(StoryEdit),
    /// Shows or hides the open section's timeline.
    ShowTimeline(bool),
    /// Puts the timeline's playhead at a frame of the original.
    SetPlayhead(f32),
    /// Selects a statement from the timeline and puts the playhead at its start.
    SelectCue(StmtPath),
    /// Moves where a walk, a slide or a pan goes to the ground under a canvas point.
    SetCuePoint {
        path: StmtPath,
        at: Vec2,
    },
    /// Sets the frames of a wait or a timed move (one undo step).
    SetCueFrames {
        path: StmtPath,
        frames: u32,
    },
    /// Swaps a statement with the next one or the previous one, keeping the section's end last.
    MoveCue {
        path: StmtPath,
        later: bool,
    },
    /// Adds a command at the playhead, to the ground under a canvas point when given.
    AddCue {
        kind: NewCue,
        at: Option<Vec2>,
    },
    /// Sets a dialogue line.
    SetText {
        key: String,
        text: String,
    },
    /// Shows the map before or after the one shown in the browser's order.
    OpenAdjacentMap {
        later: bool,
    },
    /// Drops the unsaved changes of the map shown (its document and drafts).
    Revert,

    // Saving.
    /// The pack saves go to, by its folder.
    SetTarget(Option<PathBuf>),
    /// Creates a mod pack in a mods folder, mounts it and makes it the target.
    CreateModPack {
        folder: PathBuf,
        id: String,
    },
    /// Saves the map into the target: as a patch, or as its whole data.
    Save(SaveMode),
    /// Whether a save makes a connected game read the map again.
    SetReloadOnSave(bool),
    /// The dwx program that makes `field/script_warps` again after a save changes map changes.
    SetDwxPath(Option<PathBuf>),
    /// Closes the editor (asks first when there are unsaved changes).
    Quit,
    /// Answers the unsaved-changes question about the action waiting for it.
    Resolve(PendingChoice),
}

/// The answer to "there are unsaved changes".
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PendingChoice {
    /// Saves as a patch, then goes on.
    Save,
    /// Drops the changes and goes on.
    Discard,
    /// Stays.
    Cancel,
}

impl Action {
    /// Whether the action leaves the map shown, so unsaved changes would be lost.
    pub fn leaves_map(&self) -> bool {
        matches!(
            self,
            Self::OpenContent(_)
                | Self::ReloadContent
                | Self::OpenMap(_)
                | Self::OpenMapAtExit { .. }
                | Self::Back
                | Self::OpenAdjacentMap { .. }
                | Self::Quit
        )
    }
}
