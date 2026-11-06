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
pub const LAYER_LIVE: &str = "Juego";

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
pub const LIVE_CONNECTING: &str = "Conectando…";
pub const LIVE_RECONNECTING: &str = "Reconectando";
pub const LIVE_GAME: &str = "Juego";
pub const LIVE_FOLLOW_MODE: &str = "Seguir al juego";
pub const LIVE_FOLLOW_HINT: &str = "Cuando el juego cambia de mapa, el editor abre ese mapa (pregunta antes si hay cambios sin guardar)";
pub const LIVE_PREVIEW: &str = "Vista previa en vivo";
pub const LIVE_PREVIEW_HINT: &str = "Enseña en el juego los cambios sin guardar de este mapa. Solo en \
     memoria: nada se escribe en disco y se quita al guardar, al desconectar o al cerrar";
pub const LIVE_PREVIEW_SHOWN: &str = "vista previa";
pub const LIVE_PREVIEW_COLLISION: &str = "Vista previa: colisión cambiada en el sitio";
pub const LIVE_PREVIEW_REENTERED: &str = "Vista previa: mapa vuelto a entrar";
pub const LIVE_PREVIEW_CLEARED: &str = "Vista previa quitada";
pub const LIVE_PAUSE: &str = "Pausa";
pub const LIVE_RESUME: &str = "Seguir";
pub const LIVE_STEP: &str = "Paso";
pub const LIVE_STEP_HINT: &str = "Avanza un tick (1/60 s) con el juego en pausa";
pub const LIVE_STEP_SECOND: &str = "+1 s";
pub const LIVE_BUSY: &str = "historia en curso";
pub const LIVE_PLACE_HERE: &str = "Llevar al jugador aquí";
pub const LIVE_TEST_EXIT: &str = "Probar esta salida";
pub const LIVE_TEST_EXIT_HINT: &str = "Pone al jugador en la primera casilla de la salida y deja correr el juego para que cambie de mapa";
pub const LIVE_TO_ARRIVAL: &str = "Llevar al jugador a la llegada";
pub const LIVE_PLACED: &str = "Jugador colocado";
pub const LIVE_PLAYER: &str = "jugador";
pub const LIVE_PARTNER: &str = "compañero";
pub const LIVE_NPC: &str = "NPC";
pub const LIVE_NOT_SHOWN: &str = "El juego está en otro mapa";
pub const LIVE_LOG: &str = "Registro del juego";
pub const LIVE_LOG_EMPTY: &str = "Sin avisos ni errores";
pub const LIVE_PROBLEMS: &str = "El juego respondió con errores";
pub const CLEAR: &str = "Limpiar";
pub const STORY_FOLLOW: &str = "Actualizar sola";
pub const STORY_FILTER_HINT: &str = "Filtrar por número o nombre";
pub const STORY_SCRIPT: &str = "Script del mapa";
pub const STORY_SECTIONS: &str = "Secciones en curso";
pub const STORY_STAGE: &str = "Etapa";
pub const STORY_NONE: &str = "ninguna";
pub const STORY_TRIGGER: &str = "Disparador";
pub const STORY_VARIABLE: &str = "Variable";
pub const STORY_SET_ON: &str = "Activar";
pub const STORY_SET_OFF: &str = "Quitar";
pub const STORY_SET_VALUE: &str = "Poner";
pub const STORY_SET_HINT: &str = "Para probar eventos: escribe en el estado de la historia del juego (no mientras corre un script)";

pub const SKIPPED_MOD: &str = "Mod que se queda fuera";
pub const ARCHIVES_NOT_READ: &str = "Archivos .pak que el editor no lee (solo carpetas)";
pub const DEFAULT_BANDS: &str = "se usan las bandas del original";

pub const MENU_EDIT: &str = "Editar";
pub const RELOAD_CONTENT: &str = "Recargar contenido";
pub const UNDO: &str = "Deshacer";
pub const REDO: &str = "Rehacer";
pub const SAVE_PATCH: &str = "Guardar (parche)";
pub const SAVE_WHOLE: &str = "Guardar mapa completo";
pub const SHORTCUTS: &str = "Atajos de teclado";

