//! The open section as a timeline (docs/modulos/apps/mapper.md#secuencias): a panel under the map
//! with a row per character, the camera, the text, the sound, the waits and the rest, a block per
//! command from its start for its length, and a playhead; over the map, the walks with their
//! steps, the turns, the camera's points and everyone where the playhead has them. The model is
//! `dwe_story::sequence`, placed on the map by `mapper_core::timeline`; every change goes to the
//! model as an Action (one undo step each).
//!
//! With a game connected it plays the section ("Reproducir", or "Reproducir desde aquí" with the
//! code `Mapper::play_from_here` writes), steps it, follows the command the game runs and shows how
//! far the game's characters are from where the model has them.

use std::time::{Duration, Instant};

use dwe_story::edit::StmtPath;
use dwe_story::sequence::{
    Cue, FRAMES_PER_SECOND, Hold, PARTNER, PLAYER, Sequence, Track, frames_operand,
};
use eframe::egui::{self, Align2, Color32, FontId, Pos2, Rect, Sense, Shape, Stroke, StrokeKind};
use glam::{Vec2, Vec3};
use mapper_core::{Action, Mapper, Message, NewCue, SentSection, text};
use mapper_live::{Command, LiveView, StoryRun};

use crate::app::MapperApp;
use crate::canvas::colors;

/// The height of a row and of the ruler, in points.
const ROW: f32 = 22.0;
const RULER: f32 = 18.0;
/// The width of the rows' names.
const NAMES: f32 = 96.0;
/// How near a block's right edge a drag changes its frames, in points.
const EDGE_GRAB: f32 = 5.0;
/// How often the story is read while the timeline follows the game.
const READ_EVERY: Duration = Duration::from_millis(250);
/// Game ticks per frame of the original (60 ticks a second, 20 frames).
const TICKS_PER_FRAME: u64 = 3;
/// The length of a facing arrow on the map, in world units.
const FACING: f32 = 80.0;

/// What the panel keeps between frames.
#[derive(Debug)]
pub struct TimelineUi {
    /// Points per frame.
    zoom: f32,
    /// The block whose right edge is dragged, and the frames it shows meanwhile.
    edge: Option<(StmtPath, u32)>,
    /// The section played in the game, and the code sent for it ("desde aquí").
    played: Option<(u16, u16)>,
    sent: Option<SentSection>,
    /// The playhead goes where the game is.
    follow: bool,
    next_read: Option<Instant>,
    /// The statement chosen last frame, to move the playhead when the section editor picks one.
    last_selected: Option<StmtPath>,
    /// The statement the game runs, when it runs the open section.
    game_at: Option<StmtPath>,
}

impl Default for TimelineUi {
    fn default() -> Self {
        Self {
            zoom: 6.0,
            edge: None,
            played: None,
            sent: None,
            follow: false,
            next_read: None,
            last_selected: None,
            game_at: None,
        }
    }
}

/// A row's colour.
fn track_color(track: Track) -> Color32 {
    match track {
        Track::Actor(PLAYER) => colors::LIVE_PLAYER,
        Track::Actor(PARTNER) => colors::LIVE_PARTNER,
        Track::Actor(_) => colors::NPC,
        Track::Camera => Color32::from_rgb(120, 160, 255),
        Track::Text => Color32::from_rgb(140, 220, 140),
        Track::Audio => Color32::from_rgb(200, 150, 255),
        Track::Wait => Color32::from_gray(170),
        Track::Story => Color32::from_gray(120),
    }
}

/// What a block's tooltip says.
fn cue_hint(cue: &Cue) -> String {
    let hold = match cue.hold {
        Hold::Runs => text::HOLD_RUNS,
        Hold::Frames => text::HOLD_FRAMES,
        Hold::Player => text::HOLD_PLAYER,
        Hold::Movement => text::HOLD_MOVEMENT,
        Hold::Ends => text::HOLD_ENDS,
    };
    let mut lines = vec![
        cue.command.clone(),
        format!("{} → {}", cue.start, text::frames(cue.frames)),
        hold.to_owned(),
    ];
    if cue.estimated {
        lines.push(text::CUE_ESTIMATED.to_owned());
    }
    if cue.cut {
        lines.push(text::CUE_CUT.to_owned());
    }
    if let Some(routine) = &cue.routine {
        lines.push(format!("{} {routine}", text::CUE_FROM_ROUTINE));
    }
    if cue.routine.is_none() && frames_operand(&cue.command).is_some() {
        lines.push(text::CUE_EDGE_HINT.to_owned());
    }
    lines.join("\n")
}

