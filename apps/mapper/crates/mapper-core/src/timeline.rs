//! The open section as a timeline over the map (docs/modulos/apps/mapper.md#secuencias): the
//! model is [`dwe_story::sequence`]; this module places it on the map shown (where the player, the
//! partner and the NPCs stand when it starts), finds its markers on the canvas and turns the
//! timeline's gestures into section edits, which go through [`crate::StoryEdit`] like the section
//! editor's (one undo step each, saved as story patches).

use std::collections::BTreeMap;

use dwe_map::world;
use dwe_story::ast::{Block, Expr, Stmt, StmtKind};
use dwe_story::edit::{self, StmtPath};
use dwe_story::sequence::{
    self, PARTNER, PLAYER, Pose, Sequence, Stage, Track, frames_operand, point_operands,
};
use dwe_story::{Event, EventKind, StoryFile};
use glam::{Vec2, Vec3};

use crate::session::{Mapper, Message};
use crate::story::{StoryEdit, TestPlan};
use crate::text;

/// How far behind the player the partner starts, in world units.
const PARTNER_BEHIND: f32 = 150.0;
/// A new wait's frames, a new pan's.
const NEW_FRAMES: i64 = 20;

/// What the timeline keeps.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Timeline {
    pub shown: bool,
    /// The frame the canvas shows, in frames of the original.
    pub playhead: f32,
    /// The statement whose marker is dragged on the canvas.
    pub dragging: Option<StmtPath>,
}

/// A marker of the timeline on the canvas that a drag moves: where a walk, a slide or a pan goes.
#[derive(Clone, Debug, PartialEq)]
pub struct Handle {
    pub path: StmtPath,
    pub track: Track,
    /// Where it goes, in background pixels.
    pub at: Vec2,
}

/// The code "play from here" sends, and the statement of the section each of its lines copies.
#[derive(Clone, Debug, PartialEq)]
pub struct SentSection {
    pub script: u16,
    pub section: u16,
    /// A story patch of the script with the section (the control channel's `story_run` code).
    pub code: String,
    pub lines: BTreeMap<usize, StmtPath>,
}

/// The commands the timeline adds.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NewCue {
    /// `entity_walk_to` to the point (or where the character stands).
    Walk,
    /// `entity_set_rotation` to face the point.
    Turn,
    Delay,
    /// `move_camera_to` the point (or where the camera is).
    CameraTo,
    /// `move_camera_to_entity` the character.
    CameraToEntity,
    Animation,
    /// `show_textbox` with a new line.
    Text,
}

impl NewCue {
    pub const ALL: [Self; 7] = [
        Self::Walk,
        Self::Turn,
        Self::Delay,
        Self::CameraTo,
        Self::CameraToEntity,
        Self::Animation,
        Self::Text,
    ];

    pub fn label(self) -> &'static str {
        match self {
            Self::Walk => text::CUE_WALK,
            Self::Turn => text::CUE_TURN,
            Self::Delay => text::CUE_DELAY,
            Self::CameraTo => text::CUE_CAMERA,
            Self::CameraToEntity => text::CUE_CAMERA_ENTITY,
            Self::Animation => text::CUE_ANIMATION,
            Self::Text => text::CUE_TEXT,
        }
    }
}

impl Mapper {
    /// Where everyone stands when the open section starts: the NPCs where the map puts them, the
    /// player at the event that runs the section (its cell, or in front of its NPC; else the
    /// arrival of the map's first exit), the partner behind, the camera on the player.
    pub fn stage(&self) -> Stage {
        let mut stage = Stage::default();
        let Some(map) = &self.map else { return stage };
        let document = &map.document;
        for npc in &document.npcs {
            stage.poses.insert(
                i64::from(npc.script_id),
                Pose {
                    position: world::to_world(npc.position.into()),
                    yaw: world::yaw_from_original(npc.rotation.y),
                },
            );
        }
        let open = self.section.as_ref();
        let event = open.and_then(|open| {
            self.events()
                .iter()
                .find(|event| event.script == open.script && event.section == open.section)
        });
        let player = event
            .and_then(|event| match event.kind {
                dwe_story::EventKind::Talk { slot } => document.npcs.get(slot).map(|npc| {
                    let at = world::to_world(npc.position.into());
                    let ahead = world::forward_of_yaw(world::yaw_from_original(npc.rotation.y));
                    let player = at + ahead * PARTNER_BEHIND;
                    Pose {
                        position: player,
                        yaw: (at - player).x.atan2((at - player).z),
                    }
                }),
                _ => event
                    .centre_cell()
                    .map(|cell| Pose::at(world::cell_center(cell))),
            })
            .or_else(|| {
                document
                    .warps
                    .iter()
                    .find(|warp| warp.target_map_id.is_some())
                    .map(|warp| Pose {
                        position: world::to_world(warp.spawn.into()),
                        yaw: world::yaw_from_original(warp.rotation),
                    })
            })
            .unwrap_or(Pose::at(Vec3::ZERO));
        let behind = player.position - world::forward_of_yaw(player.yaw) * PARTNER_BEHIND;
        stage.poses.insert(PLAYER, player);
        stage.poses.insert(
            PARTNER,
            Pose {
                position: behind,
                yaw: player.yaw,
            },
        );
        stage.camera = player.position;
        stage
    }

