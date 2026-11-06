//! A client on its own thread, for an interface that must never wait on the game: it asks `live`
//! at a steady pace, reads the new warnings and errors of the game's log when `live` says there
//! are some, reads the story now and then when asked to, runs the interface's commands in the order
//! sent, and connects again when the game goes away (a game restarted on the same endpoint is
//! found again). What it learns comes back as [`Event`]s.

use std::sync::mpsc::{self, Receiver, RecvTimeoutError, Sender};
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

use crate::answers::{
    GameStatus, LiveView, LogEntry, PreviewApplied, ReloadedMap, StoryState, StoryWrite,
};
use crate::{Endpoint, Error, LiveClient};

/// How often the link asks `live`: about 7 times a second, enough to follow a walk.
pub const LINK_POLL: Duration = Duration::from_millis(150);
/// How long the link waits before connecting again.
const RECONNECT: Duration = Duration::from_secs(1);
/// How often the story is read when it is followed.
const STORY_EVERY: Duration = Duration::from_secs(1);
/// How long closing the link waits for its thread to drop the preview and end.
const CLOSE_WAIT: Duration = Duration::from_secs(2);

/// What the interface asks the game, in order.
#[derive(Clone, Debug, PartialEq)]
pub enum Command {
    /// `status`, for what `live` leaves out (whether the debug overlay is on).
    Status,
    /// `reload_map` where the player stands.
    Reload,
    Overlay(Option<bool>),
    /// `preview_map`; a later preview or clear sent before this one ran replaces it.
    Preview {
        map: String,
        patch: String,
        pack: Option<String>,
    },
    ClearPreview,
    /// `place` with its text.
    Place(String),
    Pause,
    Resume,
    Advance(u64),
    StorySet(StoryWrite),
    /// Reads the story once now.
    ReadStory,
    /// Reads the story every second, or stops.
    FollowStory(bool),
}

impl Command {
    /// The channel's command it sends.
    pub fn name(&self) -> &'static str {
        match self {
            Self::Status => "status",
            Self::Reload => "reload_map",
            Self::Overlay(_) => "overlay",
            Self::Preview { .. } => "preview_map",
            Self::ClearPreview => "preview_clear",
            Self::Place(_) => "place",
            Self::Pause => "pause",
            Self::Resume => "resume",
            Self::Advance(_) => "advance",
            Self::StorySet(_) => "story_set",
            Self::ReadStory | Self::FollowStory(_) => "story_state",
        }
    }
}

/// A command the game did.
#[derive(Clone, Debug, PartialEq)]
pub enum Done {
    Status(GameStatus),
    Reloaded(ReloadedMap),
    Overlay(bool),
    Previewed {
        map: String,
        applied: PreviewApplied,
    },
    /// Whether there was a preview to drop.
    PreviewCleared(bool),
    Placed,
    /// Whether the game is paused now.
    Paused(bool),
    Advanced(u64),
    StoryWritten(StoryWrite),
}

#[derive(Clone, Debug, PartialEq)]
pub enum Event {
    Connected,
    /// The connection is gone, or could not be made: why. The link tries again.
    Lost(String),
    /// What `live` said, and how long its answer took.
    Live {
        view: Box<LiveView>,
        round_trip: Duration,
    },
    Story(Box<StoryState>),
    /// New warnings and errors of the game's log, oldest first.
    Log(Vec<LogEntry>),
    Done(Done),
    /// The game refused a command, with its message.
    Failed {
        command: &'static str,
        message: String,
    },
}

enum Message {
    Run(Command),
    Close,
}

/// A client on its own thread. Dropping it drops the preview it put on, then ends the thread.
pub struct LiveLink {
    commands: Sender<Message>,
    events: Receiver<Event>,
    ended: Receiver<()>,
    worker: Option<JoinHandle<()>>,
}

impl LiveLink {
    /// Starts connecting to the game.
    pub fn start(endpoint: Endpoint) -> Self {
        Self::start_with(endpoint, LINK_POLL, RECONNECT)
    }

