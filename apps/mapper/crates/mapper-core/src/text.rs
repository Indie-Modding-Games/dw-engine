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
pub const NOTHING_SELECTED: &str = "Nada seleccionado: elige una salida, un NPC, un punto de ruta, \
     una puerta o el baño con Seleccionar.";
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
pub const SHORTCUT_LIST: [(&str, &str); 22] = [
    ("Ctrl+S", "Guardar (parche)"),
    ("Ctrl+Z", "Deshacer"),
    ("Ctrl+Y / Ctrl+Mayús+Z", "Rehacer"),
    ("Ctrl+F", "Buscar un mapa en la lista"),
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
    ("C", "Centrar la vista en la selección"),
    ("Esc", "Deseleccionar"),
    ("1 … 9, 0", LAYER_KEYS_HINT),
    (
        "Alt+0 … Alt+9",
        "Seleccionar la salida de ese número y centrarla",
    ),
    ("RePág / AvPág", "Mapa anterior / siguiente de la lista"),
    ("Ctrl+clic", "Ir adonde lleva la salida"),
    ("Mayús+arrastrar", "Orientar la selección hacia el cursor"),
    (
        "Clic en un evento",
        "Con la capa Eventos y Seleccionar: abre su sección",
    ),
];

/// What the number keys do, as the help and the layer bar say it.
pub const LAYER_KEYS_HINT: &str = "Mostrar u ocultar una capa: 1 paredes, 2 disparadores, 3 salidas, \
     4 NPC, 5 puntos de ruta, 6 puertas, 7 baño, 8 rejilla, 9 juego, 0 eventos";

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

// The events layer and panel.
pub const EVENTS: &str = "Eventos";
pub const EVENT_MAP_ENTRY: &str = "Al entrar al mapa";
pub const EVENT_MAP_LOADED: &str = "Mapa cargado";
pub const EVENT_STEP: &str = "Pisar";
pub const EVENT_CONFIRM: &str = "Botón";
pub const EVENT_TALK: &str = "Hablar NPC";
pub const EVENTS_FILTER_HINT: &str = "Filtrar por tipo o sección";
pub const EVENTS_NONE: &str = "Este mapa no tiene eventos.";
pub const EVENT_MISSING: &str =
    "El script no tiene esta sección: no pasa nada (se crea al editarla)";
pub const EVENT_PATCHED: &str = "parcheado por";
pub const EVENT_OPEN_HINT: &str = "Clic: abrir la sección y centrar la vista";
pub const JUMP_HINT: &str = "Clic: abrir el mapa de destino en esa salida";
pub const JUMP_ALWAYS: &str = "Siempre";
pub const JUMP_WHEN: &str = "Cuando";
pub const TEST_EVENT: &str = "Probar";
pub const TEST_EVENT_HINT: &str = "Probar el evento en el juego conectado";
pub const TEST_EVENT_OFFLINE: &str = "Conecta un juego (En vivo) para probar eventos";

/// The kind of an event, in words.
pub fn event_kind(kind: dwe_story::EventKind) -> String {
    use dwe_story::EventKind;
    match kind {
        EventKind::MapEntry => EVENT_MAP_ENTRY.to_owned(),
        EventKind::MapLoaded => EVENT_MAP_LOADED.to_owned(),
        EventKind::Step => EVENT_STEP.to_owned(),
        EventKind::Confirm => EVENT_CONFIRM.to_owned(),
        EventKind::Talk { slot } => format!("{EVENT_TALK} {slot}"),
    }
}

/// A section of a script: `script 12 · sección 0x33`.
pub fn section_name(script: u16, section: u16) -> String {
    format!("script {script} · sección {section:#04x}")
}

/// Where a jump of the story goes: `→ area_b salida 2`, or another script's section.
pub fn jump_label(jump: &dwe_story::events::Jump) -> String {
    match &jump.to_map {
        Some(map) => format!("→ {map} salida {}", jump.exit),
        None if jump.file_jump => {
            format!("→ script {} sección {:#04x}", jump.map_index, jump.section)
        }
        None => format!("→ mapa {} salida {}", jump.map_index, jump.exit),
    }
}

/// How many jumps an event has, for its row.
pub fn jump_count(count: usize) -> String {
    match count {
        1 => "1 salto".to_owned(),
        count => format!("{count} saltos"),
    }
}

// The section editor.
pub const SECTION_EDITOR: &str = "Sección";
pub const ORIGIN_MODULE: &str = "Módulo";
pub const ORIGIN_PATCHED: &str = "Parcheado por";
pub const TAB_STRUCTURE: &str = "Estructura";
pub const TAB_LUA: &str = "Lua";
pub const NO_SECTION_CODE: &str =
    "El script no tiene esta sección: al editarla se crea en el borrador.";