    /// The timeline of the code the section editor shows, following the `if` arms to the line
    /// chosen.
    pub fn sequence(&self) -> Option<Sequence> {
        let code = self.open_code()?;
        let file = self.open_file()?;
        let routines = routines_of(file);
        let follow = self.section.as_ref()?.selected.as_ref();
        Some(Sequence::derive(code, &routines, &self.stage(), follow))
    }

    /// The markers a drag moves: where the walks, slides and pans of the section's own code go.
    pub fn handles(&self, sequence: &Sequence) -> Vec<Handle> {
        let Some(map) = &self.map else {
            return Vec::new();
        };
        sequence
            .cues
            .iter()
            .filter(|cue| cue.routine.is_none() && point_operands(&cue.command).is_some())
            .filter_map(|cue| {
                let (_, to) = cue.way?;
                let at = map.camera.project_to_background(to)?.pixel;
                Some(Handle {
                    path: cue.path.clone(),
                    track: cue.track,
                    at,
                })
            })
            .collect()
    }

    /// The marker under a canvas point: the chosen statement's, else a character's before the
    /// camera's (a pan often goes where a walk does), the latest first.
    pub(crate) fn handle_under(&self, at: Vec2) -> Option<StmtPath> {
        if !self.timeline.shown {
            return None;
        }
        let sequence = self.sequence()?;
        let mut under: Vec<Handle> = self
            .handles(&sequence)
            .into_iter()
            .filter(|handle| {
                self.view.to_canvas(handle.at).distance(at) <= crate::session::PICK_RADIUS
            })
            .collect();
        let selected = self
            .section
            .as_ref()
            .and_then(|open| open.selected.as_ref());
        under.reverse();
        under.sort_by_key(|handle| {
            (
                selected != Some(&handle.path),
                handle.track == Track::Camera,
            )
        });
        under.into_iter().next().map(|handle| handle.path)
    }

    /// Selects a statement from the timeline and puts the playhead at its start.
    pub(crate) fn select_cue(&mut self, path: StmtPath) {
        let start = self
            .sequence()
            .and_then(|sequence| sequence.cue_of(&path).map(|cue| cue.start));
        if let Some(open) = &mut self.section {
            open.selected = Some(path);
        }
        if let Some(start) = start {
            self.timeline.playhead = start as f32;
        }
    }

    /// The statement at a path of the code shown, as a story call to change.
    fn call_at(&self, path: &StmtPath) -> Option<Stmt> {
        let stmt = edit::stmt_at(self.open_code()?, path)?.clone();
        let mut probe = stmt.clone();
        edit::call_of_mut(&mut probe)?;
        Some(stmt)
    }

    /// Changes a call's operands (one undo step, or part of the drag's).
    fn edit_operands(&mut self, path: &StmtPath, change: impl FnOnce(&str, &mut Vec<Expr>)) {
        let Some(mut stmt) = self.call_at(path) else {
            return;
        };
        let before = stmt.clone();
        if let Some((name, arguments)) = edit::call_of_mut(&mut stmt) {
            let name = name.to_owned();
            change(&name, arguments);
        }
        if stmt != before {
            self.edit_story(StoryEdit::Replace {
                at: path.clone(),
                stmt,
            });
        }
    }

    /// Moves where a walk, a slide or a pan goes to the ground under a canvas point.
    pub(crate) fn set_cue_point(&mut self, path: &StmtPath, at: Vec2) {
        let Some(ground) = self.ground_at(at) else {
            return;
        };
        let (x, z) = sequence::original_point(ground);
        self.edit_operands(path, |name, arguments| {
            if let Some((ox, oz)) = point_operands(name)
                && oz < arguments.len()
            {
                arguments[ox] = Expr::Number(x);
                arguments[oz] = Expr::Number(z);
            }
        });
    }

