//! The map editor's link to a running dwe-game through its remote control channel
//! (docs/guides/control-remoto.md): one JSON request per line, one JSON answer per line, over a
//! Unix socket or a loopback TCP port (apps/game/src/remote_control.cpp). The editor asks which
//! map the game shows, tells it to read the map again after an edit, turns the game's map debug
//! overlay on and off and reads the story's state.

use std::io::{BufRead, BufReader, Write};
use std::net::{Ipv4Addr, SocketAddr, TcpStream};
use std::time::Duration;

use serde_json::{Map, Value, json};

/// How long a request waits for its answer.
const ANSWER_TIMEOUT: Duration = Duration::from_secs(10);

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("'{0}' is not an endpoint: use unix:<socket path> or tcp:<port>")]
    BadEndpoint(String),
    #[error("{0}")]
    Io(#[from] std::io::Error),
    #[error("the game closed the channel")]
    Closed,
    #[error("the game's answer is not JSON: {0}")]
    BadAnswer(String),
    #[error("the game refused '{command}': {message}")]
    Refused { command: String, message: String },
}

pub type Result<T> = std::result::Result<T, Error>;

/// Where the game listens: `unix:<path>` (or a bare path) or `tcp:<port>` on 127.0.0.1. The
/// channel is for this machine only, so other hosts are refused.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Endpoint {
    Unix(std::path::PathBuf),
    Tcp(u16),
}

impl Endpoint {
    pub fn parse(text: &str) -> Result<Self> {
        let text = text.trim();
        let bad = || Error::BadEndpoint(text.to_owned());
        if let Some(port) = text.strip_prefix("tcp:") {
            let port = match port.rsplit_once(':') {
                Some(("127.0.0.1" | "localhost", port)) => port,
                Some(_) => return Err(bad()),
                None => port,
            };
            return port.parse().map(Self::Tcp).map_err(|_| bad());
        }
        let path = text.strip_prefix("unix:").unwrap_or(text);
        if path.is_empty() {
            return Err(bad());
        }
        Ok(Self::Unix(path.into()))
    }
}

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
}

/// What `reload_map` did: the map entered again and the exit it was entered by, None when the
/// player kept their place.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ReloadedMap {
    pub map: String,
    pub exit: Option<i32>,
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
}

/// The exits a map has: `reload_map` takes 0 to 9.
pub const MOST_EXITS: i32 = 10;

trait Channel: std::io::Read + Write + Send {}
impl<T: std::io::Read + Write + Send> Channel for T {}

/// A connection to the game's channel.
pub struct LiveClient {
    writer: Box<dyn Channel>,
    reader: BufReader<Box<dyn Channel>>,
    next_id: u64,
}

impl LiveClient {
    pub fn connect(endpoint: &Endpoint) -> Result<Self> {
        let (writer, reader): (Box<dyn Channel>, Box<dyn Channel>) = match endpoint {
            Endpoint::Tcp(port) => {
                let address = SocketAddr::from((Ipv4Addr::LOCALHOST, *port));
                let stream = TcpStream::connect_timeout(&address, ANSWER_TIMEOUT)?;
                stream.set_read_timeout(Some(ANSWER_TIMEOUT))?;
                stream.set_nodelay(true)?;
                (Box::new(stream.try_clone()?), Box::new(stream))
            }
            #[cfg(unix)]
            Endpoint::Unix(path) => {
                let stream = std::os::unix::net::UnixStream::connect(path)?;
                stream.set_read_timeout(Some(ANSWER_TIMEOUT))?;
                (Box::new(stream.try_clone()?), Box::new(stream))
            }
            #[cfg(not(unix))]
            Endpoint::Unix(_) => {
                return Err(Error::Io(std::io::Error::new(
                    std::io::ErrorKind::Unsupported,
                    "this system has no Unix sockets: start the game with --control tcp:<port>",
                )));
            }
        };
        Ok(Self {
            writer,
            reader: BufReader::new(reader),
            next_id: 1,
        })
    }

    /// Sends a command with its fields and waits for its answer; an answer with `"ok": false` is
    /// an error with the game's message.
    pub fn request(
        &mut self,
        command: &str,
        fields: Map<String, Value>,
    ) -> Result<Map<String, Value>> {
        let id = self.next_id;
        self.next_id += 1;
        let mut request = Map::new();
        request.insert("id".into(), json!(id));
        request.insert("cmd".into(), json!(command));
        request.extend(fields);
        let mut line = Value::Object(request).to_string();
        line.push('\n');
        self.writer.write_all(line.as_bytes())?;
        self.writer.flush()?;
        loop {
            let mut answer = String::new();
            if self.reader.read_line(&mut answer)? == 0 {
                return Err(Error::Closed);
            }
            let Ok(Value::Object(answer)) = serde_json::from_str::<Value>(&answer) else {
                return Err(Error::BadAnswer(answer.trim().to_owned()));
            };
            // Answers to older requests (one that timed out) are skipped.
            if answer.get("id") != Some(&json!(id)) {
                continue;
            }
            if answer.get("ok") != Some(&Value::Bool(true)) {
                let message = answer
                    .get("error")
                    .and_then(Value::as_str)
                    .unwrap_or("no reason given")
                    .to_owned();
                return Err(Error::Refused {
                    command: command.to_owned(),
                    message,
                });
            }
            return Ok(answer);
        }
    }

