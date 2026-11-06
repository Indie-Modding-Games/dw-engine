//! The layers drawn over the background, each shown or hidden on its own.

use crate::text;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Layer {
    /// Blocked collision cells.
    Walls,
    /// Trigger cells, coloured by band.
    Triggers,
    /// Exits: their index and target, and where the player arrives.
    Exits,
    Npcs,
    Waypoints,
    Doors,
    Toilet,
    /// The collision grid's lines.
    Grid,
}

impl Layer {
    pub const ALL: [Layer; 8] = [
        Layer::Walls,
        Layer::Triggers,
        Layer::Exits,
        Layer::Npcs,
        Layer::Waypoints,
        Layer::Doors,
        Layer::Toilet,
        Layer::Grid,
    ];

    pub fn label(self) -> &'static str {
        match self {
            Layer::Walls => text::LAYER_WALLS,
            Layer::Triggers => text::LAYER_TRIGGERS,
            Layer::Exits => text::LAYER_EXITS,
            Layer::Npcs => text::LAYER_NPCS,
            Layer::Waypoints => text::LAYER_WAYPOINTS,
            Layer::Doors => text::LAYER_DOORS,
            Layer::Toilet => text::LAYER_TOILET,
            Layer::Grid => text::LAYER_GRID,
        }
    }

    /// The layer by its command-line name.
    pub fn named(name: &str) -> Option<Layer> {
        Some(match name {
            "walls" => Layer::Walls,
            "triggers" => Layer::Triggers,
            "exits" => Layer::Exits,
            "npcs" => Layer::Npcs,
            "waypoints" => Layer::Waypoints,
            "doors" => Layer::Doors,
            "toilet" => Layer::Toilet,
            "grid" => Layer::Grid,
            _ => return None,
        })
    }

    fn bit(self) -> u16 {
        1 << (self as u16)
    }
}

/// Which layers are shown.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct LayerSet(u16);

impl Default for LayerSet {
    /// Everything but the grid lines.
    fn default() -> Self {
        let mut set = Self(0);
        for layer in Layer::ALL {
            set.set(layer, layer != Layer::Grid);
        }
        set
    }
}

impl LayerSet {
    pub fn none() -> Self {
        Self(0)
    }

    pub fn shows(self, layer: Layer) -> bool {
        self.0 & layer.bit() != 0
    }

    pub fn set(&mut self, layer: Layer, shown: bool) {
        if shown {
            self.0 |= layer.bit();
        } else {
            self.0 &= !layer.bit();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn layers_toggle_on_their_own() {
        let mut set = LayerSet::default();
        assert!(set.shows(Layer::Walls));
        assert!(!set.shows(Layer::Grid));
        set.set(Layer::Walls, false);
        set.set(Layer::Grid, true);
        assert!(!set.shows(Layer::Walls));
        assert!(set.shows(Layer::Grid));
        assert!(set.shows(Layer::Npcs));
        assert_eq!(Layer::named("toilet"), Some(Layer::Toilet));
        assert_eq!(Layer::named("nothing"), None);
    }
}