    /// Sets the frames of a wait or a timed move.
    pub(crate) fn set_cue_frames(&mut self, path: &StmtPath, frames: u32) {
        let frames = i64::from(frames.max(1));
        self.edit_operands(path, |name, arguments| {
            if let Some(index) = frames_operand(name)
                && index < arguments.len()
            {
                arguments[index] = Expr::Number(frames);
            }
        });
    }

    /// Swaps a statement with its neighbour, keeping the section's end last.
    pub(crate) fn move_cue(&mut self, path: &StmtPath, later: bool) {
        let Some(code) = self.open_code() else { return };
        let Some(block) = edit::block_of(code, path) else {
            return;
        };
        let other = if later {
            path.index + 1
        } else {
            match path.index.checked_sub(1) {
                Some(index) => index,
                None => return,
            }
        };
        let ends = |index: usize| {
            block
                .get(index)
                .is_none_or(|stmt| matches!(stmt.kind, StmtKind::Return(_)))
        };
        if ends(path.index) || ends(other) {
            self.messages
                .push(Message::Error(text::CUE_KEEPS_END.to_owned()));
            return;
        }
        self.edit_story(StoryEdit::Move {
            at: path.clone(),
            later,
        });
    }

    /// Adds a command at the playhead: after the statement running then (at the top when none
    /// runs yet), for the character of the chosen statement (the player when it has none), to
    /// the ground under a canvas point when one is given.
    pub(crate) fn add_cue(&mut self, kind: NewCue, at: Option<Vec2>) {
        let Some(sequence) = self.sequence() else {
            return;
        };
        let Some(open) = self.section.clone() else {
            return;
        };
        let frame = self.timeline.playhead;
        let after = sequence
            .cues
            .iter()
            .filter(|cue| cue.routine.is_none())
            .rfind(|cue| cue.start as f32 <= frame)
            .map(|cue| cue.path.clone());
        let insert_at = match after {
            Some(mut path) => {
                let ends = self
                    .open_code()
                    .and_then(|code| edit::stmt_at(code, &path))
                    .is_some_and(|stmt| matches!(stmt.kind, StmtKind::Return(_)));
                if !ends {
                    path.index += 1;
                }
                path
            }
            None => StmtPath::top(0),
        };
        let who = open
            .selected
            .as_ref()
            .and_then(|path| sequence.cue_of(path))
            .and_then(|cue| match cue.track {
                Track::Actor(id) => Some(id),
                _ => None,
            })
            .unwrap_or(PLAYER);
        let pose = sequence.pose_at(Track::Actor(who), frame);
        let point = at
            .and_then(|at| self.ground_at(at))
            .or_else(|| pose.map(|pose| pose.position))
            .unwrap_or(Vec3::ZERO);
        let camera = at
            .and_then(|at| self.ground_at(at))
            .or_else(|| sequence.pose_at(Track::Camera, frame).map(|p| p.position))
            .unwrap_or(Vec3::ZERO);
        let (x, z) = sequence::original_point(point);
        let entity = entity_expr(who);
        let number = Expr::Number;
        let mut new_line = None;
        let call = match kind {
            NewCue::Walk => Expr::story_call(
                "entity_walk_to",
                vec![entity, number(x), number(z), number(0), number(0)],
            ),
            NewCue::Turn => {
                let facing = pose.map_or(0.0, |pose| {
                    let direction = point - pose.position;
                    direction.x.atan2(direction.z)
                });
                Expr::story_call(
                    "entity_set_rotation",
                    vec![entity, number(sequence::rotation_of(facing))],
                )
            }
            NewCue::Delay => Expr::story_call("delay", vec![number(NEW_FRAMES)]),
            NewCue::CameraTo => {
                let (cx, cz) = sequence::original_point(camera);
                Expr::story_call(
                    "move_camera_to",
                    vec![number(NEW_FRAMES), number(cx), number(cz)],
                )
            }
            NewCue::CameraToEntity => {
                Expr::story_call("move_camera_to_entity", vec![entity, number(NEW_FRAMES)])
            }
            NewCue::Animation => Expr::story_call("play_animation", vec![entity, number(0)]),
            NewCue::Text => {
                let key = self.free_text_key(open.script);
                new_line = Some(key.clone());
                Expr::story_call("show_textbox", vec![Expr::Str(key)])
            }
        };
        let hint = call
            .story_call_name()
            .and_then(|(name, _)| dwe_story::commands::command(name))
            .and_then(|command| command.hint());
        let stmt = Stmt {
            comment: hint,
            ..Stmt::new(StmtKind::Call(call))
        };
        if let Some(map) = &mut self.map {
            map.history.begin_group();
        }
        self.edit_story(StoryEdit::Insert {
            at: insert_at.clone(),
            stmt,
        });
        if let Some(key) = new_line {
            self.set_text(&key, text::CUE_NEW_LINE);
        }
        if let Some(map) = &mut self.map {
            map.history.end_group();
        }
        self.select_cue(insert_at);
    }

