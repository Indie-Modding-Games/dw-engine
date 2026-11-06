//! A section as a timeline (docs/modulos/apps/mapper.md#secuencias): what each command of the
//! section does over time, on a track per character, one for the camera, the texts, the sound,
//! the waits and the rest of the story, so a scene can be seen and edited as it plays.
//!
//! The model runs the section like the interpreter (docs/architecture/interprete-historia.md
//! §Corrutinas, fotogramas y esperas), in frames of the original (20 a second): commands run one
//! after the other in the same frame until one waits. Moves (walks, slides, turns, the camera's
//! pans) do not wait: they fill the move slot of their character, or the camera's, and the section
//! goes on; `wait_for_entity` waits for slots to empty, `delay` for its frames, a text box for the
//! player. A move ordered on a busy slot replaces the one there.
//!
//! What it cannot know it estimates, and says so: a walk's length comes from its distance and the
//! walk or run speed of the character (the player's and the partner's from the original, an
//! NPC's as the player's), an animation lasts [`ANIMATION_FRAMES`], a text box the player pages
//! [`TEXT_FRAMES`]. An `if` follows one arm: the one with the statement asked for, else the one
//! with the most commands (falling through a guard that only ends the section). A loop runs its
//! body once; a routine of the file runs inline, its commands under the statement that calls it.

use std::collections::BTreeMap;
use std::f32::consts::{PI, TAU};

use dwe_map::world::{forward_of_yaw, from_original_axes, rotation_facing, yaw_from_original};
use glam::Vec3;

use crate::ast::{Block, Expr, Stmt, StmtKind, walk_exprs};
use crate::edit::{StmtPath, children};

/// Frames of the original a second.
pub const FRAMES_PER_SECOND: u32 = 20;
/// The script ids of the player and the partner (`story.PLAYER`, `story.PARTNER`).
pub const PLAYER: i64 = 0xFD;
pub const PARTNER: i64 = 0xFC;
/// How long the model gives a text box or a choice the player pages through: an estimate.
pub const TEXT_FRAMES: u32 = 40;
/// How long the model shows an animation it does not know the length of.
pub const ANIMATION_FRAMES: u32 = 20;
/// The fade out of a map change (map.c: scriptTickChangeMap), after which the section is over.
pub const MAP_CHANGE_FRAMES: u32 = 20;
/// The frames a battle or an engine routine (a menu, a scene) is shown with: an estimate.
pub const SCENE_FRAMES: u32 = 20;
/// `wait_for_entity`'s ids that are not characters (script_interp.c:1332-1393).
const WAIT_ALL: i64 = 0xFF;
const WAIT_CAMERA: i64 = 0xC8;
/// `entity_look_at_entity` turns 0x200 of 4096 a frame (overworld.c: rotateEntity).
const LOOK_TURN_PER_FRAME: f32 = TAU / 8.0;
/// How far short of a character a walk to it stops: a cell, an estimate of the touch test.
const TOUCH_DISTANCE: f32 = 100.0;
/// How deep routine calls are followed.
const MOST_ROUTINE_DEPTH: usize = 8;

/// How fast characters walk and run under a script, in world units a frame (100 a cell): the
/// root motion of their walk and run clips (content/packs/game/field/rules.yaml).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Speeds {
    pub player: [f32; 2],
    pub partner: [f32; 2],
    /// An NPC's: its model's clips, which the editor does not read; the player's, estimated.
    pub other: [f32; 2],
}

impl Default for Speeds {
    fn default() -> Self {
        Self {
            player: [20.0, 50.0],
            partner: [22.6, 43.5],
            other: [20.0, 50.0],
        }
    }
}

/// A row of the timeline.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Track {
    /// A character by its script id: the player, the partner or an NPC.
    Actor(i64),
    Camera,
    /// Text boxes, choices and who speaks.
    Text,
    Audio,
    /// Delays and waits for characters.
    Wait,
    /// Triggers, variables, items, map changes and every other command.
    Story,
}

/// How the section goes on after a command.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Hold {
    /// At once: a move or an instant change.
    Runs,
    /// After its frames.
    Frames,
    /// When the player pages the text or chooses.
    Player,
    /// When the slots it waits for are empty.
    Movement,
    /// Never: the section is over (a map change or a jump to another script).
    Ends,
}

/// Where a character stands and faces (world space, yaw in radians), or the camera's focus.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Pose {
    pub position: Vec3,
    pub yaw: f32,
}

impl Pose {
    pub fn at(position: Vec3) -> Self {
        Self { position, yaw: 0.0 }
    }
}