    /// With its own pace, for the tests.
    pub fn start_with(endpoint: Endpoint, poll: Duration, reconnect: Duration) -> Self {
        let (commands, inbox) = mpsc::channel();
        let (outbox, events) = mpsc::channel();
        let (end, ended) = mpsc::channel();
        let worker = Worker {
            endpoint,
            client: None,
            events: outbox,
            poll,
            reconnect,
            next_poll: Instant::now(),
            follow_story: false,
            next_story: Instant::now(),
            log_seen: None,
            previewing: false,
            last_failure: None,
        };
        let worker = std::thread::Builder::new()
            .name("mapper-live".into())
            .spawn(move || {
                worker.run(&inbox);
                let _ = end.send(());
            })
            .ok();
        Self {
            commands,
            events,
            ended,
            worker,
        }
    }

    pub fn send(&self, command: Command) {
        let _ = self.commands.send(Message::Run(command));
    }

    /// The events since the last call.
    pub fn events(&self) -> Vec<Event> {
        self.events.try_iter().collect()
    }

    /// Waits up to `timeout` for the next event (for the tests and tools without a frame loop).
    pub fn next_event(&self, timeout: Duration) -> Option<Event> {
        self.events.recv_timeout(timeout).ok()
    }
}

impl Drop for LiveLink {
    fn drop(&mut self) {
        let _ = self.commands.send(Message::Close);
        // The thread may be waiting on an answer; it is left to end on its own after a while.
        if self.ended.recv_timeout(CLOSE_WAIT).is_ok()
            && let Some(worker) = self.worker.take()
        {
            let _ = worker.join();
        }
    }
}

/// Drops the previews and clears that a later one replaces: only the last preview-or-clear of a
/// batch is sent, at its place.
pub(crate) fn coalesce(batch: Vec<Command>) -> Vec<Command> {
    let is_preview =
        |command: &Command| matches!(command, Command::Preview { .. } | Command::ClearPreview);
    let last = batch.iter().rposition(is_preview);
    batch
        .into_iter()
        .enumerate()
        .filter(|(index, command)| !is_preview(command) || Some(*index) == last)
        .map(|(_, command)| command)
        .collect()
}

struct Worker {
    endpoint: Endpoint,
    client: Option<LiveClient>,
    events: Sender<Event>,
    poll: Duration,
    reconnect: Duration,
    next_poll: Instant,
    follow_story: bool,
    next_story: Instant,
    /// The newest log entry read; None before the first read of this game.
    log_seen: Option<u64>,
    /// Whether a preview this link sent is on.
    previewing: bool,
    /// The last connection failure told, so a game that stays away is told once.
    last_failure: Option<String>,
}

impl Worker {
    fn run(mut self, inbox: &Receiver<Message>) {
        loop {
            if self.client.is_none() {
                self.connect();
            }
            let wait = if self.client.is_some() {
                self.next_poll.saturating_duration_since(Instant::now())
            } else {
                self.reconnect
            };
            let mut closing = false;
            match inbox.recv_timeout(wait) {
                Ok(Message::Close) | Err(RecvTimeoutError::Disconnected) => break,
                Ok(Message::Run(first)) => {
                    let mut batch = vec![first];
                    for message in inbox.try_iter() {
                        match message {
                            Message::Run(command) => batch.push(command),
                            Message::Close => closing = true,
                        }
                    }
                    for command in coalesce(batch) {
                        self.run_command(command);
                    }
                }
                Err(RecvTimeoutError::Timeout) => {}
            }
            if closing {
                break;
            }
            if self.client.is_some() && Instant::now() >= self.next_poll {
                self.watch();
            }
        }
        if self.previewing
            && let Some(client) = &mut self.client
        {
            let _ = client.preview_clear();
        }
    }

    fn tell(&self, event: Event) {
        let _ = self.events.send(event);
    }

    fn connect(&mut self) {
        match LiveClient::connect(&self.endpoint) {
            Ok(client) => {
                self.client = Some(client);
                self.last_failure = None;
                self.log_seen = None;
                self.previewing = false;
                self.next_poll = Instant::now();
                self.tell(Event::Connected);
            }
            Err(error) => {
                let message = error.to_string();
                if self.last_failure.as_ref() != Some(&message) {
                    self.last_failure = Some(message.clone());
                    self.tell(Event::Lost(message));
                }
            }
        }
    }

