//! The link to a running game: connect, follow its map, have it read the map again (after each
//! save, when "recargar al guardar" is on), mirror its debug overlay and read the story.

use mapper_core::text;
use mapper_live::{Endpoint, LiveClient, StoryState};

/// The link to a running game.
pub struct LivePanel {
    pub endpoint: String,
    pub client: Option<LiveClient>,
    pub state: String,
    /// Whether the game's map debug overlay is on, as the game said last.
    pub overlay: bool,
    /// The story's state, once read.
    pub story: Option<StoryState>,
}

impl Default for LivePanel {
    fn default() -> Self {
        Self {
            endpoint: String::new(),
            client: None,
            state: text::LIVE_OFF.to_owned(),
            overlay: false,
            story: None,
        }
    }
}

impl LivePanel {
    pub fn connect(&mut self) {
        let connected =
            Endpoint::parse(&self.endpoint).and_then(|endpoint| LiveClient::connect(&endpoint));
        match connected {
            Ok(client) => {
                self.client = Some(client);
                self.state = text::LIVE_ON.to_owned();
                self.read_status();
            }
            Err(error) => {
                self.client = None;
                self.state = error.to_string();
            }
        }
    }

    /// Reads the game's status: its mode and tick for the panel, and whether its overlay is on.
    pub fn read_status(&mut self) {
        self.game_map();
    }

    /// The map the game is playing, if a game is being played.
    pub fn game_map(&mut self) -> Option<String> {
        let client = self.client.as_mut()?;
        match client.status() {
            Ok(status) => {
                let paused = if status.paused {
                    format!(" · {}", text::LIVE_PAUSED)
                } else {
                    String::new()
                };
                self.state = format!(
                    "{} · {} · tick {}{paused}",
                    text::LIVE_ON,
                    status.mode,
                    status.tick
                );
                if let Some(overlay) = status.overlay {
                    self.overlay = overlay;
                }
                status.map
            }
            Err(error) => {
                self.fail(error);
                None
            }
        }
    }

    pub fn is_connected(&self) -> bool {
        self.client.is_some()
    }

    /// Asks the game to list its packs again and read its map again where the player stands (the
    /// channel's `reload_map` rescans first).
    pub fn reload(&mut self) {
        let Some(client) = self.client.as_mut() else {
            return;
        };
        match client.reload_map(None) {
            Ok(reloaded) => self.state = format!("{}: {}", text::LIVE_RELOADED, reloaded.map),
            Err(error) => self.fail(error),
        }
    }

    pub fn set_overlay(&mut self, on: bool) {
        let Some(client) = self.client.as_mut() else {
            return;
        };
        match client.overlay(Some(on)) {
            Ok(shown) => self.overlay = shown,
            Err(error) => self.fail(error),
        }
    }

    pub fn read_story(&mut self) {
        let Some(client) = self.client.as_mut() else {
            return;
        };
        match client.story_state() {
            Ok(story) => self.story = Some(story),
            Err(error) => self.fail(error),
        }
    }

    fn fail(&mut self, error: mapper_live::Error) {
        if matches!(
            error,
            mapper_live::Error::Closed | mapper_live::Error::Io(_)
        ) {
            self.client = None;
            self.story = None;
        }
        self.state = error.to_string();
    }
}
