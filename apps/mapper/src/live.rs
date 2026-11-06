//! The link to a running game, for the window: connects (and reconnects) on its own thread
//! (mapper_live::LiveLink), follows the game's map, keeps the game's preview in step with the
//! edit, and keeps what the game said for the panels: where its characters are, its story, its
//! log and the commands it refused.

use std::collections::VecDeque;
use std::time::{Duration, Instant};

use mapper_core::{Action, Follow, Mapper, Message, PreviewStep, PreviewSync, text};
use mapper_live::{
    Command, Done, Endpoint, Event, LiveLink, LiveView, LogEntry, PreviewApplied, StoryState,
    StoryWrite,
};

/// How often the preview is compared with the edit (and sent when it changed): the edit settles
/// for this long before the game sees it.
const PREVIEW_EVERY: Duration = Duration::from_millis(300);
/// The log lines kept for the panel.
const MOST_LOG_LINES: usize = 300;
/// The refusals kept for the problems panel.
const MOST_PROBLEMS: usize = 50;
/// How often the window redraws while linked, to take the link's news.
pub const REDRAW_EVERY: Duration = Duration::from_millis(100);

/// The link to a running game and what it said.
pub struct LivePanel {
    pub endpoint: String,
    link: Option<LiveLink>,
    /// Whether the link has a connection now (it may be trying again).
    pub connected: bool,
    /// The connection's state in words.
    pub state: String,
    /// What `live` said last, and how long it took.
    pub view: Option<LiveView>,
    pub round_trip: Duration,
    /// Whether the game's map debug overlay is on, as the game said last.
    pub overlay: bool,
    /// Follow mode: the game's map is opened when it changes.
    pub follow: bool,
    follower: Follow,
    /// "Vista previa en vivo": the game shows the unsaved edit of its map.
    pub preview: bool,
    sync: PreviewSync,
    next_preview: Instant,
    pub story: Option<StoryState>,
    /// The story is read again every second.
    pub story_follow: bool,
    pub story_filter: String,
    /// The fields of the story writes.
    pub story_trigger: u32,
    pub story_variable: u32,
    pub story_value: u8,
    /// The game's warnings and errors, oldest first.
    pub log: VecDeque<LogEntry>,
    /// The commands the game refused, with its message (the loader's file and line).
    pub problems: Vec<String>,
}

impl Default for LivePanel {
    fn default() -> Self {
        Self {
            endpoint: String::new(),
            link: None,
            connected: false,
            state: text::LIVE_OFF.to_owned(),
            view: None,
            round_trip: Duration::ZERO,
            overlay: false,
            follow: true,
            follower: Follow::default(),
            preview: false,
            sync: PreviewSync::default(),
            next_preview: Instant::now(),
            story: None,
            story_follow: false,
            story_filter: String::new(),
            story_trigger: 0,
            story_variable: 0,
            story_value: 0,
            log: VecDeque::new(),
            problems: Vec::new(),
        }
    }
}

impl LivePanel {
    pub fn connect(&mut self) {
        match Endpoint::parse(&self.endpoint) {
            Ok(endpoint) => {
                self.link = Some(LiveLink::start(endpoint));
                self.state = text::LIVE_CONNECTING.to_owned();
                if self.story_follow {
                    self.send(Command::FollowStory(true));
                }
            }
            Err(error) => self.state = error.to_string(),
        }
    }

    /// Drops the link; the game's preview goes with it.
    pub fn disconnect(&mut self) {
        self.link = None;
        self.connected = false;
        self.view = None;
        self.story = None;
        self.sync.reset();
        self.state = text::LIVE_OFF.to_owned();
    }

    /// Whether a link is up or trying to be.
    pub fn is_linked(&self) -> bool {
        self.link.is_some()
    }

    pub fn is_connected(&self) -> bool {
        self.connected
    }

    pub fn send(&self, command: Command) {
        if let Some(link) = &self.link {
            link.send(command);
        }
    }

    /// Asks the game to read its map again from the packs where the player stands.
    pub fn reload(&self) {
        self.send(Command::Reload);
    }

    pub fn set_overlay(&self, on: bool) {
        self.send(Command::Overlay(Some(on)));
    }

    pub fn set_story_follow(&mut self, on: bool) {
        self.story_follow = on;
        self.send(Command::FollowStory(on));
    }

    pub fn write_story(&self, write: StoryWrite) {
        self.send(Command::StorySet(write));
        self.send(Command::ReadStory);
    }

    /// Puts the player somewhere (a `place` text); `run` lets the game run afterwards, so an exit
    /// it stands on takes it.
    pub fn place(&self, text: String, run: bool) {
        self.send(Command::Place(text));
        if run && self.view.as_ref().is_some_and(|view| view.paused) {
            self.send(Command::Resume);
        }
    }