pub const TOOLS: &str = "Herramientas";
pub const TOOL_SELECT: &str = "Seleccionar";
pub const TOOL_BRUSH: &str = "Pincel";
pub const TOOL_RECTANGLE: &str = "Rectángulo";
pub const TOOL_FILL: &str = "Relleno";
pub const TOOL_EYEDROPPER: &str = "Cuentagotas";
pub const BRUSH_SIZE: &str = "Tamaño";
pub const PAINT_VALUE: &str = "Valor";
pub const VALUE_WALL: &str = "Pared";
pub const VALUE_FREE: &str = "Libre";
pub const VALUE_STEP: &str = "Al pisar";
pub const VALUE_CONFIRM: &str = "Al confirmar";
pub const VALUE_EXIT: &str = "Salida";
pub const VALUE_TOILET: &str = "Baño";
pub const VALUE_RAW: &str = "Crudo";

pub const INSPECTOR: &str = "Selección";
pub const NOTHING_SELECTED: &str =
    "Nada seleccionado: elige una salida, un NPC o un punto de ruta con Seleccionar.";
pub const EXIT: &str = "Salida";
pub const TARGET_MAP: &str = "Mapa de destino";
pub const TARGET_MAP_NONE: &str = "(ninguno)";
pub const TARGET_INDEX: &str = "Índice del original";
pub const TARGET_EXIT: &str = "Salida de destino";
pub const ARRIVAL: &str = "Llegada (x, y, z)";
pub const FACING: &str = "Orientación";
pub const PAINT_EXIT_CELLS: &str = "Pintar sus celdas";
pub const ERASE_CELLS: &str = "Borrar celdas (libre)";
pub const OPEN_TARGET: &str = "Abrir el destino";
pub const NPC: &str = "NPC";
pub const POSITION: &str = "Posición (x, y, z)";
pub const NPC_FIELDS: &str = "Campos";
pub const PATH: &str = "Ruta";
pub const PART_WAIT: &str = "esperar";
pub const PART_WALK: &str = "andar";
pub const PART_RUN: &str = "correr";
pub const ADD_POINT: &str = "Añadir punto";
pub const REMOVE_POINT: &str = "Quitar";
pub const UP: &str = "Subir";
pub const DOWN: &str = "Bajar";
pub const FRAMES: &str = "fotogramas";
pub const READ_ONLY: &str = "Solo lectura";
pub const CAMERA: &str = "Cámara";
pub const LIGHTS: &str = "Luces";
pub const OBJECTS: &str = "Objetos";

pub const SAVE: &str = "Guardar";
pub const TARGET: &str = "Destino";
pub const NO_TARGET: &str = "Elige el paquete destino antes de guardar (Archivo → Destino).";
pub const TARGET_MOD: &str = "mod";
pub const TARGET_TEAM: &str = "equipo";
pub const NEW_PACK: &str = "Nuevo paquete de mod…";
pub const NO_TARGETS: &str = "Ningún paquete del contenido se puede escribir: crea uno nuevo.";
pub const PICK_FOLDER: &str = "Elegir…";
pub const NEW_PACK_ID: &str = "Id del paquete nuevo";
pub const NEW_PACK_FOLDER: &str = "Carpeta de mods";
pub const NEW_PACK_CREATE: &str = "Crear";
pub const NEW_PACK_DESCRIPTION: &str = "Cambios de mapas hechos con dwe-mapper";
pub const BAD_PACK_ID: &str = "Id de paquete no válido (1 a 64 de a-z, 0-9 y _)";
pub const PACK_EXISTS: &str = "Ya existe";
pub const PACK_NOT_MOUNTED: &str =
    "El paquete nuevo no se montó (mira los mods que se quedan fuera)";
pub const SAVE_REFUSED: &str = "No se guardó: mira los problemas.";
pub const SAVE_FAILED: &str = "No se pudo escribir";
pub const NOTHING_TO_SAVE: &str = "nada que escribir";
pub const SAVED_IN: &str = "Guardado en";
pub const SAVE_PROBLEMS: &str = "Problemas al guardar";
pub const RELOAD_ON_SAVE: &str = "Recargar al guardar";
pub const DIRTY: &str = "sin guardar";
pub const UNSAVED_TITLE: &str = "Cambios sin guardar";
pub const UNSAVED_QUESTION: &str = "El mapa tiene cambios sin guardar.";
pub const UNSAVED_SAVE: &str = "Guardar";
pub const UNSAVED_DISCARD: &str = "Descartar";
pub const UNSAVED_CANCEL: &str = "Cancelar";