/// The panel under the map, while a section is open and its timeline shown.
pub fn panel(app: &mut MapperApp, ui: &mut egui::Ui) {
    if !app.mapper.timeline.shown || app.mapper.section.is_none() {
        return;
    }
    let Some(sequence) = app.mapper.sequence() else {
        return;
    };
    let mut actions = Vec::new();
    let mut play = None;
    egui::Panel::bottom("timeline")
        .resizable(true)
        .default_size(250.0)
        .min_size(120.0)
        .show(ui, |ui| {
            header(app, ui, &sequence, &mut actions, &mut play);
            ui.separator();
            egui::ScrollArea::vertical()
                .id_salt("timeline_rows")
                .show(ui, |ui| {
                    ui.horizontal_top(|ui| {
                        names(ui, &sequence);
                        egui::ScrollArea::horizontal()
                            .id_salt("timeline_lanes")
                            .show(ui, |ui| lanes(app, ui, &sequence, &mut actions));
                    });
                });
        });
    for action in actions {
        app.apply(action);
    }
    if let Some(from_here) = play {
        start_play(app, &sequence, from_here);
    }
}

/// The buttons over the rows: the playhead, zoom, play and step, reorder and add.
fn header(
    app: &mut MapperApp,
    ui: &mut egui::Ui,
    sequence: &Sequence,
    actions: &mut Vec<Action>,
    play: &mut Option<bool>,
) {
    let connected = app.live.is_connected();
    let paused = app.live.view.as_ref().is_some_and(|view| view.paused);
    let state = &mut app.timeline_ui;
    let selected = app
        .mapper
        .section
        .as_ref()
        .and_then(|open| open.selected.clone());
    ui.horizontal_wrapped(|ui| {
        ui.strong(text::TIMELINE);
        let playhead = app.mapper.timeline.playhead;
        ui.monospace(format!(
            "{:.0} / {}",
            playhead,
            text::frames(sequence.length)
        ));
        ui.separator();
        ui.label(text::TIMELINE_ZOOM);
        ui.add(
            egui::Slider::new(&mut state.zoom, 1.0..=40.0)
                .logarithmic(true)
                .show_value(false),
        );
        ui.separator();
        if ui
            .add_enabled(connected, egui::Button::new(text::PLAY))
            .on_hover_text(text::PLAY_HINT)
            .on_disabled_hover_text(text::PLAY_OFFLINE)
            .clicked()
        {
            *play = Some(false);
        }
        if ui
            .add_enabled(connected, egui::Button::new(text::PLAY_FROM_HERE))
            .on_hover_text(text::PLAY_FROM_HERE_HINT)
            .on_disabled_hover_text(text::PLAY_OFFLINE)
            .clicked()
        {
            *play = Some(true);
        }
        let live = &app.live;
        if paused {
            if ui
                .add_enabled(connected, egui::Button::new(text::LIVE_RESUME))
                .clicked()
            {
                live.send(Command::Resume);
            }
        } else if ui
            .add_enabled(connected, egui::Button::new(text::LIVE_PAUSE))
            .clicked()
        {
            live.send(Command::Pause);
        }
        if ui
            .add_enabled(connected && paused, egui::Button::new(text::TIMELINE_FRAME))
            .on_hover_text(text::TIMELINE_FRAME_HINT)
            .clicked()
        {
            live.send(Command::Advance(TICKS_PER_FRAME));
            live.send(Command::ReadStory);
        }
        if ui
            .add_enabled(
                connected && paused,
                egui::Button::new(text::LIVE_STEP_SECOND),
            )
            .clicked()
        {
            live.send(Command::Advance(60));
            live.send(Command::ReadStory);
        }
        ui.add_enabled(
            connected,
            egui::Checkbox::new(&mut state.follow, text::TIMELINE_FOLLOW),
        )
        .on_hover_text(text::TIMELINE_FOLLOW_HINT);
        if connected && state.played.is_some() {
            match &state.game_at {
                Some(at) => ui.weak(format!("{} {}", text::TIMELINE_GAME_AT, path_text(at))),
                None => ui.weak(text::TIMELINE_GAME_IDLE),
            };
        }
    });
    ui.horizontal_wrapped(|ui| {
        if let Some(path) = &selected {
            ui.monospace(path_text(path));
            for (label, later) in [(text::CUE_EARLIER, false), (text::CUE_LATER, true)] {
                if ui
                    .button(label)
                    .on_hover_text(text::CUE_MOVE_HINT)
                    .clicked()
                {
                    actions.push(Action::MoveCue {
                        path: path.clone(),
                        later,
                    });
                }
            }
            ui.separator();
        }
        ui.label(text::TIMELINE_ADD)
            .on_hover_text(text::TIMELINE_ADD_HINT);
        for kind in NewCue::ALL {
            if ui.small_button(kind.label()).clicked() {
                actions.push(Action::AddCue { kind, at: None });
            }
        }
        let drift = drift(&app.mapper, app.live.shows(&app.mapper), sequence);
        if !drift.is_empty() {
            ui.separator();
            let words: Vec<String> = drift
                .iter()
                .map(|(track, units, ..)| text::drift(&text::track_name(*track), *units))
                .collect();
            ui.label(format!("{}: {}", text::DRIFT, words.join(" · ")))
                .on_hover_text(text::DRIFT_HINT);
        }
    });
}

