//! The map editor's link to a running dwe-game through its remote control channel
//! (docs/guides/control-remoto.md): one JSON request per line, one JSON answer per line, over a
//! Unix socket or a loopback TCP port. The editor asks which map the game shows and, once the
//! game has the command, tells it to read the map again after an edit.

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
    pub tick: i64,
    /// title, movie, interview, field or battle.
    pub mode: String,
    /// The map shown, in the field.
    pub map: Option<String>,
}

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
            tick: answer.get("tick").and_then(Value::as_i64).unwrap_or(0),
            mode: answer
                .get("mode")
                .and_then(Value::as_str)
                .unwrap_or_default()
                .to_owned(),
            map: answer
                .get("map")
                .and_then(Value::as_str)
                .filter(|map| !map.is_empty())
                .map(str::to_owned),
        })
    }

    /// Asks the game to read the current map again from the packs and re-enter it where the
    /// player stands (the channel's `reload_map`).
    pub fn reload_map(&mut self) -> Result<()> {
        self.request("reload_map", Map::new()).map(|_| ())
    }
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
        let port = fake_game(vec![
            json!({"ok": true, "tick": 42, "mode": "field", "map": "area_a"}),
            json!({"ok": false, "error": "unknown command 'reload_map'"}),
        ]);
        let mut client = LiveClient::connect(&Endpoint::Tcp(port)).unwrap();
        assert_eq!(
            client.status().unwrap(),
            GameStatus {
                tick: 42,
                mode: "field".into(),
                map: Some("area_a".into())
            }
        );
        let error = client.reload_map().unwrap_err().to_string();
        assert!(error.contains("unknown command"), "{error}");
        assert!(matches!(
            client.status(),
            Err(Error::Closed) | Err(Error::Io(_))
        ));
    }
}
