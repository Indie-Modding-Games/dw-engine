//! A connection to the game's channel that asks one thing at a time and waits for its answer.

use std::io::{BufRead, BufReader, Write};
use std::net::{Ipv4Addr, SocketAddr, TcpStream};
use std::time::Duration;

use serde_json::{Map, Value, json};

use crate::answers::{
    GameStatus, LiveView, LogEntry, PreviewApplied, ReloadedMap, StoryState, StoryWrite, non_empty,
};
use crate::{Endpoint, Error, Result};

/// How long a request waits for its answer.
const ANSWER_TIMEOUT: Duration = Duration::from_secs(10);

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
                // A game with all its clients taken says so once, without an id, and closes.
                if answer.get("id") == Some(&Value::Null)
                    && answer.get("ok") == Some(&Value::Bool(false))
                {
                    return Err(refusal(command, &answer));
                }
                continue;
            }
            if answer.get("ok") != Some(&Value::Bool(true)) {
                return Err(refusal(command, &answer));
            }
            return Ok(answer);
        }
    }

    pub fn status(&mut self) -> Result<GameStatus> {
        Ok(GameStatus::read(&self.request("status", Map::new())?))
    }

    /// The game's state and its characters (the channel's `live`).
    pub fn live(&mut self) -> Result<LiveView> {
        let answer = self.request("live", Map::new())?;
        serde_json::from_value(Value::Object(answer))
            .map_err(|error| Error::BadAnswer(error.to_string()))
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

    /// The story's triggers, variables and running sections (the channel's `story_state`).
    pub fn story_state(&mut self) -> Result<StoryState> {
        Ok(StoryState::read(&self.request("story_state", Map::new())?))
    }

    /// Shows the map with a patch over it, in memory only (the channel's `preview_map`): `patch`
    /// is the text of a map patch (docs/formats/mapas.md, "Parches"), `pack` the pack the edit is
    /// for, whose rights it takes.
    pub fn preview_map(
        &mut self,
        map: &str,
        patch: &str,
        pack: Option<&str>,
    ) -> Result<PreviewApplied> {
        let mut fields = Map::new();
        fields.insert("map".into(), json!(map));
        fields.insert("patch".into(), json!(patch));
        if let Some(pack) = pack {
            fields.insert("pack".into(), json!(pack));
        }
        let answer = self.request("preview_map", fields)?;
        Ok(match answer.get("applied").and_then(Value::as_str) {
            Some("collision") => PreviewApplied::Collision,
            _ => PreviewApplied::Reentered,
        })
    }

    /// Drops the preview, showing the packs' map again; answers whether there was one.
    pub fn preview_clear(&mut self) -> Result<bool> {
        let answer = self.request("preview_clear", Map::new())?;
        Ok(answer
            .get("cleared")
            .and_then(Value::as_bool)
            .unwrap_or(false))
    }

    /// The warnings and errors the game logged after `since` (all it keeps with None), and the
    /// number of the newest one.
    pub fn log_tail(&mut self, since: Option<u64>) -> Result<(Vec<LogEntry>, u64)> {
        let mut fields = Map::new();
        if let Some(since) = since {
            fields.insert("since".into(), json!(since));
        }
        let answer = self.request("log_tail", fields)?;
        let next = answer.get("next").and_then(Value::as_u64).unwrap_or(0);
        let entries = answer
            .get("entries")
            .cloned()
            .map(serde_json::from_value::<Vec<LogEntry>>)
            .transpose()
            .map_err(|error| Error::BadAnswer(error.to_string()))?
            .unwrap_or_default();
        Ok((entries, next))
    }

    /// Writes a story trigger or variable (the channel's `story_set`).
    pub fn story_set(&mut self, write: StoryWrite) -> Result<()> {
        let mut fields = Map::new();
        let (kind, number, value) = match write {
            StoryWrite::Trigger { number, on } => ("trigger", number, json!(on)),
            StoryWrite::Variable { number, value } => ("var", number, json!(value)),
        };
        fields.insert("kind".into(), json!(kind));
        fields.insert("number".into(), json!(number));
        fields.insert("value".into(), json!(value));
        self.request("story_set", fields).map(|_| ())
    }

    /// Puts a character somewhere: `place("<who> <x> <z> <rotation>")` in the original's units
    /// and axes (apps/game/src/place_command.hpp).
    pub fn place(&mut self, text: &str) -> Result<()> {
        let mut fields = Map::new();
        fields.insert("text".into(), json!(text));
        self.request("place", fields).map(|_| ())
    }

    pub fn pause(&mut self) -> Result<GameStatus> {
        Ok(GameStatus::read(&self.request("pause", Map::new())?))
    }

    pub fn resume(&mut self) -> Result<GameStatus> {
        Ok(GameStatus::read(&self.request("resume", Map::new())?))
    }

    /// Runs that many ticks (all of them while paused); answers once they ran.
    pub fn advance(&mut self, ticks: u64) -> Result<()> {
        let mut fields = Map::new();
        fields.insert("ticks".into(), json!(ticks));
        self.request("advance", fields).map(|_| ())
    }

    /// Holds keys for some ticks, after the presses already queued (`space` confirms).
    pub fn press(&mut self, keys: &[&str], ticks: u64) -> Result<()> {
        let mut fields = Map::new();
        fields.insert("keys".into(), json!(keys));
        fields.insert("ticks".into(), json!(ticks));
        self.request("press", fields).map(|_| ())
    }
}