/// One command on the timeline.
#[derive(Clone, Debug, PartialEq)]
pub struct Cue {
    /// The statement of the section (a routine's commands are under the call that runs them).
    pub path: StmtPath,
    /// The line of the statement in the code it was read from (0 when edited, not read).
    pub line: usize,
    /// The story command (`entity_walk_to`...).
    pub command: String,
    pub track: Track,
    /// When it starts and how long it lasts, in frames.
    pub start: u32,
    pub frames: u32,
    pub hold: Hold,
    /// The length is a guess (see the module's notes), or an operand is not a number.
    pub estimated: bool,
    /// A later order on the same slot took over before it ended.
    pub cut: bool,
    /// The routine of the file it comes from.
    pub routine: Option<String>,
    /// A move's way, from and to (world space), for the canvas.
    pub way: Option<(Vec3, Vec3)>,
    /// The facing a turn ends with.
    pub facing: Option<f32>,
}

impl Cue {
    pub fn end(&self) -> u32 {
        self.start + self.frames
    }
}

/// A stretch of a character's (or the camera's) move between two poses.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Segment {
    pub track: Track,
    pub start: u32,
    pub frames: u32,
    pub from: Pose,
    pub to: Pose,
}

/// Where everyone stands when the section starts.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Stage {
    /// By script id: the player, the partner, the map's NPCs.
    pub poses: BTreeMap<i64, Pose>,
    /// The camera's focus.
    pub camera: Vec3,
    pub speeds: Speeds,
}

/// A section's timeline.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Sequence {
    pub cues: Vec<Cue>,
    pub segments: Vec<Segment>,
    /// The poses at frame 0 (the camera's under [`Track::Camera`]).
    pub start: BTreeMap<Track, Pose>,
    /// The last frame anything happens on.
    pub length: u32,
    /// Whether the section ends with a map change or a jump.
    pub ends: bool,
}

/// The routines a section's code may call, by name.
pub type Routines<'b> = dyn Fn(&str) -> Option<&'b Block> + 'b;

impl Sequence {
    /// The timeline of a section's code. `follow` picks the arms of the `if`s on the way to that
    /// statement.
    pub fn derive<'b>(
        body: &Block,
        routines: &Routines<'b>,
        stage: &Stage,
        follow: Option<&StmtPath>,
    ) -> Self {
        let mut walker = Walker {
            routines,
            speeds: stage.speeds,
            follow,
            frame: 0,
            poses: stage
                .poses
                .iter()
                .map(|(&id, &pose)| (Track::Actor(id), pose))
                .chain([(Track::Camera, Pose::at(stage.camera))])
                .collect(),
            busy: BTreeMap::new(),
            moving: BTreeMap::new(),
            sequence: Sequence::default(),
            depth: 0,
            routine: None,
        };
        walker.sequence.start = walker.poses.clone();
        walker.block(body, &StmtPath::default(), None);
        let mut sequence = walker.sequence;
        sequence.length = sequence
            .cues
            .iter()
            .map(Cue::end)
            .chain(sequence.segments.iter().map(|s| s.start + s.frames))
            .max()
            .unwrap_or(0);
        sequence
    }

    /// The rows the timeline shows: the player, the partner and the NPCs that do something, then
    /// the camera, the text, the sound, the waits and the rest, each once.
    pub fn tracks(&self) -> Vec<Track> {
        let mut tracks: Vec<Track> = self.cues.iter().map(|cue| cue.track).collect();
        tracks.sort_by_key(|track| match track {
            Track::Actor(PLAYER) => (0, 0),
            Track::Actor(PARTNER) => (1, 0),
            Track::Actor(id) => (2, *id),
            Track::Camera => (3, 0),
            Track::Text => (4, 0),
            Track::Audio => (5, 0),
            Track::Wait => (6, 0),
            Track::Story => (7, 0),
        });
        tracks.dedup();
        tracks
    }

    /// Where a character (or the camera) is at a frame, with the moves before it played.
    pub fn pose_at(&self, track: Track, frame: f32) -> Option<Pose> {
        let mut pose = *self.start.get(&track)?;
        for segment in self.segments.iter().filter(|s| s.track == track) {
            let start = segment.start as f32;
            if start > frame {
                break;
            }
            let span = segment.frames.max(1) as f32;
            let done = ((frame - start) / span).clamp(0.0, 1.0);
            pose = Pose {
                position: segment.from.position.lerp(segment.to.position, done),
                yaw: lerp_angle(segment.from.yaw, segment.to.yaw, done),
            };
        }
        Some(pose)
    }

    /// Every character with a pose (the camera left out).
    pub fn actors(&self) -> impl Iterator<Item = i64> + '_ {
        self.start.keys().filter_map(|track| match track {
            Track::Actor(id) => Some(*id),
            _ => None,
        })
    }

    /// The first cue of a statement.
    pub fn cue_of(&self, path: &StmtPath) -> Option<&Cue> {
        self.cues.iter().find(|cue| &cue.path == path)
    }

    /// The statement that runs at a frame: the last one started at or before it.
    pub fn path_at(&self, frame: u32) -> Option<&StmtPath> {
        self.cues
            .iter()
            .rfind(|cue| cue.start <= frame)
            .map(|cue| &cue.path)
    }

    /// The cue a running section stands at, from the lines its functions are at (innermost
    /// first, as `story_state` gives them): the first line a cue was read from.
    pub fn cue_at_lines(&self, lines: &[i64]) -> Option<&Cue> {
        lines.iter().find_map(|&line| {
            self.cues
                .iter()
                .rfind(|cue| cue.line != 0 && cue.line as i64 == line)
        })
    }
}