/// A statement's place, as the section editor numbers its lines: `3` or `3.1.2`.
fn path_text(path: &StmtPath) -> String {
    let mut parts: Vec<String> = path
        .blocks
        .iter()
        .flat_map(|(index, block)| [format!("{}", index + 1), format!("{}", block + 1)])
        .collect();
    parts.push(format!("{}", path.index + 1));
    format!("#{}", parts.join("."))
}

/// The rows' names, beside the lanes.
fn names(ui: &mut egui::Ui, sequence: &Sequence) {
    let tracks = sequence.tracks();
    let (rect, _) = ui.allocate_exact_size(
        egui::vec2(NAMES, RULER + ROW * tracks.len() as f32),
        Sense::hover(),
    );
    let painter = ui.painter_at(rect);
    for (row, track) in tracks.iter().enumerate() {
        let y = rect.top() + RULER + ROW * row as f32 + ROW * 0.5;
        painter.text(
            Pos2::new(rect.left() + 4.0, y),
            Align2::LEFT_CENTER,
            text::track_name(*track),
            FontId::proportional(13.0),
            track_color(*track),
        );
    }
}

/// The ruler, the rows with their blocks and the playhead.
fn lanes(app: &mut MapperApp, ui: &mut egui::Ui, sequence: &Sequence, actions: &mut Vec<Action>) {
    let tracks = sequence.tracks();
    let zoom = app.timeline_ui.zoom;
    let frames = sequence.length.max(1) + FRAMES_PER_SECOND * 2;
    let size = egui::vec2(
        (frames as f32 * zoom).max(ui.available_width()),
        RULER + ROW * tracks.len() as f32,
    );
    let (response, painter) = ui.allocate_painter(size, Sense::click_and_drag());
    let rect = response.rect;
    let x_of = |frame: f32| rect.left() + frame * zoom;
    let frame_of = |x: f32| ((x - rect.left()) / zoom).max(0.0);
    let block = |cue: &Cue, frames: u32| {
        let row = tracks
            .iter()
            .position(|track| *track == cue.track)
            .unwrap_or(0);
        let top = rect.top() + RULER + ROW * row as f32 + 3.0;
        let left = x_of(cue.start as f32);
        let width = (frames as f32 * zoom).max(4.0);
        Rect::from_min_size(Pos2::new(left, top), egui::vec2(width, ROW - 6.0))
    };
    // The ruler: a tick a second, a number every second.
    painter.rect_filled(
        Rect::from_min_size(rect.min, egui::vec2(rect.width(), RULER)),
        0.0,
        Color32::from_gray(35),
    );
    let mut second = 0;
    while second * FRAMES_PER_SECOND <= frames {
        let x = x_of((second * FRAMES_PER_SECOND) as f32);
        painter.line_segment(
            [
                Pos2::new(x, rect.top() + RULER - 6.0),
                Pos2::new(x, rect.bottom()),
            ],
            Stroke::new(1.0, Color32::from_gray(45)),
        );
        painter.text(
            Pos2::new(x + 2.0, rect.top() + 2.0),
            Align2::LEFT_TOP,
            format!("{second} s"),
            FontId::monospace(10.0),
            Color32::GRAY,
        );
        second += 1;
    }
    for row in 0..tracks.len() {
        if row % 2 == 1 {
            let top = rect.top() + RULER + ROW * row as f32;
            painter.rect_filled(
                Rect::from_min_size(Pos2::new(rect.left(), top), egui::vec2(rect.width(), ROW)),
                0.0,
                Color32::from_white_alpha(8),
            );
        }
    }
    let state = &mut app.timeline_ui;
    let selected = app
        .mapper
        .section
        .as_ref()
        .and_then(|open| open.selected.clone());
    // The gesture: a drag from a block's right edge changes its frames, a click picks a block,
    // anything else puts the playhead.
    let pointer = response.interact_pointer_pos().or(response.hover_pos());
    let editable = |cue: &Cue| cue.routine.is_none() && frames_operand(&cue.command).is_some();
    let under = |at: Pos2| {
        sequence
            .cues
            .iter()
            .rev()
            .find(|cue| block(cue, cue.frames).expand(1.0).contains(at))
    };
    // A drag starts some points away from where the button went down: the edge is where it went.
    if response.drag_started()
        && let Some(at) = ui.input(|input| input.pointer.press_origin())
    {
        let edge = sequence.cues.iter().rev().find(|cue| {
            let rect = block(cue, cue.frames);
            editable(cue)
                && (at.x - rect.right()).abs() <= EDGE_GRAB
                && rect.y_range().contains(at.y)
        });
        state.edge = edge.map(|cue| (cue.path.clone(), cue.frames));
    }
    if let Some((path, frames)) = &mut state.edge {
        if let (Some(at), Some(cue)) = (pointer, sequence.cue_of(path)) {
            *frames = ((at.x - x_of(cue.start as f32)) / zoom).round().max(1.0) as u32;
        }
        if response.drag_stopped() {
            let (path, frames) = state.edge.take().unwrap_or_default();
            if sequence
                .cue_of(&path)
                .is_some_and(|cue| cue.frames != frames)
            {
                actions.push(Action::SetCueFrames { path, frames });
            }
        }
    } else if response.clicked()
        && let Some(at) = pointer
    {
        match under(at) {
            Some(cue) => actions.push(Action::SelectCue(cue.path.clone())),
            None => actions.push(Action::SetPlayhead(frame_of(at.x))),
        }
    } else if response.dragged()
        && let Some(at) = pointer
    {
        actions.push(Action::SetPlayhead(frame_of(at.x)));
    }
    // The blocks.
    for cue in &sequence.cues {
        let frames = match &state.edge {
            Some((path, frames)) if *path == cue.path => *frames,
            _ => cue.frames,
        };
        let area = block(cue, frames);
        let color = track_color(cue.track);
        let fill = if cue.estimated {
            color.gamma_multiply(0.35)
        } else {
            color.gamma_multiply(0.7)
        };
        painter.rect_filled(area, 3.0, fill);
        if cue.estimated {
            // A guess: hatched.
            let mut x = area.left() + 4.0;
            while x < area.right() {
                painter.line_segment(
                    [
                        Pos2::new(x, area.bottom()),
                        Pos2::new((x + 6.0).min(area.right()), area.top()),
                    ],
                    Stroke::new(1.0, color.gamma_multiply(0.6)),
                );
                x += 8.0;
            }
        }
        if cue.cut {
            painter.line_segment(
                [area.right_top(), area.right_bottom()],
                Stroke::new(2.0, Color32::LIGHT_RED),
            );
        }
        if cue.hold == Hold::Player {
            // Waits for the player: a bar and the words, where they fit.
            painter.line_segment(
                [area.right_top(), area.right_bottom()],
                Stroke::new(3.0, Color32::WHITE),
            );
        }
        let label = match cue.hold {
            Hold::Player => format!("{} · {}", short(&cue.command), text::HOLD_PLAYER),
            _ => short(&cue.command).to_owned(),
        };
        let galley = painter.layout_no_wrap(label, FontId::proportional(11.0), Color32::WHITE);
        if galley.size().x + 6.0 <= area.width() {
            painter.galley(
                Pos2::new(area.left() + 3.0, area.center().y - galley.size().y * 0.5),
                galley,
                Color32::WHITE,
            );
        }
        if selected.as_ref() == Some(&cue.path) {
            painter.rect_stroke(
                area,
                3.0,
                Stroke::new(2.0, Color32::WHITE),
                StrokeKind::Outside,
            );
        }
        if state.game_at.as_ref() == Some(&cue.path) {
            painter.rect_stroke(
                area.expand(2.0),
                3.0,
                Stroke::new(2.0, Color32::from_rgb(255, 150, 40)),
                StrokeKind::Outside,
            );
        }
    }
    if sequence.ends {
        let x = x_of(sequence.length as f32);
        painter.line_segment(
            [
                Pos2::new(x, rect.top() + RULER),
                Pos2::new(x, rect.bottom()),
            ],
            Stroke::new(2.0, Color32::LIGHT_RED),
        );
    }
    let x = x_of(app.mapper.timeline.playhead);
    painter.line_segment(
        [Pos2::new(x, rect.top()), Pos2::new(x, rect.bottom())],
        Stroke::new(2.0, Color32::from_rgb(255, 80, 80)),
    );
    painter.add(Shape::convex_polygon(
        vec![
            Pos2::new(x - 5.0, rect.top()),
            Pos2::new(x + 5.0, rect.top()),
            Pos2::new(x, rect.top() + 7.0),
        ],
        Color32::from_rgb(255, 80, 80),
        Stroke::NONE,
    ));
    if state.edge.is_none()
        && let Some(cue) = response.hover_pos().and_then(under)
    {
        response.clone().on_hover_text_at_pointer(cue_hint(cue));
    } else if let Some(at) = response.hover_pos()
        && sequence.cues.iter().any(|cue| {
            let rect = block(cue, cue.frames);
            editable(cue)
                && (at.x - rect.right()).abs() <= EDGE_GRAB
                && rect.y_range().contains(at.y)
        })
    {
        ui.ctx().set_cursor_icon(egui::CursorIcon::ResizeHorizontal);
    }
}