    pub fn status(&mut self) -> Result<GameStatus> {
        let answer = self.request("status", Map::new())?;
        Ok(GameStatus {
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
        })
    }

    /// Asks the game to list its packs' files again and to read the current map again from them,
    /// then re-enter it where the player stands or, with an exit (0 to 9), by that exit (the
    /// channel's `reload_map`).
    pub fn reload_map(&mut self, exit: Option<i32>) -> Result<ReloadedMap> {
        let mut fields = Map::new();
        if let Some(exit) = exit {
            if !(0..MOST_EXITS).contains(&exit) {
                return Err(Error::Refused {
                    command: "reload_map".to_owned(),
                    message: format!("exit {exit} is not 0 to {}", MOST_EXITS - 1),
                });
            }
            fields.insert("exit".into(), json!(exit));
        }
        let answer = self.request("reload_map", fields)?;
        Ok(ReloadedMap {
            map: non_empty(answer.get("map")).unwrap_or_default(),
            exit: answer
                .get("exit")
                .and_then(Value::as_i64)
                .and_then(|exit| i32::try_from(exit).ok()),
        })
    }

    /// Asks the game to list its packs' files again (the channel's `rescan`).
    pub fn rescan(&mut self) -> Result<()> {
        self.request("rescan", Map::new()).map(|_| ())
    }

    /// Turns the game's map debug overlay on or off, or toggles it with None; answers whether it
    /// is on now (the channel's `overlay`).
    pub fn overlay(&mut self, on: Option<bool>) -> Result<bool> {
        let mut fields = Map::new();
        if let Some(on) = on {
            fields.insert("on".into(), json!(on));
        }
        let answer = self.request("overlay", fields)?;
        Ok(answer
            .get("overlay")
            .and_then(Value::as_bool)
            .unwrap_or(false))
    }

    /// The story's triggers and variables (the channel's `story_state`).
    pub fn story_state(&mut self) -> Result<StoryState> {
        let answer = self.request("story_state", Map::new())?;
        let story = answer.get("story").and_then(Value::as_object);
        let triggers = story
            .and_then(|story| story.get("triggers"))
            .and_then(Value::as_array)
            .map(|triggers| {
                triggers
                    .iter()
                    .filter_map(|trigger| u32::try_from(trigger.as_u64()?).ok())
                    .collect()
            })
            .unwrap_or_default();
        let mut variables: Vec<(u32, i64)> = story
            .and_then(|story| story.get("variables"))
            .and_then(Value::as_object)
            .map(|variables| {
                variables
                    .iter()
                    .filter_map(|(number, value)| Some((number.parse().ok()?, value.as_i64()?)))
                    .collect()
            })
            .unwrap_or_default();
        variables.sort_unstable();
        Ok(StoryState {
            tick: answer.get("tick").and_then(Value::as_u64).unwrap_or(0),
            map: non_empty(story.and_then(|story| story.get("map"))),
            triggers,
            variables,
        })
    }
}

/// A text field that is there and not empty.
fn non_empty(value: Option<&Value>) -> Option<String> {
    value
        .and_then(Value::as_str)
        .filter(|text| !text.is_empty())
        .map(str::to_owned)
}

#[cfg(test)]
mod tests {
    use std::io::{BufRead, BufReader, Write};
    use std::net::TcpListener;

    use super::*;

    #[test]
    fn parses_endpoints_and_refuses_other_hosts() {
        assert_eq!(Endpoint::parse("tcp:7000").unwrap(), Endpoint::Tcp(7000));
        assert_eq!(
            Endpoint::parse("tcp:127.0.0.1:7000").unwrap(),
            Endpoint::Tcp(7000)
        );
        assert_eq!(
            Endpoint::parse("unix:/tmp/game.sock").unwrap(),
            Endpoint::Unix("/tmp/game.sock".into())
        );
        assert_eq!(
            Endpoint::parse("/tmp/game.sock").unwrap(),
            Endpoint::Unix("/tmp/game.sock".into())
        );
        assert!(Endpoint::parse("tcp:192.168.1.2:7000").is_err());
        assert!(Endpoint::parse("tcp:port").is_err());
        assert!(Endpoint::parse("unix:").is_err());
    }