/// The keyboard shortcuts, as the help shows them.
pub const SHORTCUT_LIST: [(&str, &str); 16] = [
    ("Ctrl+S", "Guardar (parche)"),
    ("Ctrl+Z", "Deshacer"),
    ("Ctrl+Y / Ctrl+Mayús+Z", "Rehacer"),
    ("V", "Seleccionar"),
    ("B", "Pincel"),
    ("R", "Rectángulo"),
    ("F", "Relleno"),
    (
        "I",
        "Cuentagotas (o Alt+clic con una herramienta de pintar)",
    ),
    ("[ / ]", "Pincel más pequeño / más grande"),
    ("X", "Valor: pared / libre"),
    ("Q / E", "Girar la selección 45° a un lado / al otro"),
    ("Supr", "Quitar el punto de ruta seleccionado"),
    (
        "W",
        "Añadir un punto de ruta al NPC seleccionado bajo el cursor",
    ),
    ("Esc", "Deseleccionar"),
    ("Ctrl+clic", "Ir adonde lleva la salida"),
    ("Mayús+arrastrar", "Orientar la selección hacia el cursor"),
];

/// The name of a tool.
pub fn tool_name(tool: crate::tools::Tool) -> &'static str {
    match tool {
        crate::tools::Tool::Select => TOOL_SELECT,
        crate::tools::Tool::Brush => TOOL_BRUSH,
        crate::tools::Tool::Rectangle => TOOL_RECTANGLE,
        crate::tools::Tool::Fill => TOOL_FILL,
        crate::tools::Tool::Eyedropper => TOOL_EYEDROPPER,
    }
}

/// What a save found, said for the problems panel.
pub fn problem(problem: &dwe_map::save::Problem) -> String {
    use dwe_map::save::Problem;
    match problem {
        Problem::TargetBelowData { data_pack } => format!(
            "El paquete destino está debajo de «{data_pack}», que trae el mapa entero: su parche no se aplicaría."
        ),
        Problem::NoData => "Ningún paquete trae este mapa.".to_owned(),
        Problem::DoesNotLoad(message) => format!("Lo escrito no carga: {message}"),
        Problem::ExitConflict { exit } => format!(
            "Salida {exit}: el id y el índice del original nombran mapas distintos (el juego la desactiva)."
        ),
        Problem::UnknownMap { exit, id } => format!("Salida {exit}: «{id}» no es ningún mapa."),
        Problem::DeadExit { exit } => format!(
            "Salida {exit}: tiene celdas de disparo pero no lleva a ningún sitio (warp muerto)."
        ),
        Problem::BadTargetExit { exit, target_exit } => {
            format!("Salida {exit}: la salida de destino {target_exit} no existe (0 a 9).")
        }
        Problem::BadPath { slot } => {
            format!("NPC {slot}: la ruta no cabe en el registro (8 partes, como mucho 8 puntos).")
        }
        Problem::NpcSlotsInPatch => {
            "Hay NPC añadidos o quitados: un parche no puede decirlo, guarda el mapa entero."
                .to_owned()
        }
        Problem::Overridden { packs } => format!(
            "Aviso: un parche de encima ({}) cambia lo mismo; el juego verá el suyo.",
            packs.join(", ")
        ),
    }
}

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

/// The game's mode as `status` and `live` name it.
pub fn game_mode(mode: &str) -> &str {
    match mode {
        "field" => "campo",
        "battle" => "batalla",
        "movie" => "vídeo",
        "title" => "título",
        "interview" => "preguntas iniciales",
        other => other,
    }
}

// The story.
pub const LAYER_EVENTS: &str = "Eventos";
pub const NOT_SAVED: &str = "sin guardar";
pub const TOO_MANY_NPCS: &str = "El mapa ya tiene 8 NPC, los que el juego admite.";
pub const NO_NPC_TEMPLATE: &str = "No hay ningún NPC que copiar en el contenido.";
pub const NO_ENTRY_SECTION: &str = "El mapa no tiene sección de entrada (script 0): el NPC nuevo no aparecerá hasta que una sección lo haga aparecer.";
pub const NO_CODE: &str = "La sección o la rutina abierta no tiene código.";
pub const NPCS_SAVE_WHOLE: &str = "Hay NPC añadidos o quitados: el mapa se guarda entero (los parches nombran los NPC por su posición).";
pub const WARPS_BY_HAND: &str =
    "El guardado cambia saltos de mapa de la historia; vuelve a sacar field/script_warps con";
pub const WARPS_MADE: &str = "field/script_warps sacado de nuevo";
pub const WARPS_FAILED: &str = "dwx script-warps falló";