fn refusal(command: &str, answer: &Map<String, Value>) -> Error {
    let message = answer
        .get("error")
        .and_then(Value::as_str)
        .unwrap_or("no reason given")
        .to_owned();
    Error::Refused {
        command: command.to_owned(),
        message,
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use std::io::{BufRead, BufReader, Write};
    use std::net::TcpListener;

    use super::*;
    use crate::answers::{Actor, LogLevel, Section, ViewRect};

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
                   "held": [], "pending": 0, "overlay": true}),
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
                map: Some("area_a".into()),
                overlay: Some(true),
            }
        );
        let before_a_game = client.status().unwrap();
        assert_eq!((before_a_game.map, before_a_game.overlay), (None, None));
        let error = client.reload_map(None).unwrap_err();
        assert!(error.to_string().contains("cannot reload"), "{error}");
        assert!(!error.is_lost());
        let gone = client.status().unwrap_err();
        assert!(gone.is_lost(), "{gone}");
    }

    /// A fake game that answers every request with `answer(request)` and records the requests.
    /// It takes `clients` connections one after the other.
    pub(crate) fn echoing_game(
        clients: usize,
        answer: fn(&Value) -> Value,
    ) -> (u16, std::sync::mpsc::Receiver<Value>) {
        let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, 0)).unwrap();
        let port = listener.local_addr().unwrap().port();
        let (sent, seen) = std::sync::mpsc::channel();
        std::thread::spawn(move || {
            for _ in 0..clients {
                let Ok((stream, _)) = listener.accept() else {
                    return;
                };
                let mut reader = BufReader::new(stream.try_clone().unwrap());
                let mut writer = stream;
                let mut line = String::new();
                while reader.read_line(&mut line).unwrap_or(0) > 0 {
                    let request: Value = serde_json::from_str(&line).unwrap();
                    let mut reply = answer(&request);
                    reply["id"] = request["id"].clone();
                    if reply.get("ok").is_none() {
                        reply["ok"] = json!(true);
                    }
                    if writeln!(writer, "{reply}").is_err() {
                        break;
                    }
                    let _ = sent.send(request);
                    line.clear();
                }
            }
        });
        (port, seen)
    }

    #[test]
    fn sends_the_editor_commands_as_the_game_reads_them() {
        let (port, seen) = echoing_game(1, |request| match request["cmd"].as_str() {
            Some("reload_map") => json!({"map": "area_a", "exit": request.get("exit").cloned()
                .unwrap_or(Value::Null), "tick": 9}),
            Some("overlay") => json!({"overlay": request.get("on").cloned()
                .unwrap_or(json!(true))}),
            Some("story_state") => json!({"tick": 5, "story": {"map": "area_b",
                "triggers": [3, 40], "variables": {"12": 3, "2": -1},
                "script": {"busy": true, "map_script": 7,
                           "sections": [{"script": 7, "section": 2}]},
                "stage": "e1", "camera": null}}),
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
                busy: true,
                map_script: Some(7),
                sections: vec![Section {
                    script: 7,
                    section: 2
                }],
                stage: Some("e1".into()),
            }
        );
        assert_eq!(next()["cmd"], "story_state");
    }

    #[test]
    fn sends_the_live_commands_as_the_game_reads_them() {
        let (port, seen) = echoing_game(1, |request| match request["cmd"].as_str() {
            Some("live") => json!({"tick": 5120, "paused": false, "mode": "field",
                "map": "area_a", "busy": false, "preview": null, "log": 12,
                "player": {"x": 256, "y": 0, "z": 1285.5, "yaw": 0},
                "partner": null,
                "npcs": [{"id": 11, "x": -839, "y": 0, "z": -2210, "yaw": 0}],
                "view": {"x": 0, "y": 16, "w": 320, "h": 240}}),
            Some("preview_map") => json!({"map": request["map"], "applied":
                if request["patch"].as_str().unwrap().starts_with("collision") { "collision" }
                else { "reentered" }, "tick": 1}),
            Some("preview_clear") => json!({"map": "area_a", "cleared": true}),
            Some("log_tail") => json!({"next": 13, "entries": [
                {"seq": 12, "level": "warning", "text": "a"},
                {"seq": 13, "level": "error", "text": "b"}]}),
            Some("story_set") if request["number"] == 99 => {
                json!({"ok": false, "error": "story_set: a story script runs"})
            }
            Some("pause" | "resume") => json!({"tick": 3, "paused": request["cmd"] == "pause",
                "mode": "field", "map": "area_a"}),
            _ => json!({}),
        });
        let mut client = LiveClient::connect(&Endpoint::Tcp(port)).unwrap();
        let next = || seen.recv_timeout(ANSWER_TIMEOUT).unwrap();

        let live = client.live().unwrap();
        assert_eq!(next()["cmd"], "live");
        assert_eq!(live.map(), Some("area_a"));
        assert_eq!(
            (live.tick, live.log, live.preview.clone()),
            (5120, 12, None)
        );
        let player = live.player.unwrap();
        assert_eq!((player.x, player.z, player.id), (256.0, 1285.5, None));
        assert_eq!(player.facing(), [0.0, 1.0], "yaw 0 faces +Z");
        assert_eq!(live.partner, None);
        assert_eq!(
            live.npcs,
            vec![Actor {
                id: Some(11),
                x: -839.0,
                y: 0.0,
                z: -2210.0,
                yaw: 0.0
            }]
        );
        assert_eq!(
            live.view,
            Some(ViewRect {
                x: 0.0,
                y: 16.0,
                w: 320.0,
                h: 240.0
            })
        );

        let patch = "collision:\n  cells: []\n";
        assert_eq!(
            client.preview_map("area_a", patch, Some("my_mod")).unwrap(),
            PreviewApplied::Collision
        );
        let request = next();
        assert_eq!(
            (&request["map"], &request["patch"], &request["pack"]),
            (&json!("area_a"), &json!(patch), &json!("my_mod"))
        );
        assert_eq!(
            client.preview_map("area_a", "npcs: []\n", None).unwrap(),
            PreviewApplied::Reentered
        );
        assert!(next().get("pack").is_none());
        assert!(client.preview_clear().unwrap());
        assert_eq!(next()["cmd"], "preview_clear");

        let (entries, newest) = client.log_tail(Some(11)).unwrap();
        assert_eq!(next()["since"], 11);
        assert_eq!(newest, 13);
        assert_eq!(
            entries.iter().map(|entry| entry.level).collect::<Vec<_>>(),
            [LogLevel::Warning, LogLevel::Error]
        );

        client
            .story_set(StoryWrite::Trigger {
                number: 12,
                on: true,
            })
            .unwrap();
        let request = next();
        assert_eq!(
            (&request["kind"], &request["number"], &request["value"]),
            (&json!("trigger"), &json!(12), &json!(true))
        );
        client
            .story_set(StoryWrite::Variable {
                number: 4,
                value: 200,
            })
            .unwrap();
        assert_eq!(next()["kind"], "var");
        let refused = client
            .story_set(StoryWrite::Trigger {
                number: 99,
                on: false,
            })
            .unwrap_err();
        assert!(refused.to_string().contains("script runs"), "{refused}");
        next();

        client.place("player 100 -200 1024").unwrap();
        assert_eq!(next()["text"], "player 100 -200 1024");
        assert!(client.pause().unwrap().paused);
        next();
        assert!(!client.resume().unwrap().paused);
        next();
        client.advance(6).unwrap();
        assert_eq!(next()["ticks"], 6);
        client.press(&["space"], 4).unwrap();
        let request = next();
        assert_eq!(
            (&request["cmd"], &request["keys"], &request["ticks"]),
            (&json!("press"), &json!(["space"]), &json!(4))
        );
    }

    #[test]
    fn a_game_with_every_client_taken_is_a_refusal() {
        let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, 0)).unwrap();
        let port = listener.local_addr().unwrap().port();
        std::thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            // Read first: closing with a request unread would reset the connection.
            BufReader::new(stream.try_clone().unwrap())
                .read_line(&mut String::new())
                .unwrap();
            writeln!(
                stream,
                "{}",
                json!({"id": null, "ok": false,
                       "error": "control: too many clients (4 at most)"})
            )
            .unwrap();
        });
        let mut client = LiveClient::connect(&Endpoint::Tcp(port)).unwrap();
        let error = client.status().unwrap_err();
        assert!(error.to_string().contains("too many clients"), "{error}");
    }
}
