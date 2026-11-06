//! What the editor decides about a running game without talking to it: which map to follow, which
//! preview the game should show, and where to put the player for a test. The window sends what
//! these say through the live link (crate mapper-live).

use dwe_map::TriggerBand;
use dwe_map::diff::{diff_patch, patch_text};
use dwe_map::world::{self, Cell};
use glam::Vec3;

use crate::session::Mapper;

/// The text of the game's `place` for the player at a world point, facing an original rotation
/// (4096 a turn): `player <x> <z> <rotation>` in the original's axes (x, -y, -z of the world).
pub fn place_player(world: Vec3, rotation: i32) -> String {
    format!(
        "player {} {} {}",
        world.x.round() as i32,
        (-world.z).round() as i32,
        rotation.rem_euclid(world::ORIGINAL_ANGLE_PER_TURN)
    )
}

/// The original rotation of a yaw as the game gives it (radians, facing `(sin yaw, cos yaw)`).
pub fn rotation_of_yaw(yaw: f32) -> i32 {
    world::rotation_facing(world::forward_of_yaw(yaw))
}

impl Mapper {
    /// The first trigger cell of an exit in reading order: where a test of the exit puts the
    /// player (the game fires a trigger when the player is put on it, as when stepping onto it).
    pub fn exit_cell(&self, exit: i32) -> Option<Cell> {
        let map = self.map.as_ref()?;
        map.document
            .collision
            .cells()
            .find(|&(_, value)| {
                matches!(self.bands.band(value), TriggerBand::Exit(e) if i32::from(e) == exit)
            })
            .map(|(cell, _)| cell)
    }

    /// The `place` text that puts the player where an exit of the shown map leaves them, facing
    /// its way.
    pub fn place_at_arrival(&self, exit: i32) -> Option<String> {
        let map = self.map.as_ref()?;
        let warp = map.document.warps.iter().find(|warp| warp.exit == exit)?;
        Some(format!(
            "player {} {} {}",
            warp.spawn.x,
            warp.spawn.z,
            warp.rotation.rem_euclid(world::ORIGINAL_ANGLE_PER_TURN)
        ))
    }

    /// The edit of the shown map as a patch over the map the packs give: its id and the patch's
    /// text, or None without a map or without changes.
    pub fn preview_patch(&self) -> Option<(String, String)> {
        let map = self.map.as_ref()?;
        let patch = diff_patch(&map.loaded, &map.document)?;
        Some((map.document.id.clone(), patch_text(&patch)))
    }
}

/// Follow mode: the map the game enters is opened once, when it changes, so a map opened by hand
/// stays until the game goes somewhere else (and an answer of "stay" to the unsaved-changes
/// question is not asked again and again).
#[derive(Debug, Default)]
pub struct Follow {
    last: Option<String>,
}

impl Follow {
    /// The map to open, given the game's map and the map shown.
    pub fn map_to_open(&mut self, game: Option<&str>, shown: Option<&str>) -> Option<String> {
        let game = game?;
        if self.last.as_deref() == Some(game) {
            return None;
        }
        self.last = Some(game.to_owned());
        (shown != Some(game)).then(|| game.to_owned())
    }

    /// Forgets the map followed: the next one the game says is opened (a new connection).
    pub fn reset(&mut self) {
        self.last = None;
    }
}

/// What to tell the game about the preview.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum PreviewStep {
    Send { map: String, patch: String },
    Clear,
    Wait,
}

/// Keeps the game's preview in step with the edit: sends a patch when it changes, sends it again
/// when the game dropped it (it left the map and came back), clears it when the edit is gone or
/// the preview is turned off. A patch the game refused is not sent again until it changes.
#[derive(Debug, Default)]
pub struct PreviewSync {
    sent: Option<(String, String)>,
    /// The game answered the patch sent.
    confirmed: bool,
    /// A clear was sent for a preview the game showed without this editor asking.
    clearing: bool,
}