/// A command's name without its family's prefix, for a block.
fn short(command: &str) -> &str {
    command
        .strip_prefix("entity_")
        .or_else(|| command.strip_prefix("story_"))
        .unwrap_or(command)
}

/// The characters the game has on the map shown, by the model's ids, and how far each is from
/// where the model has it at the playhead (with both points, for the canvas).
fn drift(
    mapper: &Mapper,
    view: Option<&LiveView>,
    sequence: &Sequence,
) -> Vec<(Track, f32, Vec3, Vec3)> {
    let Some(view) = view else {
        return Vec::new();
    };
    let frame = mapper.timeline.playhead;
    sequence
        .tracks()
        .into_iter()
        .filter_map(|track| {
            let Track::Actor(id) = track else { return None };
            let live = match id {
                PLAYER => view.player,
                PARTNER => view.partner,
                id => view
                    .npcs
                    .iter()
                    .find(|npc| npc.id.is_some_and(|known| i64::from(known) == id))
                    .copied(),
            }?;
            let game = Vec3::new(live.x, live.y, live.z);
            let model = sequence.pose_at(track, frame)?.position;
            let ground = Vec2::new(game.x - model.x, game.z - model.z).length();
            Some((track, ground, game, model))
        })
        .collect()
}

/// Starts playing the open section in the game: from its start, or from the statement chosen
/// (the playhead's when none is) with the code that places everyone first.
fn start_play(app: &mut MapperApp, sequence: &Sequence, from_here: bool) {
    let Some(open) = app.mapper.section.clone() else {
        return;
    };
    let mut run = StoryRun {
        script: open.script,
        section: open.section,
        code: None,
    };
    let mut sent = None;
    if from_here {
        let at = open.selected.clone().or_else(|| {
            sequence
                .path_at(app.mapper.timeline.playhead as u32)
                .cloned()
        });
        let Some(at) = at else {
            app.mapper
                .messages
                .push(Message::Error(text::PLAY_NEEDS_CUE.to_owned()));
            return;
        };
        match app.mapper.play_from_here(&at) {
            Ok(section) => {
                run.code = Some(section.code.clone());
                sent = Some(section);
            }
            Err(message) => {
                app.mapper.messages.push(Message::Error(message));
                return;
            }
        }
    }
    let state = &mut app.timeline_ui;
    state.played = Some((open.script, open.section));
    state.sent = sent;
    state.follow = true;
    state.game_at = None;
    crate::probe::ask_play(app, run);
}