/// Turns from `from` to `to` the short way.
fn lerp_angle(from: f32, to: f32, done: f32) -> f32 {
    let delta = (to - from + PI).rem_euclid(TAU) - PI;
    from + delta * done
}

/// A number operand: a literal, `story.PLAYER`/`PLAYER`, `story.PARTNER`/`PARTNER`.
pub fn number(expr: &Expr) -> Option<i64> {
    match expr.unparen() {
        Expr::Number(value) => Some(*value),
        Expr::Name(name) => named(name),
        Expr::Field(object, name) if matches!(object.as_ref(), Expr::Name(n) if n == "story") => {
            named(name)
        }
        _ => None,
    }
}

fn named(name: &str) -> Option<i64> {
    match name {
        "PLAYER" => Some(PLAYER),
        "PARTNER" => Some(PARTNER),
        _ => None,
    }
}

/// A point of the original's ground (x, z) in world space.
pub fn ground_point(x: i64, z: i64) -> Vec3 {
    let world = from_original_axes(Vec3::new(x as f32, 0.0, z as f32));
    Vec3::new(world.x, 0.0, world.z)
}

/// The original's (x, z) of a world point.
pub fn original_point(world: Vec3) -> (i64, i64) {
    let original = from_original_axes(world);
    (original.x.round() as i64, original.z.round() as i64)
}

/// The operands a command moves to, as (x, z) positions in its operand list.
pub fn point_operands(command: &str) -> Option<(usize, usize)> {
    match command {
        "entity_walk_to"
        | "entity_walk_to_with_camera"
        | "entity_move_to"
        | "entity_move_to_with_camera"
        | "move_camera_to" => Some((1, 2)),
        _ => None,
    }
}

/// The operand that holds a command's length in frames, if it has one.
pub fn frames_operand(command: &str) -> Option<usize> {
    match command {
        "delay" | "move_camera_to" => Some(0),
        "move_camera_to_entity" => Some(1),
        "entity_move_to_entity" | "entity_move_to_entity_with_camera" => Some(2),
        "entity_move_to"
        | "entity_move_to_with_camera"
        | "entity_move_to_axis"
        | "entity_move_to_axis_with_camera" => Some(3),
        _ => None,
    }
}

/// Whether a command only changes the story's state at once (no time, no wait, no move), so
/// "play from here" runs it before the statement chosen.
pub fn sets_state(command: &str) -> bool {
    !matches!(
        command,
        "show_textbox"
            | "set_selection"
            | "fadeout_hud"
            | "delay"
            | "wait_for_entity"
            | "start_battle"
            | "call_routine"
            | "call_partner_routine"
            | "play_sound"
            | "play_animation"
            | "warp_to"
            | "teleport"
            | "jump_to_file"
            | "move_object_to"
            | "rotate_3d_object"
            | "move_boulder"
            | "spawn_sprite_at_entity"
            | "spawn_sprite_at_location"
    ) && !command.starts_with("entity_")
        && !command.starts_with("move_camera")
}

struct Walker<'r, 'a> {
    routines: &'r Routines<'a>,
    speeds: Speeds,
    follow: Option<&'r StmtPath>,
    frame: u32,
    poses: BTreeMap<Track, Pose>,
    /// When each slot's move ends.
    busy: BTreeMap<Track, u32>,
    /// The segment each slot moves on, to cut when another order takes the slot.
    moving: BTreeMap<Track, (usize, Option<usize>)>,
    sequence: Sequence,
    depth: usize,
    routine: Option<String>,
}