impl PreviewSync {
    /// `wanted`: the map and patch to show, None for none; `game`: the map the game previews.
    pub fn step(&mut self, wanted: Option<(String, String)>, game: Option<&str>) -> PreviewStep {
        if game.is_none() {
            self.clearing = false;
        }
        match wanted {
            Some(wanted) => {
                let dropped = self.confirmed && game != Some(wanted.0.as_str());
                if self.sent.as_ref() == Some(&wanted) && !dropped {
                    return PreviewStep::Wait;
                }
                let (map, patch) = wanted.clone();
                self.sent = Some(wanted);
                self.confirmed = false;
                PreviewStep::Send { map, patch }
            }
            None if self.sent.take().is_some() => {
                self.confirmed = false;
                PreviewStep::Clear
            }
            None if game.is_some() && !self.clearing => {
                self.clearing = true;
                PreviewStep::Clear
            }
            None => PreviewStep::Wait,
        }
    }

    /// The game showed the patch sent.
    pub fn confirm(&mut self) {
        self.confirmed = self.sent.is_some();
    }

    /// Starts over (a new connection): what the game shows is learnt again.
    pub fn reset(&mut self) {
        *self = Self::default();
    }

    /// Whether a preview was asked for and not cleared.
    pub fn is_on(&self) -> bool {
        self.sent.is_some()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn places_in_the_original_axes() {
        assert_eq!(
            place_player(Vec3::new(150.4, 0.0, -1200.0), -1024),
            "player 150 1200 3072"
        );
        // Yaw 0 faces world +Z, the original's rotation 0.
        assert_eq!(rotation_of_yaw(0.0), 0);
        assert_eq!(
            rotation_of_yaw(world::yaw_from_original(1024)),
            1024,
            "a quarter turn comes back"
        );
    }

    #[test]
    fn follows_the_game_once_per_change() {
        let mut follow = Follow::default();
        assert_eq!(follow.map_to_open(None, Some("a")), None);
        assert_eq!(follow.map_to_open(Some("b"), Some("a")), Some("b".into()));
        // Asked again (the user kept their map): not again until the game moves.
        assert_eq!(follow.map_to_open(Some("b"), Some("a")), None);
        assert_eq!(follow.map_to_open(Some("c"), Some("a")), Some("c".into()));
        assert_eq!(follow.map_to_open(Some("d"), Some("d")), None);
        follow.reset();
        assert_eq!(follow.map_to_open(Some("d"), Some("a")), Some("d".into()));
    }

    #[test]
    fn keeps_the_preview_in_step() {
        let mut sync = PreviewSync::default();
        let edit = |patch: &str| Some(("a".to_owned(), patch.to_owned()));
        let send = |patch: &str| PreviewStep::Send {
            map: "a".into(),
            patch: patch.into(),
        };
        assert_eq!(sync.step(None, None), PreviewStep::Wait);
        assert_eq!(sync.step(edit("x"), None), send("x"));
        // Before the answer the game may still say it shows none: no second send.
        assert_eq!(sync.step(edit("x"), None), PreviewStep::Wait);
        sync.confirm();
        assert_eq!(sync.step(edit("x"), Some("a")), PreviewStep::Wait);
        assert_eq!(sync.step(edit("y"), Some("a")), send("y"));
        sync.confirm();
        // The game left the map and came back: the preview is gone, so it goes again.
        assert_eq!(sync.step(edit("y"), None), send("y"));
        // A refused patch (no confirm) is not sent again until it changes.
        assert_eq!(sync.step(edit("y"), None), PreviewStep::Wait);
        assert!(sync.is_on());
        assert_eq!(sync.step(None, Some("a")), PreviewStep::Clear);
        assert!(!sync.is_on());
        // A preview this editor did not ask for (an earlier session) is cleared once.
        sync.reset();
        assert_eq!(sync.step(None, Some("a")), PreviewStep::Clear);
        assert_eq!(sync.step(None, Some("a")), PreviewStep::Wait);
        assert_eq!(sync.step(None, None), PreviewStep::Wait);
    }
}
