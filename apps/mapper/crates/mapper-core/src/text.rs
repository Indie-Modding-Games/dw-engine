//! Every text the editor shows, in one place for a later translation. The editor is for the team,
//! so it speaks Spanish.

pub const APP_TITLE: &str = "dwe mapper";

pub const MENU_FILE: &str = "Archivo";
pub const OPEN_CONTENT: &str = "Abrir carpeta de contenido…";
pub const NO_CONTENT: &str =
    "Abre una carpeta de contenido (el paquete base y los demás) con Archivo o --content.";
pub const NO_MAP: &str = "Elige un mapa en la lista de la izquierda.";
pub const QUIT: &str = "Salir";

pub const MAPS: &str = "Mapas";
pub const FILTER_HINT: &str = "Filtrar por id o nombre";
pub const GROUP_BY: &str = "Agrupar por";
pub const GROUP_BY_NAME: &str = "Nombre de zona";
pub const GROUP_BY_PREFIX: &str = "Prefijo del id";
pub const UNNAMED_GROUP: &str = "(sin nombre)";

pub const LAYERS: &str = "Capas";
pub const LAYER_WALLS: &str = "Paredes";
pub const LAYER_TRIGGERS: &str = "Disparadores";
pub const LAYER_EXITS: &str = "Salidas";
pub const LAYER_NPCS: &str = "NPC";
pub const LAYER_WAYPOINTS: &str = "Puntos de ruta";
pub const LAYER_DOORS: &str = "Puertas";
pub const LAYER_TOILET: &str = "Baño";
pub const LAYER_GRID: &str = "Rejilla";

pub const TIME_OF_DAY: &str = "Fondo";
pub const VARIANT_DAY: &str = "Día";
pub const VARIANT_DUSK: &str = "Atardecer";
pub const VARIANT_NIGHT: &str = "Noche";

pub const FIT_VIEW: &str = "Encajar";
pub const BACK: &str = "Atrás";

pub const MAP_INFO: &str = "Mapa";
pub const EXITS: &str = "Salidas";
pub const NPCS: &str = "NPC";
pub const HOVER: &str = "Cursor";
pub const CELL: &str = "Celda";
pub const WORLD: &str = "Mundo";
pub const ORIGINAL: &str = "Original";
pub const VALUE: &str = "Valor";
pub const GO: &str = "Ir";
pub const NOWHERE: &str = "sin destino";
pub const CONFLICT: &str = "destino en conflicto (el juego la desactiva)";
pub const UNKNOWN_ID: &str = "id desconocido; va por el índice del original";
pub const NO_TRIGGER_CELLS: &str = "sin celdas de disparo";
pub const NO_BACKGROUND: &str = "Este mapa no tiene fondo en los paquetes.";
pub const PATCHED_BY: &str = "parcheado por";
pub const EXIT_PATCHED_BY: &str = "parche de";

pub const KIND_FREE: &str = "libre";
pub const KIND_BLOCKED: &str = "pared";
pub const BAND_STEP: &str = "al pisar";
pub const BAND_CONFIRM: &str = "al confirmar";
pub const BAND_EXIT: &str = "salida";
pub const BAND_TOILET: &str = "baño";
pub const BAND_OTHER: &str = "sin uso";

pub const LIVE: &str = "En vivo";
pub const LIVE_ENDPOINT_HINT: &str = "unix:/ruta/al/socket o tcp:PUERTO";
pub const LIVE_CONNECT: &str = "Conectar";
pub const LIVE_DISCONNECT: &str = "Desconectar";
pub const LIVE_FOLLOW: &str = "Ir al mapa del juego";
pub const LIVE_RELOAD: &str = "Recargar el mapa en el juego";
pub const LIVE_OVERLAY: &str = "Capa de depuración del juego";
pub const LIVE_STORY: &str = "Leer la historia";
pub const LIVE_PAUSED: &str = "en pausa";
pub const LIVE_RELOADED: &str = "Recargado";
pub const STORY: &str = "Historia";
pub const STORY_TRIGGERS: &str = "Disparadores activos";
pub const STORY_VARIABLES: &str = "Variables distintas de 0";
pub const LIVE_OFF: &str = "Sin conexión";
pub const LIVE_ON: &str = "Conectado";

pub const SKIPPED_MOD: &str = "Mod que se queda fuera";
pub const ARCHIVES_NOT_READ: &str = "Archivos .pak que el editor no lee (solo carpetas)";
pub const DEFAULT_BANDS: &str = "se usan las bandas del original";

/// A short word for a trigger value's band.
pub fn band_name(band: dwe_map::TriggerBand) -> String {
    match band {
        dwe_map::TriggerBand::Step => BAND_STEP.to_owned(),
        dwe_map::TriggerBand::Confirm => BAND_CONFIRM.to_owned(),
        dwe_map::TriggerBand::Exit(exit) => format!("{BAND_EXIT} {exit}"),
        dwe_map::TriggerBand::Toilet => BAND_TOILET.to_owned(),
        dwe_map::TriggerBand::Other => BAND_OTHER.to_owned(),
    }
}

/// A story number with its label from `story/names`, if it has one: `12 door_open`.
pub fn named(number: u32, name: Option<&str>) -> String {
    match name {
        Some(name) => format!("{number} {name}"),
        None => number.to_string(),
    }
}

/// The label of a background variant.
pub fn variant_name(variant: &str) -> String {
    match variant {
        "day" => VARIANT_DAY.to_owned(),
        "dusk" => VARIANT_DUSK.to_owned(),
        "night" => VARIANT_NIGHT.to_owned(),
        other => other.to_owned(),
    }
}
