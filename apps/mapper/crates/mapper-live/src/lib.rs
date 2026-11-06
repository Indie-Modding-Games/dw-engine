//! The map editor's link to a running dwe-game through its remote control channel
//! (docs/guides/control-remoto.md): one JSON request per line, one JSON answer per line, over a
//! Unix socket or a loopback TCP port (apps/game/src/remote_control.cpp). The game takes several
//! clients at once, so the editor and other tools (the MCP server, a script) can talk to the same
//! game.
//!
//! [`LiveClient`] asks one thing at a time and waits for its answer. [`LiveLink`] runs a client on
//! its own thread for an interface: it watches the game at a steady pace (`live`), reconnects when
//! the game goes away and comes back, and runs the editor's commands in order, so the interface
//! never waits on the game.

mod answers;
mod client;
mod link;

pub use answers::{
    Actor, GameStatus, LiveView, LogEntry, LogLevel, PreviewApplied, ReloadedMap, Section,
    StoryState, StoryWrite, ViewRect,
};
pub use client::{LiveClient, MOST_EXITS};
pub use link::{Command, Done, Event, LINK_POLL, LiveLink};

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

impl Error {
    /// Whether the connection is gone (the game closed, crashed or stopped answering), as opposed
    /// to a request the game refused.
    pub fn is_lost(&self) -> bool {
        matches!(self, Self::Closed | Self::Io(_) | Self::BadAnswer(_))
    }
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

#[cfg(test)]
mod tests {
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
}