    /// An error of a request: a refusal is told; a lost connection is dropped, to connect again.
    fn failed(&mut self, command: &'static str, error: Error) {
        if error.is_lost() {
            self.client = None;
            self.previewing = false;
            let message = error.to_string();
            self.last_failure = Some(message.clone());
            self.tell(Event::Lost(message));
        } else {
            let message = match error {
                Error::Refused { message, .. } => message,
                other => other.to_string(),
            };
            self.tell(Event::Failed { command, message });
        }
    }

    fn run_command(&mut self, command: Command) {
        let name = command.name();
        if let Command::FollowStory(on) = command {
            self.follow_story = on;
            self.next_story = Instant::now();
            return;
        }
        let Some(client) = &mut self.client else {
            self.tell(Event::Failed {
                command: name,
                message: self
                    .last_failure
                    .clone()
                    .unwrap_or_else(|| "not connected".to_owned()),
            });
            return;
        };
        let done = match command {
            Command::Status => client.status().map(Done::Status),
            Command::Reload => client.reload_map(None).map(Done::Reloaded),
            Command::Overlay(on) => client.overlay(on).map(Done::Overlay),
            Command::Preview { map, patch, pack } => client
                .preview_map(&map, &patch, pack.as_deref())
                .map(|applied| Done::Previewed { map, applied }),
            Command::ClearPreview => client.preview_clear().map(Done::PreviewCleared),
            Command::Place(text) => client.place(&text).map(|()| Done::Placed),
            Command::Pause => client.pause().map(|status| Done::Paused(status.paused)),
            Command::Resume => client.resume().map(|status| Done::Paused(status.paused)),
            Command::Advance(ticks) => client.advance(ticks).map(|()| Done::Advanced(ticks)),
            Command::StorySet(write) => client.story_set(write).map(|()| Done::StoryWritten(write)),
            Command::ReadStory => match client.story_state() {
                Ok(story) => {
                    self.tell(Event::Story(Box::new(story)));
                    return;
                }
                Err(error) => Err(error),
            },
            Command::FollowStory(_) => return,
        };
        match done {
            Ok(done) => {
                match &done {
                    Done::Previewed { .. } => self.previewing = true,
                    Done::PreviewCleared(_) | Done::Reloaded(_) => self.previewing = false,
                    _ => {}
                }
                self.tell(Event::Done(done));
            }
            Err(error) => self.failed(name, error),
        }
    }

    /// Asks `live`, then the log's new entries and the story when due.
    fn watch(&mut self) {
        self.next_poll = Instant::now() + self.poll;
        let Some(client) = &mut self.client else {
            return;
        };
        let asked = Instant::now();
        let view = match client.live() {
            Ok(view) => view,
            Err(error) => return self.failed("live", error),
        };
        let round_trip = asked.elapsed();
        // A game started again counts its log from 1 again.
        if self.log_seen.is_some_and(|seen| view.log < seen) {
            self.log_seen = None;
        }
        let new_log = view.log > 0 && self.log_seen.is_none_or(|seen| view.log > seen);
        let _ = self.events.send(Event::Live {
            view: Box::new(view),
            round_trip,
        });
        if new_log {
            match client.log_tail(self.log_seen) {
                Ok((entries, next)) => {
                    self.log_seen = Some(next);
                    if !entries.is_empty() {
                        let _ = self.events.send(Event::Log(entries));
                    }
                }
                Err(error) => return self.failed("log_tail", error),
            }
        }
        if self.follow_story && Instant::now() >= self.next_story {
            self.next_story = Instant::now() + STORY_EVERY;
            self.run_command(Command::ReadStory);
        }
    }
}

#[cfg(test)]
mod tests {
    use std::io::{BufRead, BufReader, Write};
    use std::net::{Ipv4Addr, TcpListener, TcpStream};
    use std::sync::{Arc, Mutex};

    use serde_json::{Value, json};

    use super::*;

    const WAIT: Duration = Duration::from_secs(5);

