//! What the game answers, read into types: the fields as apps/game/src/remote_control.cpp writes
//! them. A field the game leaves out takes its default, so an older game still answers.

use serde::Deserialize;
use serde_json::{Map, Value};

/// What `status` says about the game.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct GameStatus {
    pub tick: u64,
    /// Whether the logic is paused (the channel's `pause`).
    pub paused: bool,
    /// title, movie, interview, field or battle.
    pub mode: String,
    /// The map of the game being played; the game sends an empty one before a game starts.
    pub map: Option<String>,
    /// Whether the game's map debug overlay is on; None from a game that does not say.
    pub overlay: Option<bool>,
}

impl GameStatus {
    pub(crate) fn read(answer: &Map<String, Value>) -> Self {
        Self {
            tick: answer.get("tick").and_then(Value::as_u64).unwrap_or(0),
            paused: answer
                .get("paused")
                .and_then(Value::as_bool)
                .unwrap_or(false),
            mode: answer
                .get("mode")
                .and_then(Value::as_str)
                .unwrap_or_default()
                .to_owned(),
            map: non_empty(answer.get("map")),
            overlay: answer.get("overlay").and_then(Value::as_bool),
        }
    }
}

/// What `reload_map` did: the map entered again and the exit it was entered by, None when the
/// player kept their place.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ReloadedMap {
    pub map: String,
    pub exit: Option<i32>,
}

/// A section of a story script that runs.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Section {
    pub script: u32,
    pub section: u32,
    /// The lines of its code it is at, innermost call first (empty when the game does not say).
    pub lines: Vec<i64>,
}

/// The parts of `story_state` the editor shows.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct StoryState {
    pub tick: u64,
    /// The map of the story, if any.
    pub map: Option<String>,
    /// The triggers on, lowest first.
    pub triggers: Vec<u32>,
    /// The variables that are not 0, by number.
    pub variables: Vec<(u32, i64)>,
    /// A story script runs or is about to.
    pub busy: bool,
    /// The map's script, as set_script set it up.
    pub map_script: Option<u32>,
    /// The sections running now, the first requested first.
    pub sections: Vec<Section>,
    /// The story lock's stage, if any.
    pub stage: Option<String>,
}

impl StoryState {
    pub(crate) fn read(answer: &Map<String, Value>) -> Self {
        let story = answer.get("story").and_then(Value::as_object);
        let field = |key: &str| story.and_then(|story| story.get(key));
        let triggers = field("triggers")
            .and_then(Value::as_array)
            .map(|triggers| {
                triggers
                    .iter()
                    .filter_map(|trigger| u32::try_from(trigger.as_u64()?).ok())
                    .collect()
            })
            .unwrap_or_default();
        let mut variables: Vec<(u32, i64)> = field("variables")
            .and_then(Value::as_object)
            .map(|variables| {
                variables
                    .iter()
                    .filter_map(|(number, value)| Some((number.parse().ok()?, value.as_i64()?)))
                    .collect()
            })
            .unwrap_or_default();
        variables.sort_unstable();
        let script = field("script").and_then(Value::as_object);
        let sections = script
            .and_then(|script| script.get("sections"))
            .and_then(Value::as_array)
            .map(|sections| {
                sections
                    .iter()
                    .filter_map(|section| {
                        Some(Section {
                            script: u32::try_from(section.get("script")?.as_u64()?).ok()?,
                            section: u32::try_from(section.get("section")?.as_u64()?).ok()?,
                            lines: section
                                .get("lines")
                                .and_then(Value::as_array)
                                .map(|lines| lines.iter().filter_map(Value::as_i64).collect())
                                .unwrap_or_default(),
                        })
                    })
                    .collect()
            })
            .unwrap_or_default();
        Self {
            tick: answer.get("tick").and_then(Value::as_u64).unwrap_or(0),
            map: non_empty(field("map")),
            triggers,
            variables,
            busy: script
                .and_then(|script| script.get("busy"))
                .and_then(Value::as_bool)
                .unwrap_or(false),
            map_script: script
                .and_then(|script| script.get("map_script"))
                .and_then(Value::as_u64)
                .and_then(|number| u32::try_from(number).ok()),
            sections,
            stage: non_empty(field("stage")),
        }
    }
}

/// A character as `live` gives it: in world space (docs/architecture/world-space.md), its yaw in
/// radians as the engine keeps it, facing `(sin yaw, cos yaw)` on the ground's (x, z).
#[derive(Clone, Copy, Debug, Default, PartialEq, Deserialize)]
#[serde(default)]
pub struct Actor {
    /// An NPC's script id; None for the player and the partner.
    pub id: Option<i32>,
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub yaw: f32,
}

impl Actor {
    /// The ground direction it faces, (x, z).
    pub fn facing(&self) -> [f32; 2] {
        [self.yaw.sin(), self.yaw.cos()]
    }
}

/// The background pixels the game's window shows: its top-left corner and size.
#[derive(Clone, Copy, Debug, Default, PartialEq, Deserialize)]
#[serde(default)]
pub struct ViewRect {
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
}

/// What `live` says: the game's state and where its characters are, cheap enough to ask several
/// times a second.
#[derive(Clone, Debug, Default, PartialEq, Deserialize)]
#[serde(default)]
pub struct LiveView {
    pub tick: u64,
    pub paused: bool,
    /// title, movie, interview, field or battle.
    pub mode: String,
    /// The map being played; empty before a game starts.
    pub map: String,
    /// A story script or section runs (the game refuses map edits then).
    pub busy: bool,
    /// The map with a preview on, if any.
    pub preview: Option<String>,
    /// The number of the newest warning or error `log_tail` keeps (0 for none).
    pub log: u64,
    pub player: Option<Actor>,
    pub partner: Option<Actor>,
    pub npcs: Vec<Actor>,
    pub view: Option<ViewRect>,
}

impl LiveView {
    /// The map being played, if a game is.
    pub fn map(&self) -> Option<&str> {
        Some(self.map.as_str()).filter(|map| !map.is_empty())
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum LogLevel {
    #[default]
    Warning,
    Error,
}

/// A warning or an error of the game's log, as `log_tail` keeps them.
#[derive(Clone, Debug, Default, PartialEq, Eq, Deserialize)]
#[serde(default)]
pub struct LogEntry {
    pub seq: u64,
    pub level: LogLevel,
    pub text: String,
}

/// How `preview_map` showed the edit: by changing the collision in place, or by entering the map
/// again.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PreviewApplied {
    Collision,
    Reentered,
}

/// A write to the story's state (`story_set`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StoryWrite {
    Trigger { number: u32, on: bool },
    Variable { number: u32, value: u8 },
}

/// A text field that is there and not empty.
pub(crate) fn non_empty(value: Option<&Value>) -> Option<String> {
    value
        .and_then(Value::as_str)
        .filter(|text| !text.is_empty())
        .map(str::to_owned)
}

/// A section to start (`story_run`), and the code to run in its place once, if any.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StoryRun {
    pub script: u16,
    pub section: u16,
    pub code: Option<String>,
}