impl Walker<'_, '_> {
    /// Runs a block; `at` is the path of its first statement's block (`calls`: the statement
    /// all of it shows under, inside a routine). False when the section ended.
    fn block(&mut self, block: &Block, parent: &StmtPath, calls: Option<&StmtPath>) -> bool {
        for (index, stmt) in block.iter().enumerate() {
            let mut path = parent.clone();
            path.index = index;
            if !self.stmt(stmt, &path, calls) {
                return false;
            }
        }
        true
    }

    fn stmt(&mut self, stmt: &Stmt, path: &StmtPath, calls: Option<&StmtPath>) -> bool {
        let shown = calls.unwrap_or(path);
        match &stmt.kind {
            StmtKind::Call(call) => self.call(call, stmt.line, shown),
            StmtKind::Local { value, .. } | StmtKind::Assign { value, .. } => {
                // `local choice = story.set_selection(...)`.
                if value.story_call_name().is_some() {
                    self.call(value, stmt.line, shown)
                } else {
                    true
                }
            }
            StmtKind::Return(values) => {
                if let Some(call) = values.first()
                    && (call.story_call_name().is_some() || call.routine_call_name().is_some())
                {
                    self.call(call, stmt.line, shown);
                }
                false
            }
            StmtKind::If { arms, otherwise } => {
                let blocks = children(stmt);
                let chosen = self.arm(path, &blocks, otherwise.is_none(), arms.len());
                match chosen.and_then(|arm| blocks.get(arm).map(|block| (arm, *block))) {
                    Some((arm, block)) => self.block(block, &path.child(arm, 0), calls),
                    None => true,
                }
            }
            StmtKind::While { body, .. } | StmtKind::Do(body) => {
                self.block(body, &path.child(0, 0), calls)
            }
            StmtKind::Function { .. } | StmtKind::Break | StmtKind::Comment(_) => true,
        }
    }

    /// The arm of an `if` the timeline follows (None: the missing `else`).
    fn arm(&self, path: &StmtPath, blocks: &[&Block], no_else: bool, arms: usize) -> Option<usize> {
        if let Some(follow) = self.follow
            && self.routine.is_none()
            && let Some(&(index, arm)) = follow.blocks.get(path.blocks.len())
            && follow.blocks.starts_with(&path.blocks)
            && index == path.index
        {
            return Some(arm);
        }
        let count = |block: &Block| {
            let mut calls = 0;
            walk_exprs(block, &mut |expr| {
                calls += usize::from(expr.story_call_name().is_some());
            });
            calls
        };
        let mut best = no_else.then_some((0, arms));
        for (arm, block) in blocks.iter().enumerate() {
            let calls = count(block);
            if best.is_none_or(|(most, _)| calls > most || (calls == most && arm >= arms)) {
                best = Some((calls, arm));
            }
        }
        best.map(|(_, arm)| arm).filter(|&arm| arm < blocks.len())
    }