pub const EMPTY_BLOCK: &str = "(vacío)";
pub const ARM_ELSEIF: &str = "O si";
pub const ARM_ELSE: &str = "Si no";
pub const DELETE_STMT: &str = "Borrar";
pub const OPEN_ROUTINE: &str = "Abrir rutina";
pub const BACK_TO_SECTION: &str = "Volver a la sección";
pub const ROUTINE: &str = "Rutina";
pub const PALETTE: &str = "Paleta";
pub const PALETTE_FILTER_HINT: &str = "Filtrar comandos";
pub const PALETTE_PLAIN: &str = "Comandos";
pub const PALETTE_ENDS: &str = "Cambian de mapa o de script (acaban la sección)";
pub const PALETTE_BLOCKS: &str = "Bloques";
pub const NEW_IF: &str = "Si…";
pub const NEW_END: &str = "Fin";
pub const INSERT_INSIDE: &str = "Insertar dentro del bloque seleccionado";
pub const INSERT_HINT: &str =
    "Lo nuevo va después de la línea seleccionada (al final si no hay ninguna)";
pub const OPERANDS: &str = "Operandos";
pub const NOTHING_TO_EDIT: &str = "Esta línea no tiene campos: cámbiala en la pestaña Lua.";
pub const SELECT_A_LINE: &str = "Elige una línea para cambiar sus campos.";
pub const TEXT_KEY: &str = "Clave";
pub const TEXT_LINE: &str = "Línea";
pub const NEW_LINE: &str = "Línea nueva";
pub const NEW_LINE_HINT: &str = "Da a este comando una clave propia con el texto de ahora, para cambiarlo sin tocar otros sitios";
pub const TEXT_LINE_HINT: &str =
    "Se aplica al salir del campo; cambia la línea en todos los sitios que usan la clave";
pub const ENTITY_PLAYER: &str = "jugador";
pub const ENTITY_PARTNER: &str = "compañero";
pub const ENTITY_NPC: &str = "NPC";
pub const SECTION_NONE: &str = "sin sección";
pub const CONDITION: &str = "Condición";
pub const QUICK_TRIGGER_ON: &str = "disparador encendido";
pub const QUICK_TRIGGER_OFF: &str = "disparador apagado";
pub const QUICK_VAR: &str = "variable";
pub const QUICK_OTHER: &str = "otra (escríbela en Lua)";
pub const APPLY: &str = "Aplicar";
pub const EXPR_HINT: &str = "Lua; Intro para aplicar";
pub const LUA_OK: &str = "Lua correcto";
pub const LUA_APPLY_HINT: &str = "Cambia el código de la sección por este (un paso de deshacer)";
pub const REVERT_SECTION: &str = "Revertir sección";
pub const REVERT_SECTION_HINT: &str =
    "Quita la sección del borrador del paquete destino: corre como la tienen los paquetes";
pub const CLOSE: &str = "Cerrar";
pub const SECTION_NO_TARGET: &str =
    "Sin paquete destino: los cambios se ven, pero para guardarlos elige uno en Archivo → Destino.";

/// The origin line of the section editor.
pub fn origin(patched_by: Option<&str>) -> String {
    match patched_by {
        Some(patch) => format!("{ORIGIN_PATCHED} {patch}"),
        None => ORIGIN_MODULE.to_owned(),
    }
}

/// A Lua error said for the editor: `línea 3: …`.
pub fn lua_error(error: &dwe_story::Error) -> String {
    match error {
        dwe_story::Error::Syntax { line, message } => format!("línea {line}: {message}"),
        other => other.to_string(),
    }
}

/// The label of an arm of an `if` (the first is the statement's own line).
pub fn arm_label(arm: usize) -> &'static str {
    if arm == 0 { CONDITION } else { ARM_ELSEIF }
}

// Trigger cells and their sections.
pub const PICK_SECTION: &str = "Sección…";
pub const SECTION_EXISTS: &str = "existe";
pub const SECTION_MISSING: &str = "no existe";
pub const NO_MAP_SCRIPT: &str = "el mapa no tiene script";
pub const OPEN_SECTION: &str = "Abrir sección";
pub const HOVER_EVENT: &str = "Evento";
pub const LAST_EVENT_HINT: &str = "El último evento bajo el cursor";