    /// A fake game: answers each request with the given answers, in order, echoing its id.
    fn fake_game(answers: Vec<Value>) -> u16 {
        let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, 0)).unwrap();
        let port = listener.local_addr().unwrap().port();
        std::thread::spawn(move || {
            let (stream, _) = listener.accept().unwrap();
            let mut reader = BufReader::new(stream.try_clone().unwrap());
            let mut writer = stream;
            for mut answer in answers {
                let mut line = String::new();
                if reader.read_line(&mut line).unwrap() == 0 {
                    return;
                }
                let request: Value = serde_json::from_str(&line).unwrap();
                // A stale answer first, which the client must skip.
                writeln!(writer, "{}", json!({"id": -1, "ok": true})).unwrap();
                answer["id"] = request["id"].clone();
                answer["cmd_seen"] = request["cmd"].clone();
                writeln!(writer, "{answer}").unwrap();
            }
        });
        port
    }

    #[test]
    fn asks_the_status_and_reports_refusals() {
        // The fields as remote_control.cpp writes them.
        let port = fake_game(vec![
            json!({"ok": true, "tick": 42, "paused": true, "mode": "field", "map": "area_a",
                   "held": [], "pending": 0}),
            json!({"ok": true, "tick": 1, "paused": false, "mode": "title", "map": "",
                   "held": [], "pending": 0}),
            json!({"ok": false, "error": "reload_map: this game cannot reload its map"}),
        ]);
        let mut client = LiveClient::connect(&Endpoint::Tcp(port)).unwrap();
        assert_eq!(
            client.status().unwrap(),
            GameStatus {
                tick: 42,
                paused: true,
                mode: "field".into(),
                map: Some("area_a".into())
            }
        );
        assert_eq!(client.status().unwrap().map, None);
        let error = client.reload_map(None).unwrap_err().to_string();
        assert!(error.contains("cannot reload"), "{error}");
        assert!(matches!(
            client.status(),
            Err(Error::Closed) | Err(Error::Io(_))
        ));
    }

    /// A fake game that answers every request with `answer(request)` and records the requests.
    fn echoing_game(answer: fn(&Value) -> Value) -> (u16, std::sync::mpsc::Receiver<Value>) {
        let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, 0)).unwrap();
        let port = listener.local_addr().unwrap().port();
        let (sent, seen) = std::sync::mpsc::channel();
        std::thread::spawn(move || {
            let (stream, _) = listener.accept().unwrap();
            let mut reader = BufReader::new(stream.try_clone().unwrap());
            let mut writer = stream;
            let mut line = String::new();
            while reader.read_line(&mut line).unwrap_or(0) > 0 {
                let request: Value = serde_json::from_str(&line).unwrap();
                let mut reply = answer(&request);
                reply["id"] = request["id"].clone();
                reply["ok"] = json!(true);
                writeln!(writer, "{reply}").unwrap();
                let _ = sent.send(request);
                line.clear();
            }
        });
        (port, seen)
    }

    #[test]
    fn sends_the_editor_commands_as_the_game_reads_them() {
        let (port, seen) = echoing_game(|request| match request["cmd"].as_str() {
            Some("reload_map") => json!({"map": "area_a", "exit": request.get("exit").cloned()
                .unwrap_or(Value::Null), "tick": 9}),
            Some("overlay") => json!({"overlay": request.get("on").cloned()
                .unwrap_or(json!(true))}),
            Some("story_state") => json!({"tick": 5, "story": {"map": "area_b",
                "triggers": [3, 40], "variables": {"12": 3, "2": -1}, "script": {}}}),
            _ => json!({}),
        });
        let mut client = LiveClient::connect(&Endpoint::Tcp(port)).unwrap();
        let next = || seen.recv_timeout(ANSWER_TIMEOUT).unwrap();

        assert_eq!(
            client.reload_map(None).unwrap(),
            ReloadedMap {
                map: "area_a".into(),
                exit: None
            }
        );
        let request = next();
        assert_eq!(request["cmd"], "reload_map");
        assert!(request.get("exit").is_none());

        assert_eq!(client.reload_map(Some(3)).unwrap().exit, Some(3));
        assert_eq!(next()["exit"], 3);
        assert!(client.reload_map(Some(10)).is_err());

        assert!(!client.overlay(Some(false)).unwrap());
        assert_eq!(next()["on"], false);
        assert!(client.overlay(None).unwrap());
        assert!(next().get("on").is_none());

        client.rescan().unwrap();
        assert_eq!(next()["cmd"], "rescan");

        assert_eq!(
            client.story_state().unwrap(),
            StoryState {
                tick: 5,
                map: Some("area_b".into()),
                triggers: vec![3, 40],
                variables: vec![(2, -1), (12, 3)],
            }
        );
        assert_eq!(next()["cmd"], "story_state");
    }
}