    fn call(&mut self, call: &Expr, line: usize, path: &StmtPath) -> bool {
        if let Some(name) = call.routine_call_name() {
            let Some(body) = (self.routines)(name) else {
                return true;
            };
            if self.depth >= MOST_ROUTINE_DEPTH {
                return true;
            }
            self.depth += 1;
            let outer = self.routine.replace(name.to_owned());
            let going = self.block(body, &StmtPath::default(), Some(path));
            self.routine = outer;
            self.depth -= 1;
            return going;
        }
        let Some((name, arguments)) = call.story_call_name() else {
            return true;
        };
        let operands: Vec<Option<i64>> = arguments.iter().map(number).collect();
        let known = operands.iter().all(Option::is_some);
        let arg = |index: usize| operands.get(index).copied().flatten();
        let (start, routine) = (self.frame, self.routine.clone());
        let cue = move |track: Track, frames: u32, hold: Hold| Cue {
            path: path.clone(),
            line,
            command: name.to_owned(),
            track,
            start,
            frames,
            hold,
            estimated: !known,
            cut: false,
            routine: routine.clone(),
            way: None,
            facing: None,
        };
        match name {
            "show_textbox" | "set_selection" => {
                let mut text = cue(Track::Text, TEXT_FRAMES, Hold::Player);
                text.estimated = true;
                self.push(text);
                self.frame += TEXT_FRAMES;
            }
            "set_dialog_owner" | "set_textbox_mode" | "set_textbox_size" | "set_textbox_origin" => {
                self.push(cue(Track::Text, 0, Hold::Runs));
            }
            "fadeout_hud" => {
                self.push(cue(Track::Text, 1, Hold::Frames));
                self.frame += 1;
            }
            "delay" => {
                let frames = arg(0).map_or(1, |n| n.clamp(1, i64::from(u32::MAX)) as u32);
                self.push(cue(Track::Wait, frames, Hold::Frames));
                self.frame += frames;
            }
            "wait_for_entity" => {
                let until = match arg(0) {
                    Some(WAIT_ALL) => self.busy.values().copied().max(),
                    Some(WAIT_CAMERA) => self.busy.get(&Track::Camera).copied(),
                    Some(id) => self.busy.get(&Track::Actor(id)).copied(),
                    None => None,
                }
                .unwrap_or(self.frame)
                .max(self.frame);
                self.push(cue(Track::Wait, until - self.frame, Hold::Movement));
                self.frame = until;
            }
            "play_sound" | "set_bgm" | "stop_bgm" => self.push(cue(Track::Audio, 0, Hold::Runs)),
            "start_battle" | "call_routine" | "call_partner_routine" => {
                let mut scene = cue(Track::Story, SCENE_FRAMES, Hold::Player);
                scene.estimated = true;
                self.push(scene);
                self.frame += SCENE_FRAMES;
            }
            "warp_to" | "teleport" | "jump_to_file" => {
                self.push(cue(Track::Story, MAP_CHANGE_FRAMES, Hold::Ends));
                self.sequence.ends = true;
                return false;
            }
            "play_animation" => {
                let Some(who) = arg(0) else {
                    self.push(cue(Track::Story, 0, Hold::Runs));
                    return true;
                };
                let mut animation = cue(Track::Actor(who), ANIMATION_FRAMES, Hold::Runs);
                animation.estimated = true;
                self.push(animation);
            }
            "entity_set_rotation" => self.turn(cue, arg(0), arg(1).map(yaw_of)),
            "entity_look_at_entity" => {
                let yaw = match (arg(0), arg(1)) {
                    (Some(who), Some(target)) => self.facing(who, target),
                    _ => None,
                };
                self.turn(cue, arg(0), yaw);
            }
            "entity_walk_to" | "entity_walk_to_with_camera" => {
                let to = arg(1).zip(arg(2)).map(|(x, z)| ground_point(x, z));
                let speed = arg(0).map(|who| self.speed(who, arg(3).unwrap_or(0)));
                self.go(
                    cue,
                    arg(0),
                    to,
                    Pace::Speed(speed),
                    name.ends_with("camera"),
                );
            }
            "entity_walk_to_entity" | "entity_walk_to_entity_with_camera" => {
                let to = arg(0)
                    .zip(arg(2))
                    .and_then(|(who, target)| self.near(who, target));
                let speed = arg(0).map(|who| self.speed(who, arg(1).unwrap_or(0)));
                self.go(
                    cue,
                    arg(0),
                    to,
                    Pace::Speed(speed),
                    name.ends_with("camera"),
                );
            }
            "entity_move_to" | "entity_move_to_with_camera" => {
                let to = arg(1).zip(arg(2)).map(|(x, z)| ground_point(x, z));
                self.go(
                    cue,
                    arg(0),
                    to,
                    Pace::Frames(arg(3)),
                    name.ends_with("camera"),
                );
            }
            "entity_move_to_entity" | "entity_move_to_entity_with_camera" => {
                let to = arg(1).and_then(|target| self.position(Track::Actor(target)));
                self.go(
                    cue,
                    arg(0),
                    to,
                    Pace::Frames(arg(2)),
                    name.ends_with("camera"),
                );
            }
            "entity_move_to_axis" | "entity_move_to_axis_with_camera" => {
                let to = arg(0)
                    .zip(arg(1))
                    .zip(arg(2))
                    .and_then(|((who, at), axis)| {
                        let mut point = self.position(Track::Actor(who))?;
                        match axis {
                            0 => point.x = at as f32,
                            1 => point.y = -(at as f32),
                            _ => point.z = -(at as f32),
                        }
                        Some(point)
                    });
                self.go(
                    cue,
                    arg(0),
                    to,
                    Pace::Frames(arg(3)),
                    name.ends_with("camera"),
                );
            }
            "move_camera_to" => {
                let to = arg(1).zip(arg(2)).map(|(x, z)| ground_point(x, z));
                self.pan(cue, to, arg(0));
            }
            "move_camera_to_entity" => {
                let to = arg(0).and_then(|who| self.position(Track::Actor(who)));
                self.pan(cue, to, arg(1));
            }
            _ => self.push(cue(Track::Story, 0, Hold::Runs)),
        }
        true
    }