/// Each frame: reads the story while following the game, finds the statement it runs and moves
/// the playhead there, and puts the playhead at the statement the section editor chose.
pub fn tick(app: &mut MapperApp) {
    let selected = app
        .mapper
        .section
        .as_ref()
        .and_then(|open| open.selected.clone());
    if !app.mapper.timeline.shown {
        app.timeline_ui.last_selected = selected;
        return;
    }
    let now = Instant::now();
    let connected = app.live.is_connected();
    let state = &mut app.timeline_ui;
    if state.follow && connected && state.next_read.is_none_or(|at| now >= at) {
        app.live.send(Command::ReadStory);
        state.next_read = Some(now + READ_EVERY);
    }
    let Some(sequence) = app.mapper.sequence() else {
        return;
    };
    state.game_at = connected
        .then(|| game_statement(&app.mapper, app.live.story.as_ref(), state, &sequence))
        .flatten();
    if state.follow
        && let Some(cue) = state.game_at.as_ref().and_then(|at| sequence.cue_of(at))
    {
        app.mapper.timeline.playhead = cue.start as f32;
    } else if selected != state.last_selected
        && let Some(cue) = selected.as_ref().and_then(|at| sequence.cue_of(at))
    {
        app.mapper.timeline.playhead = cue.start as f32;
    }
    app.timeline_ui.last_selected = selected;
}