    fn preview(map: &str) -> Command {
        Command::Preview {
            map: map.into(),
            patch: String::new(),
            pack: None,
        }
    }

    #[test]
    fn only_the_last_preview_of_a_batch_is_sent() {
        let batch = vec![
            preview("a"),
            Command::Pause,
            preview("b"),
            Command::ClearPreview,
            Command::Advance(1),
            preview("c"),
        ];
        assert_eq!(
            coalesce(batch),
            vec![Command::Pause, Command::Advance(1), preview("c")]
        );
        assert_eq!(
            coalesce(vec![preview("a"), Command::ClearPreview]),
            vec![Command::ClearPreview]
        );
    }

    /// A fake game that takes any number of clients at once, each on its own thread, and answers
    /// `live` with a tick that grows and a log number of 2, and `log_tail` with two entries. It
    /// keeps the requests seen with the client's number, from 1.
    struct FakeGame {
        port: u16,
        seen: Arc<Mutex<Vec<(u64, Value)>>>,
    }

    impl FakeGame {
        fn start() -> Self {
            let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, 0)).unwrap();
            let port = listener.local_addr().unwrap().port();
            let seen = Arc::new(Mutex::new(Vec::new()));
            let log = seen.clone();
            std::thread::spawn(move || {
                for (client, stream) in (1u64..).zip(listener.incoming()) {
                    let Ok(stream) = stream else { return };
                    let log = log.clone();
                    std::thread::spawn(move || serve(client, stream, &log, u64::MAX));
                }
            });
            Self { port, seen }
        }

        fn commands_of(&self, client: u64) -> Vec<String> {
            self.seen
                .lock()
                .unwrap()
                .iter()
                .filter(|(who, _)| *who == client)
                .map(|(_, request)| request["cmd"].as_str().unwrap_or_default().to_owned())
                .collect()
        }
    }

    fn serve(client: u64, stream: TcpStream, log: &Mutex<Vec<(u64, Value)>>, limit: u64) {
        let mut reader = BufReader::new(stream.try_clone().unwrap());
        let mut writer = stream;
        let mut line = String::new();
        let mut tick = 0;
        for _ in 0..limit {
            line.clear();
            if reader.read_line(&mut line).unwrap_or(0) == 0 {
                return;
            }
            let request: Value = serde_json::from_str(&line).unwrap();
            tick += 1;
            let mut reply = match request["cmd"].as_str() {
                Some("live") => json!({"tick": tick, "mode": "field", "map": "area_a",
                    "log": 2, "player": {"x": 1, "y": 0, "z": 2, "yaw": 0}, "npcs": []}),
                Some("log_tail") => json!({"next": 2, "entries": [
                    {"seq": 1, "level": "warning", "text": "first"},
                    {"seq": 2, "level": "error", "text": "second"}]}),
                Some("preview_map") => json!({"map": "area_a", "applied": "reentered"}),
                Some("preview_clear") => json!({"map": "area_a", "cleared": true}),
                Some("story_set") => {
                    json!({"ok": false, "error": "story_set: a story script runs"})
                }
                _ => json!({}),
            };
            reply["id"] = request["id"].clone();
            if reply.get("ok").is_none() {
                reply["ok"] = json!(true);
            }
            log.lock().unwrap().push((client, request));
            if writeln!(writer, "{reply}").is_err() {
                return;
            }
        }
    }

    /// The events until one matches, or panics after a while.
    fn wait_for(link: &LiveLink, mut wanted: impl FnMut(&Event) -> bool) -> Vec<Event> {
        let mut events = Vec::new();
        let until = Instant::now() + WAIT;
        while Instant::now() < until {
            if let Some(event) = link.next_event(Duration::from_millis(50)) {
                let found = wanted(&event);
                events.push(event);
                if found {
                    return events;
                }
            }
        }
        panic!("the event never came; got {events:?}");
    }

    fn fast_link(port: u16) -> LiveLink {
        LiveLink::start_with(
            Endpoint::Tcp(port),
            Duration::from_millis(20),
            Duration::from_millis(50),
        )
    }

    #[test]
    fn watches_the_game_reads_its_log_once_and_runs_commands() {
        let game = FakeGame::start();
        let link = fast_link(game.port);
        let events = wait_for(&link, |event| matches!(event, Event::Log(_)));
        assert_eq!(events.first(), Some(&Event::Connected));
        let Some(Event::Live { view, .. }) =
            events.iter().find(|e| matches!(e, Event::Live { .. }))
        else {
            panic!("no live view: {events:?}");
        };
        assert_eq!(view.map(), Some("area_a"));
        assert_eq!(view.player.unwrap().z, 2.0);
        let Some(Event::Log(entries)) = events.last() else {
            unreachable!()
        };
        assert_eq!(entries.len(), 2);

        link.send(Command::StorySet(StoryWrite::Trigger {
            number: 3,
            on: true,
        }));
        let events = wait_for(&link, |event| matches!(event, Event::Failed { .. }));
        assert_eq!(
            events.last(),
            Some(&Event::Failed {
                command: "story_set",
                message: "story_set: a story script runs".into()
            })
        );
        // The log is not read again while its newest entry stays the same.
        wait_for(
            &link,
            |event| matches!(event, Event::Live { view, .. } if view.tick > 6),
        );
        assert_eq!(
            game.commands_of(1)
                .iter()
                .filter(|command| *command == "log_tail")
                .count(),
            1
        );

        link.send(preview("area_a"));
        wait_for(&link, |event| {
            matches!(event, Event::Done(Done::Previewed { .. }))
        });
        // Dropping the link drops the preview it put on.
        drop(link);
        let commands = game.commands_of(1);
        assert_eq!(commands.last().map(String::as_str), Some("preview_clear"));
    }

    #[test]
    fn connects_again_when_the_game_comes_back() {
        // Nothing listens yet: the link says so once and keeps trying.
        let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, 0)).unwrap();
        let port = listener.local_addr().unwrap().port();
        drop(listener);
        let link = fast_link(port);
        let first = wait_for(&link, |event| matches!(event, Event::Lost(_)));
        assert_eq!(first.len(), 1);
        std::thread::sleep(Duration::from_millis(200));
        assert!(
            link.events().is_empty(),
            "a game that stays away is told once"
        );
        link.send(Command::Pause);
        assert!(matches!(
            link.next_event(WAIT),
            Some(Event::Failed {
                command: "pause",
                ..
            })
        ));

        // The game comes up on that port; it closes its first client after three answers.
        let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, port)).unwrap();
        let seen = Arc::new(Mutex::new(Vec::new()));
        let log = seen.clone();
        std::thread::spawn(move || {
            for (client, stream) in (1u64..).zip(listener.incoming()) {
                let Ok(stream) = stream else { return };
                let log = log.clone();
                let limit = if client == 1 { 3 } else { u64::MAX };
                std::thread::spawn(move || serve(client, stream, &log, limit));
            }
        });
        wait_for(&link, |event| *event == Event::Connected);
        wait_for(&link, |event| matches!(event, Event::Lost(_)));
        wait_for(&link, |event| *event == Event::Connected);
        wait_for(&link, |event| matches!(event, Event::Live { .. }));
        let clients: Vec<u64> = seen.lock().unwrap().iter().map(|(who, _)| *who).collect();
        assert!(clients.contains(&2), "{clients:?}");
    }

    #[test]
    fn two_clients_talk_to_the_same_game_at_once() {
        let game = FakeGame::start();
        let link = fast_link(game.port);
        wait_for(&link, |event| matches!(event, Event::Live { .. }));
        // Another tool on the same game, while the link keeps watching.
        let mut tool = LiveClient::connect(&Endpoint::Tcp(game.port)).unwrap();
        for _ in 0..5 {
            assert_eq!(tool.live().unwrap().map(), Some("area_a"));
            tool.pause().unwrap();
        }
        wait_for(
            &link,
            |event| matches!(event, Event::Live { view, .. } if view.tick > 5),
        );
        let tool_commands = game.commands_of(2);
        assert_eq!(tool_commands.len(), 10, "{tool_commands:?}");
        assert!(game.commands_of(1).iter().all(|command| command != "pause"));
    }
}