    fn push(&mut self, cue: Cue) {
        self.sequence.cues.push(cue);
    }

    fn position(&self, track: Track) -> Option<Vec3> {
        self.poses.get(&track).map(|pose| pose.position)
    }

    /// The walk (animation 0) or run speed of a character, a frame.
    fn speed(&self, who: i64, animation: i64) -> f32 {
        let pair = match who {
            PLAYER => self.speeds.player,
            PARTNER => self.speeds.partner,
            _ => self.speeds.other,
        };
        pair[usize::from(animation != 0)]
    }

    /// The yaw `who` faces `target` with.
    fn facing(&self, who: i64, target: i64) -> Option<f32> {
        let from = self.position(Track::Actor(who))?;
        let to = self.position(Track::Actor(target))?;
        let direction = to - from;
        (direction.length_squared() > 1.0).then(|| direction.x.atan2(direction.z))
    }

    /// Where a walk to a character stops: a touch short of it.
    fn near(&self, who: i64, target: i64) -> Option<Vec3> {
        let from = self.position(Track::Actor(who))?;
        let to = self.position(Track::Actor(target))?;
        let gap = to - from;
        let length = gap.length();
        Some(if length <= TOUCH_DISTANCE {
            from
        } else {
            from + gap * ((length - TOUCH_DISTANCE) / length)
        })
    }

    /// Fills a slot: a move there still going ends now where it got to.
    fn take_slot(&mut self, track: Track) {
        if let Some(&end) = self.busy.get(&track)
            && end > self.frame
            && let Some((cue, segment)) = self.moving.remove(&track)
        {
            let frame = self.frame as f32;
            let stopped = self.sequence.pose_at(track, frame);
            if let Some(index) = segment {
                let part = &mut self.sequence.segments[index];
                part.frames = self.frame - part.start;
                if let Some(pose) = stopped {
                    part.to = pose;
                }
            }
            let cut = &mut self.sequence.cues[cue];
            cut.frames = self.frame - cut.start;
            cut.cut = true;
            if let (Some(pose), Some((from, _))) = (stopped, cut.way) {
                cut.way = Some((from, pose.position));
            }
            if let Some(pose) = stopped {
                self.poses.insert(track, pose);
            }
        }
    }

    /// Records a move of a slot from its pose to `to` over `frames`.
    fn moves(&mut self, track: Track, mut cue: Cue, to: Pose, frames: u32) {
        self.take_slot(track);
        let from = self.poses.get(&track).copied().unwrap_or(to);
        cue.frames = frames;
        let segment = self.sequence.segments.len();
        self.sequence.segments.push(Segment {
            track,
            start: self.frame,
            frames,
            from,
            to,
        });
        self.moving
            .insert(track, (self.sequence.cues.len(), Some(segment)));
        self.push(cue);
        self.busy.insert(track, self.frame + frames);
        self.poses.insert(track, to);
    }

    fn turn(&mut self, cue: impl Fn(Track, u32, Hold) -> Cue, who: Option<i64>, yaw: Option<f32>) {
        let Some(who) = who else {
            self.push(cue(Track::Story, 0, Hold::Runs));
            return;
        };
        let track = Track::Actor(who);
        self.take_slot(track);
        let Some(yaw) = yaw else {
            let mut unknown = cue(track, 1, Hold::Runs);
            unknown.estimated = true;
            self.push(unknown);
            return;
        };
        let from = self.poses.get(&track).copied();
        let turned = from.map_or(0.0, |pose| lerp_angle(pose.yaw, yaw, 1.0) - pose.yaw);
        let frames = ((turned.abs() / LOOK_TURN_PER_FRAME).ceil() as u32).max(1);
        let mut turn = cue(track, frames, Hold::Runs);
        turn.facing = Some(yaw);
        let to = Pose {
            position: from.map_or(Vec3::ZERO, |pose| pose.position),
            yaw,
        };
        if from.is_none() {
            self.poses.insert(track, to);
        }
        self.moves(track, turn, to, frames);
    }