/// The statement of the open section the game runs now, from the lines `story_state` gives: the
/// lines of the code sent for it, or of the section's own file.
fn game_statement(
    mapper: &Mapper,
    story: Option<&mapper_live::StoryState>,
    state: &TimelineUi,
    sequence: &Sequence,
) -> Option<StmtPath> {
    let open = mapper.section.as_ref()?;
    let running = story?.sections.iter().find(|running| {
        running.script == u32::from(open.script) && running.section == u32::from(open.section)
    })?;
    if let Some(sent) = state
        .sent
        .as_ref()
        .filter(|sent| sent.script == open.script && sent.section == open.section)
    {
        return running.lines.iter().find_map(|&line| {
            let line = usize::try_from(line).ok()?;
            sent.lines.get(&line).cloned()
        });
    }
    sequence
        .cue_at_lines(&running.lines)
        .map(|cue| cue.path.clone())
}

/// Over the map: each character's walks with arrows and step numbers, the turns, the camera's
/// points, the markers a drag moves, everyone where the playhead has them, and how far the game's
/// characters are from there.
pub fn draw(painter: &egui::Painter, app: &MapperApp, to_screen: &impl Fn(Vec2) -> Pos2) {
    let mapper = &app.mapper;
    if !mapper.timeline.shown || mapper.section.is_none() {
        return;
    }
    let (Some(map), Some(sequence)) = (&mapper.map, mapper.sequence()) else {
        return;
    };
    let project = |world: Vec3| {
        map.camera
            .project_to_background(world)
            .map(|at| to_screen(at.pixel))
    };
    let selected = mapper
        .section
        .as_ref()
        .and_then(|open| open.selected.as_ref());
    let frame = mapper.timeline.playhead;
    for track in sequence.tracks() {
        if !matches!(track, Track::Actor(_) | Track::Camera) {
            continue;
        }
        let color = track_color(track);
        let mut step = 0;
        for cue in sequence.cues.iter().filter(|cue| cue.track == track) {
            let chosen = selected == Some(&cue.path);
            let width = if chosen { 3.0 } else { 1.8 };
            if let Some((from, to)) = cue.way
                && let (Some(a), Some(b)) = (project(from), project(to))
            {
                step += 1;
                painter.arrow(a, b - a, Stroke::new(width, color));
                if track == Track::Camera {
                    frustum(painter, b, color, false);
                }
                let middle = a + (b - a) * 0.5;
                let number = format!("{step}");
                let rect = crate::canvas::label_rect(
                    painter,
                    middle + egui::vec2(0.0, 14.0),
                    &number,
                    color,
                );
                crate::canvas::label_in(painter, rect, &number, color);
            } else if let Some(yaw) = cue.facing
                && let Some(pose) = sequence.pose_at(track, cue.start as f32)
                && let (Some(a), Some(b)) = (
                    project(pose.position),
                    project(pose.position + Vec3::new(yaw.sin(), 0.0, yaw.cos()) * FACING),
                )
            {
                // A turn: a dashed arrow from where it stands.
                painter.add(Shape::dashed_line(
                    &[a, b],
                    Stroke::new(width, color),
                    4.0,
                    3.0,
                ));
                painter.arrow(a + (b - a) * 0.7, (b - a) * 0.3, Stroke::new(width, color));
            }
        }
    }
    for handle in mapper.handles(&sequence) {
        let at = to_screen(handle.at);
        let dragging = mapper.timeline.dragging.as_ref() == Some(&handle.path);
        let chosen = selected == Some(&handle.path);
        painter.add(crate::canvas::diamond(at, 6.0, track_color(handle.track)));
        if chosen || dragging {
            painter.circle_stroke(at, 9.0, Stroke::new(2.0, colors::SELECTED));
        }
    }
    // Everyone at the playhead.
    for track in sequence.tracks() {
        let Some(pose) = sequence.pose_at(track, frame) else {
            continue;
        };
        let Some(at) = project(pose.position) else {
            continue;
        };
        let color = track_color(track);
        match track {
            Track::Camera => frustum(painter, at, color, true),
            Track::Actor(_) => {
                let ahead = pose.position + Vec3::new(pose.yaw.sin(), 0.0, pose.yaw.cos()) * FACING;
                if let Some(tip) = project(ahead) {
                    painter.arrow(at, tip - at, Stroke::new(2.5, Color32::WHITE));
                }
                painter.circle_filled(at, 7.0, color);
                painter.circle_stroke(at, 7.0, Stroke::new(1.5, Color32::BLACK));
                crate::canvas::label_at(painter, at, &text::track_name(track), color);
            }
            _ => {}
        }
    }
    for (_, _, game, model) in drift(mapper, app.live.shows(mapper), &sequence) {
        if let (Some(a), Some(b)) = (project(game), project(model)) {
            painter.add(Shape::dashed_line(
                &[a, b],
                Stroke::new(1.5, Color32::LIGHT_RED),
                5.0,
                3.0,
            ));
        }
    }
}

/// A camera marker: a frustum opening down from its point, filled for where it is now.
fn frustum(painter: &egui::Painter, at: Pos2, color: Color32, now: bool) {
    let points = vec![
        at + egui::vec2(-4.0, -10.0),
        at + egui::vec2(4.0, -10.0),
        at + egui::vec2(11.0, 6.0),
        at + egui::vec2(-11.0, 6.0),
    ];
    let fill = if now {
        color.gamma_multiply(0.6)
    } else {
        Color32::TRANSPARENT
    };
    painter.add(Shape::convex_polygon(points, fill, Stroke::new(1.5, color)));
    painter.circle_filled(at, 2.5, color);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_path_reads_like_the_editor_numbers_lines() {
        assert_eq!(path_text(&StmtPath::top(2)), "#3");
        assert_eq!(path_text(&StmtPath::top(1).child(0, 4)), "#2.1.5");
    }

    #[test]
    fn a_block_shows_its_command_without_the_family() {
        assert_eq!(short("entity_walk_to"), "walk_to");
        assert_eq!(short("delay"), "delay");
    }
}