/// What a trigger value runs: `Pisar → sección 0x33 (script 12)`.
pub fn trigger_section(confirm: bool, section: u16, script: Option<u16>) -> String {
    let kind = if confirm { EVENT_CONFIRM } else { EVENT_STEP };
    match script {
        Some(script) => format!("{kind} → sección {section:#04x} (script {script})"),
        None => format!("{kind} → sección {section:#04x} ({NO_MAP_SCRIPT})"),
    }
}

// "Probar evento".
pub const TEST_TITLE: &str = "Probar evento";
pub const TEST_WRITES: &str = "Se escribe en la historia del juego";
pub const TEST_NO_WRITES: &str = "Nada que escribir";
pub const TEST_UNSAVED: &str =
    "Hay cambios sin guardar: el juego corre lo guardado (Ctrl+S antes de probar).";
pub const TEST_TO_SELECTED: &str = "Para llegar también a la línea elegida en el editor";
pub const TEST_OTHERS: &str = "Condiciones que no se pueden poner (cúmplelas a mano)";
pub const TEST_PLACE: &str = "El jugador va a";
pub const TEST_NO_PLACE: &str = "El evento corre al entrar: solo se recarga el mapa";
pub const TEST_PRESS: &str = "Después se pulsa confirmar";
pub const TEST_RUN: &str = "Probar";
pub const TEST_CANCEL: &str = "Cancelar";
pub const TEST_RUNNING: &str = "Probando el evento…";
pub const TEST_BUSY: &str = "El juego está corriendo un script de historia y no deja escribir en ella: \
     espera a que acabe (o pasa sus diálogos) y vuelve a probar.";
pub const TEST_NO_RELOAD: &str = "El juego no volvió a cargar el mapa a tiempo: prueba otra vez.";
pub const TEST_DIFF: &str = "Lo que cambió la prueba";
pub const TEST_DIFF_NONE: &str = "La historia no cambió";
pub const TEST_SKIPPED: &str = "No se puede escribir (fuera de 0 a 255)";

/// A story write of the test, said: `disparador 12 door_open: encendido`.
pub fn need_trigger(name: &str, on: bool) -> String {
    format!("disparador {name}: {}", on_off(on))
}

pub fn need_var(name: &str, value: i64) -> String {
    format!("variable {name} = {value}")
}

pub fn on_off(on: bool) -> &'static str {
    if on { "encendido" } else { "apagado" }
}

/// A trigger that changed: `disparador 12 door_open: apagado → encendido`.
pub fn trigger_change(name: &str, on: bool) -> String {
    format!("disparador {name}: {} → {}", on_off(!on), on_off(on))
}

pub fn var_change(name: &str, before: i64, after: i64) -> String {
    format!("variable {name}: {before} → {after}")
}

// NPC slots, doors and the toilet.
pub const ADD_NPC: &str = "Añadir NPC";
pub const ADD_NPC_HERE: &str = "Añadir un NPC aquí";
pub const REMOVE_NPC: &str = "Quitar el último NPC";
pub const REMOVE_NPC_HINT: &str =
    "Solo se puede quitar el último: la historia nombra los NPC por su número";
pub const DOOR: &str = "Puerta";
pub const DOOR_MODEL: &str = "Modelo";
pub const DOOR_NO_MODEL: &str = "sin modelo";
pub const ROTATION: &str = "Giro";
pub const ADD_DOOR: &str = "Añadir puerta";
pub const NO_FREE_DOOR: &str = "Todas las puertas del mapa tienen modelo";
pub const TOILET: &str = "Baño";
pub const TOILET_FIRST: &str = "Punto 1 (x, z)";
pub const TOILET_SECOND: &str = "Punto 2 (x, z)";
pub const TOILET_HINT: &str = "El compañero corre al punto 1 y después al 2";
pub const ADD_TOILET: &str = "Añadir baño";
pub const REMOVE_TOILET: &str = "Quitar baño";
pub const ADD_THINGS: &str = "Añadir al mapa";

// The file menu and the settings.
pub const REVERT: &str = "Revertir";
pub const REVERT_QUESTION: &str = "¿Descartar los cambios sin guardar de este mapa?";
pub const SETTINGS: &str = "Ajustes…";
pub const SETTINGS_TITLE: &str = "Ajustes";
pub const DWX_PATH: &str = "Programa dwx";
pub const DWX_PATH_HINT: &str = "/ruta/a/dwx";
pub const DWX_HELP: &str = "Con él, un guardado que cambia saltos de mapa de la historia vuelve a sacar \
     field/script_warps solo. Sin él, el editor dice el comando que hay que ejecutar:";
pub const DWX_CLEAR: &str = "Quitar";
pub const PACK_PLACEHOLDER: &str = "<paquete>";
pub const STORY_PROBLEMS: &str = "Problemas de la historia al guardar";