    fn go(
        &mut self,
        cue: impl Fn(Track, u32, Hold) -> Cue,
        who: Option<i64>,
        to: Option<Vec3>,
        pace: Pace,
        camera: bool,
    ) {
        let Some(who) = who else {
            self.push(cue(Track::Story, 0, Hold::Runs));
            return;
        };
        let track = Track::Actor(who);
        self.take_slot(track);
        let from = self.position(track);
        let (Some(from), Some(to)) = (from, to) else {
            let mut unknown = cue(track, 1, Hold::Runs);
            unknown.estimated = true;
            self.push(unknown);
            return;
        };
        let to = Vec3::new(to.x, from.y, to.z);
        let distance = (to - from).length();
        let (frames, estimated) = match pace {
            Pace::Speed(Some(speed)) if speed > 0.0 => (
                (distance / speed).ceil().max(1.0) as u32,
                who != PLAYER && who != PARTNER,
            ),
            Pace::Speed(_) => (1, true),
            Pace::Frames(Some(frames)) => (frames.clamp(1, i64::from(u32::MAX)) as u32, false),
            Pace::Frames(None) => (1, true),
        };
        let mut moved = cue(track, frames, Hold::Runs);
        moved.estimated |= estimated;
        moved.way = Some((from, to));
        let yaw = if distance > 1.0 {
            let direction = to - from;
            direction.x.atan2(direction.z)
        } else {
            self.poses.get(&track).map_or(0.0, |pose| pose.yaw)
        };
        moved.facing = Some(yaw);
        // A walker faces where it goes at once (as the walk tick turns it).
        if let Some(pose) = self.poses.get_mut(&track) {
            pose.yaw = yaw;
        }
        self.moves(track, moved, Pose { position: to, yaw }, frames);
        if camera && let Some(focus) = self.position(Track::Camera) {
            // The camera moves by the mover's steps (as the walk tick drags it).
            let shifted = Pose::at(focus + (to - from));
            self.take_slot(Track::Camera);
            let segment = Segment {
                track: Track::Camera,
                start: self.frame,
                frames,
                from: Pose::at(focus),
                to: shifted,
            };
            self.sequence.segments.push(segment);
            self.poses.insert(Track::Camera, shifted);
        }
    }

    fn pan(
        &mut self,
        cue: impl Fn(Track, u32, Hold) -> Cue,
        to: Option<Vec3>,
        frames: Option<i64>,
    ) {
        let mut pan = cue(Track::Camera, 1, Hold::Runs);
        self.take_slot(Track::Camera);
        let (Some(to), Some(from)) = (to, self.position(Track::Camera)) else {
            pan.estimated = true;
            self.push(pan);
            return;
        };
        let frames = frames.map_or(1, |n| n.clamp(1, i64::from(u32::MAX)) as u32);
        pan.way = Some((from, to));
        self.moves(Track::Camera, pan, Pose::at(to), frames);
    }
}

/// How long a move takes: at a speed (units a frame) or in a count of frames.
#[derive(Clone, Copy)]
enum Pace {
    Speed(Option<f32>),
    Frames(Option<i64>),
}

/// The yaw of the original's rotation operand.
fn yaw_of(rotation: i64) -> f32 {
    yaw_from_original((rotation & 0xFFF) as i32)
}

/// The original's rotation operand of a yaw.
pub fn rotation_of(yaw: f32) -> i64 {
    i64::from(rotation_facing(forward_of_yaw(yaw)))
}

/// The code "play from here" sends for a section (the control channel's `story_run` with code):
/// first where everyone stands when `at` starts (moves of one frame, the facings and the camera,
/// as the timeline has them), then the statements before `at` that only set the story's state
/// (triggers, variables, models, NPCs...; their waits and moves dropped), then `at` and what
/// follows it in its block and in each block around it. Each top-level statement of the result
/// says the statement of the section it copies (None for the placing ones).
pub fn play_from(
    body: &Block,
    at: &StmtPath,
    sequence: &Sequence,
) -> (Block, Vec<Option<StmtPath>>) {
    let mut out = Vec::new();
    let mut origins = Vec::new();
    if let Some(start) = sequence.cue_of(at).map(|cue| cue.start) {
        for stmt in placing(sequence, start) {
            out.push(stmt);
            origins.push(None);
        }
    }
    // The blocks around `at`, outermost first, with the statement of each that holds the next.
    let mut levels: Vec<(StmtPath, &Block)> = Vec::new();
    let mut block = body;
    let mut prefix = Vec::new();
    for &(index, child) in &at.blocks {
        levels.push((
            StmtPath {
                blocks: prefix.clone(),
                index,
            },
            block,
        ));
        let Some(next) = block
            .get(index)
            .and_then(|stmt| children(stmt).get(child).copied())
        else {
            return (out, origins);
        };
        prefix.push((index, child));
        block = next;
    }
    let parent = StmtPath {
        blocks: prefix,
        index: 0,
    };
    // Before `at`: the state the statements set, outermost block first.
    for (path, block) in &levels {
        for (index, stmt) in block.iter().enumerate().take(path.index) {
            if let Some(kept) = state_only(stmt) {
                out.push(kept);
                origins.push(Some(StmtPath {
                    index,
                    ..path.clone()
                }));
            }
        }
    }
    for (index, stmt) in block.iter().enumerate().take(at.index) {
        if let Some(kept) = state_only(stmt) {
            out.push(kept);
            origins.push(Some(StmtPath {
                index,
                ..parent.clone()
            }));
        }
    }
    // From `at` on, innermost block first; a loop around it runs again whole.
    for (index, stmt) in block.iter().enumerate().skip(at.index) {
        out.push(stmt.clone());
        origins.push(Some(StmtPath {
            index,
            ..parent.clone()
        }));
    }
    for (path, block) in levels.iter().rev() {
        let around = &block[path.index];
        let first = if matches!(around.kind, StmtKind::While { .. }) {
            path.index
        } else {
            path.index + 1
        };
        for (index, stmt) in block.iter().enumerate().skip(first) {
            out.push(stmt.clone());
            origins.push(Some(StmtPath {
                index,
                ..path.clone()
            }));
        }
    }
    (out, origins)
}