    /// The map the game plays, when it is the one shown.
    pub fn shows<'a>(&'a self, mapper: &Mapper) -> Option<&'a LiveView> {
        let view = self.view.as_ref()?;
        let shown = mapper.map.as_ref()?.document.id.as_str();
        (view.map() == Some(shown)).then_some(view)
    }

    /// Each frame: what the link said, then follow mode and the preview.
    pub fn update(&mut self, mapper: &mut Mapper, pointer_held: bool) {
        let Some(link) = &self.link else {
            return;
        };
        for event in link.events() {
            self.take(event, mapper);
        }
        if !self.connected {
            return;
        }
        if self.follow {
            let game = self.view.as_ref().and_then(LiveView::map);
            let shown = mapper.map.as_ref().map(|map| map.document.id.as_str());
            if let Some(map) = self.follower.map_to_open(game, shown) {
                mapper.apply(Action::OpenMap(map));
            }
        }
        self.sync_preview(mapper, pointer_held);
    }

    fn take(&mut self, event: Event, mapper: &mut Mapper) {
        match event {
            Event::Connected => {
                self.connected = true;
                self.state = text::LIVE_ON.to_owned();
                self.follower.reset();
                self.sync.reset();
                self.send(Command::Status);
            }
            Event::Lost(reason) => {
                self.connected = false;
                self.view = None;
                self.state = format!("{} · {reason}", text::LIVE_RECONNECTING);
            }
            Event::Live { view, round_trip } => {
                self.view = Some(*view);
                self.round_trip = round_trip;
            }
            Event::Story(story) => self.story = Some(*story),
            Event::Log(entries) => {
                self.log.extend(entries);
                while self.log.len() > MOST_LOG_LINES {
                    self.log.pop_front();
                }
            }
            Event::Done(done) => self.done(done, mapper),
            Event::Failed { command, message } => {
                let line = format!("{command}: {message}");
                mapper.messages.push(Message::Error(line.clone()));
                self.problems.push(line);
                if self.problems.len() > MOST_PROBLEMS {
                    self.problems.remove(0);
                }
            }
        }
    }

    fn done(&mut self, done: Done, mapper: &mut Mapper) {
        let info = match done {
            Done::Status(status) => {
                self.overlay = status.overlay.unwrap_or(self.overlay);
                return;
            }
            Done::Overlay(on) => {
                self.overlay = on;
                return;
            }
            Done::Reloaded(reloaded) => format!("{}: {}", text::LIVE_RELOADED, reloaded.map),
            Done::Previewed { map, applied } => {
                self.sync.confirm();
                // The last `live` may predate the preview: until the next one says otherwise,
                // the game shows it (or the sync would take it as dropped and send it again).
                if let Some(view) = &mut self.view {
                    view.preview = Some(map);
                }
                match applied {
                    PreviewApplied::Collision => text::LIVE_PREVIEW_COLLISION,
                    PreviewApplied::Reentered => text::LIVE_PREVIEW_REENTERED,
                }
                .to_owned()
            }
            Done::PreviewCleared(cleared) => {
                if let Some(view) = &mut self.view {
                    view.preview = None;
                }
                if !cleared {
                    return;
                }
                text::LIVE_PREVIEW_CLEARED.to_owned()
            }
            Done::Placed => text::LIVE_PLACED.to_owned(),
            Done::Paused(_) | Done::Advanced(_) | Done::Pressed | Done::StoryWritten(_) => return,
        };
        mapper.messages.push(Message::Info(info));
    }

    /// Sends the edit to the game when it changed (and the pointer is not in the middle of a
    /// stroke), or clears the preview when the edit is gone or the preview was turned off. The
    /// game takes a preview only in the field with no story running; until then nothing is sent.
    fn sync_preview(&mut self, mapper: &Mapper, pointer_held: bool) {
        let now = Instant::now();
        if pointer_held || now < self.next_preview {
            return;
        }
        self.next_preview = now + PREVIEW_EVERY;
        let Some(view) = &self.view else {
            return;
        };
        if view.mode != "field" || view.busy {
            return;
        }
        let wanted = if self.preview && self.shows(mapper).is_some() {
            mapper.preview_patch()
        } else {
            None
        };
        let game = self.view.as_ref().and_then(|view| view.preview.clone());
        match self.sync.step(wanted, game.as_deref()) {
            PreviewStep::Send { map, patch } => self.send(Command::Preview {
                map,
                patch,
                pack: mapper.current_target().map(|target| target.id),
            }),
            PreviewStep::Clear => self.send(Command::ClearPreview),
            PreviewStep::Wait => {}
        }
    }

    /// The status bar's words about the game.
    pub fn summary(&self) -> String {
        if !self.connected {
            return self.state.clone();
        }
        let Some(view) = &self.view else {
            return self.state.clone();
        };
        let mut parts = vec![text::game_mode(&view.mode).to_owned()];
        if let Some(map) = view.map() {
            parts.push(map.to_owned());
        }
        parts.push(format!("tick {}", view.tick));
        if view.paused {
            parts.push(text::LIVE_PAUSED.to_owned());
        }
        if view.busy {
            parts.push(text::LIVE_BUSY.to_owned());
        }
        if view.preview.is_some() {
            parts.push(text::LIVE_PREVIEW_SHOWN.to_owned());
        }
        parts.push(format!("{} ms", self.round_trip.as_millis()));
        format!("{}: {}", text::LIVE_GAME, parts.join(" · "))
    }
}