    /// The ground under a canvas point, in world space.
    fn ground_at(&self, at: Vec2) -> Option<Vec3> {
        let map = self.map.as_ref()?;
        map.camera
            .ground_at_background(self.view.to_background(at), 0.0)
    }

    /// What playing the open section in the game writes and where it puts the player first: the
    /// test plan of the map's event that runs it (or of the section alone when none does),
    /// without the confirm press, since `story_run` starts the section.
    pub fn play_plan(&self) -> Option<TestPlan> {
        let open = self.section.as_ref()?;
        let event = self
            .events()
            .iter()
            .find(|event| event.script == open.script && event.section == open.section)
            .cloned()
            .unwrap_or(Event {
                kind: EventKind::MapLoaded,
                script: open.script,
                section: open.section,
                cells: Vec::new(),
                exists: true,
                patched_by: None,
                jumps: Vec::new(),
            });
        let mut plan = self.test_plan(&event);
        plan.press = false;
        Some(plan)
    }

    /// The code "play from here" sends to start the open section at a statement of its own code,
    /// and where each of its lines comes from.
    pub fn play_from_here(&self, at: &StmtPath) -> Result<SentSection, String> {
        let open = self.section.as_ref().ok_or(text::NO_SECTION_OPEN)?;
        if open.routine.is_some() {
            return Err(text::PLAY_FROM_ROUTINE.to_owned());
        }
        let code = self.open_code().ok_or(text::NO_SECTION_OPEN)?;
        let file = self.open_file().ok_or(text::NO_SECTION_OPEN)?;
        let routines = routines_of(file);
        let sequence = Sequence::derive(code, &routines, &self.stage(), Some(at));
        let (body, origins) = sequence::play_from(code, at, &sequence);
        let mut patch = StoryFile::new_patch(open.script, text::PLAY_HEADER);
        patch
            .take_section(file, open.section)
            .map_err(|error| error.to_string())?;
        *patch
            .section_body_mut(open.section)
            .ok_or(text::NO_SECTION_OPEN)? = body;
        patch.drop_unreached_routines();
        patch.has_routines = !patch.routines.is_empty();
        let written = patch.write();
        let reread = StoryFile::parse(&written).map_err(|error| error.to_string())?;
        let mut lines = BTreeMap::new();
        if let Some(block) = reread.section_block(open.section) {
            for (stmt, origin) in block.iter().zip(&origins) {
                if let Some(origin) = origin {
                    map_lines(stmt, origin, &mut lines);
                }
            }
        }
        Ok(SentSection {
            script: open.script,
            section: open.section,
            code: written,
            lines,
        })
    }
}

/// The routines of a story file, by name.
pub fn routines_of<'f>(file: &'f StoryFile) -> impl Fn(&str) -> Option<&'f Block> + 'f {
    move |name: &str| file.routine(name).map(|routine| &routine.body)
}

/// `story.PLAYER`, `story.PARTNER` or an NPC's script id.
fn entity_expr(id: i64) -> Expr {
    match id {
        PLAYER => Expr::name("PLAYER"),
        PARTNER => Expr::name("PARTNER"),
        id => Expr::Number(id),
    }
}

/// Records the line of a statement copied from `origin`, and of the statements inside it.
fn map_lines(stmt: &Stmt, origin: &StmtPath, lines: &mut BTreeMap<usize, StmtPath>) {
    if stmt.line != 0 {
        lines.entry(stmt.line).or_insert_with(|| origin.clone());
    }
    for (block, body) in edit::children(stmt).into_iter().enumerate() {
        for (index, inner) in body.iter().enumerate() {
            map_lines(inner, &origin.child(block, index), lines);
        }
    }
}