/// The statements that put everyone where the timeline has them at a frame.
fn placing(sequence: &Sequence, frame: u32) -> Vec<Stmt> {
    let call = |name: &str, operands: Vec<i64>| {
        Stmt::new(StmtKind::Call(Expr::story_call(
            name,
            operands.into_iter().map(Expr::Number).collect(),
        )))
    };
    let entity = |id: i64| match id {
        PLAYER => Expr::field(Expr::name("story"), "PLAYER"),
        PARTNER => Expr::field(Expr::name("story"), "PARTNER"),
        id => Expr::Number(id),
    };
    let moved: Vec<Track> = sequence
        .segments
        .iter()
        .filter(|segment| segment.start < frame)
        .map(|segment| segment.track)
        .collect();
    let mut stmts = Vec::new();
    let mut turns = Vec::new();
    for id in sequence.actors() {
        let track = Track::Actor(id);
        if !moved.contains(&track) {
            continue;
        }
        let Some(pose) = sequence.pose_at(track, frame as f32) else {
            continue;
        };
        let (x, z) = original_point(pose.position);
        stmts.push(Stmt::new(StmtKind::Call(Expr::story_call(
            "entity_move_to",
            vec![
                entity(id),
                Expr::Number(x),
                Expr::Number(z),
                Expr::Number(1),
                Expr::Number(0),
            ],
        ))));
        turns.push(Stmt::new(StmtKind::Call(Expr::story_call(
            "entity_set_rotation",
            vec![entity(id), Expr::Number(rotation_of(pose.yaw))],
        ))));
    }
    if moved.contains(&Track::Camera)
        && let Some(pose) = sequence.pose_at(Track::Camera, frame as f32)
    {
        let (x, z) = original_point(pose.position);
        stmts.push(call("move_camera_to", vec![1, x, z]));
    }
    if stmts.is_empty() {
        return stmts;
    }
    // A turn fills the same slot as a move: it waits for the moves to end first.
    stmts.push(call("wait_for_entity", vec![WAIT_ALL]));
    if !turns.is_empty() {
        stmts.extend(turns);
        stmts.push(call("wait_for_entity", vec![WAIT_ALL]));
    }
    stmts
}

/// A statement before "play from here"'s start as it runs there: only what sets the story's
/// state (None when nothing is left). A choice keeps its local with the first option.
fn state_only(stmt: &Stmt) -> Option<Stmt> {
    let mut kept = stmt.clone();
    match &mut kept.kind {
        StmtKind::Call(call) => match call.story_call_name() {
            Some((name, _)) if sets_state(name) => Some(kept),
            _ => None,
        },
        StmtKind::Local { value, .. } | StmtKind::Assign { value, .. } => {
            if let Some((name, _)) = value.story_call_name()
                && !sets_state(name)
            {
                *value = Expr::Number(0);
            }
            Some(kept)
        }
        StmtKind::If { arms, otherwise } => {
            for (_, body) in arms.iter_mut() {
                *body = body.iter().filter_map(state_only).collect();
            }
            if let Some(body) = otherwise {
                *body = body.iter().filter_map(state_only).collect();
            }
            Some(kept)
        }
        StmtKind::Do(body) => {
            *body = body.iter().filter_map(state_only).collect();
            Some(kept)
        }
        // A guard keeps its section's ends; routines and loops would run their waits again.
        StmtKind::Return(_) | StmtKind::Comment(_) => Some(kept),
        StmtKind::While { .. } | StmtKind::Function { .. } | StmtKind::Break => None,
    }
}
