//! Every text the editor shows, in English and Spanish. The language is chosen once at startup
//! (`--lang en|es`, else the system's language, else English) and `set_language` changes it; it
//! is shared with `dwe-story`, whose descriptions and labels the editor shows.
//!
//! A fixed text is a function named after it that returns the text in the current language; the
//! `texts!` blocks list it with both languages. A text with numbers or names in it is a function
//! that builds it in the current language.

// The language is the one of dwe-story, so that its descriptions and labels follow it too.
pub use dwe_story::language::{Language, language, set_language};

fn pick(english: &'static str, spanish: &'static str) -> &'static str {
    match language() {
        Language::English => english,
        Language::Spanish => spanish,
    }
}

/// One of two expressions, by language; only the chosen one runs.
macro_rules! say {
    ($english:expr, $spanish:expr) => {
        match language() {
            Language::English => $english,
            Language::Spanish => $spanish,
        }
    };
}

/// Fixed texts: `name: "English", "Spanish";`.
macro_rules! texts {
    ($($name:ident: $english:literal, $spanish:literal;)*) => {
        $(
            pub fn $name() -> &'static str {
                pick($english, $spanish)
            }
        )*
    };
}

texts! {
    app_title: "dwe mapper", "dwe mapper";

    menu_file: "File", "Archivo";
    open_content: "Open content folder…", "Abrir carpeta de contenido…";
    no_content:
        "Open a content folder (the base package and the others) with File or --content.",
        "Abre una carpeta de contenido (el paquete base y los demás) con Archivo o --content.";
    no_map: "Pick a map from the list on the left.", "Elige un mapa en la lista de la izquierda.";
    quit: "Quit", "Salir";

    maps: "Maps", "Mapas";
    filter_hint: "Filter by id or name", "Filtrar por id o nombre";
    group_by: "Group by", "Agrupar por";
    group_by_name: "Area name", "Nombre de zona";
    group_by_prefix: "Id prefix", "Prefijo del id";
    unnamed_group: "(unnamed)", "(sin nombre)";

    layers: "Layers", "Capas";
    layer_walls: "Walls", "Paredes";
    layer_triggers: "Triggers", "Disparadores";
    layer_exits: "Exits", "Salidas";
    layer_npcs: "NPCs", "NPC";
    layer_waypoints: "Waypoints", "Puntos de ruta";
    layer_doors: "Doors", "Puertas";
    layer_toilet: "Toilet", "Baño";
    layer_grid: "Grid", "Rejilla";
    layer_live: "Game", "Juego";

    time_of_day: "Background", "Fondo";
    variant_day: "Day", "Día";
    variant_dusk: "Dusk", "Atardecer";
    variant_night: "Night", "Noche";

    fit_view: "Fit", "Encajar";
    back: "Back", "Atrás";

    map_info: "Map", "Mapa";
    exits: "Exits", "Salidas";
    npcs: "NPCs", "NPC";
    hover: "Cursor", "Cursor";
    cell: "Cell", "Celda";
    world: "World", "Mundo";
    original: "Original", "Original";
    value: "Value", "Valor";
    go: "Go", "Ir";
    nowhere: "no destination", "sin destino";
    conflict:
        "conflicting destination (the game disables it)",
        "destino en conflicto (el juego la desactiva)";
    unknown_id:
        "unknown id; it follows the original's index",
        "id desconocido; va por el índice del original";
    no_trigger_cells: "no trigger cells", "sin celdas de disparo";
    no_background:
        "This map has no background in the packages.",
        "Este mapa no tiene fondo en los paquetes.";
    patched_by: "patched by", "parcheado por";
    exit_patched_by: "patch from", "parche de";

    kind_free: "free", "libre";
    kind_blocked: "wall", "pared";
    band_step: "on step", "al pisar";
    band_confirm: "on confirm", "al confirmar";
    band_exit: "exit", "salida";
    band_toilet: "toilet", "baño";
    band_other: "unused", "sin uso";

    live: "Live", "En vivo";
    live_endpoint_hint: "unix:/path/to/socket or tcp:PORT", "unix:/ruta/al/socket o tcp:PUERTO";
    live_connect: "Connect", "Conectar";
    live_disconnect: "Disconnect", "Desconectar";
    live_follow: "Go to the game's map", "Ir al mapa del juego";
    live_reload: "Reload the map in the game", "Recargar el mapa en el juego";
    live_overlay: "The game's debug layer", "Capa de depuración del juego";
    live_story: "Read the story", "Leer la historia";
    live_paused: "paused", "en pausa";
    live_reloaded: "Reloaded", "Recargado";
    story: "Story", "Historia";
    story_triggers: "Active triggers", "Disparadores activos";
    story_variables: "Variables other than 0", "Variables distintas de 0";
    live_off: "Not connected", "Sin conexión";
    live_on: "Connected", "Conectado";
    live_connecting: "Connecting…", "Conectando…";
    live_reconnecting: "Reconnecting", "Reconectando";
    live_game: "Game", "Juego";
    live_follow_mode: "Follow the game", "Seguir al juego";
    live_follow_hint:
        "When the game changes map, the editor opens that map (it asks first if there are unsaved \
         changes)",
        "Cuando el juego cambia de mapa, el editor abre ese mapa (pregunta antes si hay cambios \
         sin guardar)";
    live_preview: "Live preview", "Vista previa en vivo";
    live_preview_hint:
        "Shows this map's unsaved changes in the game. In memory only: nothing is written to \
         disk, and it is removed on save, on disconnect or on close",
        "Enseña en el juego los cambios sin guardar de este mapa. Solo en memoria: nada se \
         escribe en disco y se quita al guardar, al desconectar o al cerrar";
    live_preview_shown: "preview", "vista previa";
    live_preview_collision:
        "Preview: collision changed in place",
        "Vista previa: colisión cambiada en el sitio";
    live_preview_reentered: "Preview: map entered again", "Vista previa: mapa vuelto a entrar";
    live_preview_cleared: "Preview removed", "Vista previa quitada";
    live_pause: "Pause", "Pausa";
    live_resume: "Resume", "Seguir";
    live_step: "Step", "Paso";
    live_step_hint:
        "Advances one tick (1/60 s) while the game is paused",
        "Avanza un tick (1/60 s) con el juego en pausa";
    live_step_second: "+1 s", "+1 s";
    live_busy: "story running", "historia en curso";
    live_place_here: "Move the player here", "Llevar al jugador aquí";
    live_test_exit: "Test this exit", "Probar esta salida";
    live_test_exit_hint:
        "Puts the player on the exit's first cell and lets the game run so that it changes map",
        "Pone al jugador en la primera casilla de la salida y deja correr el juego para que \
         cambie de mapa";
    live_to_arrival: "Move the player to the arrival point", "Llevar al jugador a la llegada";
    live_placed: "Player placed", "Jugador colocado";
    live_player: "player", "jugador";
    live_partner: "partner", "compañero";
    live_npc: "NPC", "NPC";
    live_not_shown: "The game is on another map", "El juego está en otro mapa";
    live_log: "Game log", "Registro del juego";
    live_log_empty: "No warnings or errors", "Sin avisos ni errores";
    live_problems: "The game answered with errors", "El juego respondió con errores";
    clear: "Clear", "Limpiar";
    story_follow: "Refresh automatically", "Actualizar sola";
    story_filter_hint: "Filter by number or name", "Filtrar por número o nombre";
    story_script: "Map script", "Script del mapa";
    story_sections: "Running sections", "Secciones en curso";
    story_stage: "Stage", "Etapa";
    story_none: "none", "ninguna";
    story_trigger: "Trigger", "Disparador";
    story_variable: "Variable", "Variable";
    story_set_on: "Turn on", "Activar";
    story_set_off: "Turn off", "Quitar";
    story_set_value: "Set", "Poner";
    story_set_hint:
        "For testing events: writes to the game's story state (not while a script is running)",
        "Para probar eventos: escribe en el estado de la historia del juego (no mientras corre un \
         script)";

    skipped_mod: "Mod left out", "Mod que se queda fuera";
    archives_not_read:
        ".pak archives the editor does not read (folders only)",
        "Archivos .pak que el editor no lee (solo carpetas)";
    default_bands: "the original's bands are used", "se usan las bandas del original";

    menu_edit: "Edit", "Editar";
    reload_content: "Reload content", "Recargar contenido";
    undo: "Undo", "Deshacer";
    redo: "Redo", "Rehacer";
    save_patch: "Save (patch)", "Guardar (parche)";
    save_whole: "Save whole map", "Guardar mapa completo";
    shortcuts: "Keyboard shortcuts", "Atajos de teclado";

    tools: "Tools", "Herramientas";
    tool_select: "Select", "Seleccionar";
    tool_brush: "Brush", "Pincel";
    tool_rectangle: "Rectangle", "Rectángulo";
    tool_fill: "Fill", "Relleno";
    tool_eyedropper: "Eyedropper", "Cuentagotas";
    brush_size: "Size", "Tamaño";
    paint_value: "Value", "Valor";
    value_wall: "Wall", "Pared";
    value_free: "Free", "Libre";
    value_step: "On step", "Al pisar";
    value_confirm: "On confirm", "Al confirmar";
    value_exit: "Exit", "Salida";
    value_toilet: "Toilet", "Baño";
    value_raw: "Raw", "Crudo";

    inspector: "Selection", "Selección";
    nothing_selected:
        "Nothing selected: pick an exit, an NPC, a waypoint, a door or the toilet with Select.",
        "Nada seleccionado: elige una salida, un NPC, un punto de ruta, una puerta o el baño con \
         Seleccionar.";
    exit: "Exit", "Salida";
    target_map: "Destination map", "Mapa de destino";
    target_map_none: "(none)", "(ninguno)";
    target_index: "Original's index", "Índice del original";
    target_exit: "Destination exit", "Salida de destino";
    arrival: "Arrival (x, y, z)", "Llegada (x, y, z)";
    facing: "Facing", "Orientación";
    paint_exit_cells: "Paint its cells", "Pintar sus celdas";
    erase_cells: "Erase cells (free)", "Borrar celdas (libre)";
    open_target: "Open the destination", "Abrir el destino";
    npc: "NPC", "NPC";
    position: "Position (x, y, z)", "Posición (x, y, z)";
    npc_fields: "Fields", "Campos";
    path: "Route", "Ruta";
    part_wait: "wait", "esperar";
    part_walk: "walk", "andar";
    part_run: "run", "correr";
    add_point: "Add point", "Añadir punto";
    remove_point: "Remove", "Quitar";
    up: "Up", "Subir";
    down: "Down", "Bajar";
    frames_unit: "frames", "fotogramas";
    read_only: "Read only", "Solo lectura";
    camera: "Camera", "Cámara";
    lights: "Lights", "Luces";
    objects: "Objects", "Objetos";

    save: "Save", "Guardar";
    target: "Target", "Destino";
    no_target:
        "Pick the target package before saving (File → Target).",
        "Elige el paquete destino antes de guardar (Archivo → Destino).";
    target_mod: "mod", "mod";
    target_team: "team", "equipo";
    new_pack: "New mod package…", "Nuevo paquete de mod…";
    no_targets:
        "No package in the content can be written to: create a new one.",
        "Ningún paquete del contenido se puede escribir: crea uno nuevo.";
    pick_folder: "Pick…", "Elegir…";
    new_pack_id: "New package id", "Id del paquete nuevo";
    new_pack_folder: "Mods folder", "Carpeta de mods";
    new_pack_create: "Create", "Crear";
    bad_pack_id:
        "Invalid package id (1 to 64 of a-z, 0-9 and _)",
        "Id de paquete no válido (1 a 64 de a-z, 0-9 y _)";
    pack_exists: "Already exists", "Ya existe";
    pack_not_mounted:
        "The new package was not mounted (see the mods that are left out)",
        "El paquete nuevo no se montó (mira los mods que se quedan fuera)";
    save_refused: "Not saved: see the problems.", "No se guardó: mira los problemas.";
    save_failed: "Could not write", "No se pudo escribir";
    nothing_to_save: "nothing to write", "nada que escribir";
    saved_in: "Saved in", "Guardado en";
    save_problems: "Problems when saving", "Problemas al guardar";
    reload_on_save: "Reload on save", "Recargar al guardar";
    dirty: "unsaved", "sin guardar";
    unsaved_title: "Unsaved changes", "Cambios sin guardar";
    unsaved_question: "The map has unsaved changes.", "El mapa tiene cambios sin guardar.";
    unsaved_save: "Save", "Guardar";
    unsaved_discard: "Discard", "Descartar";
    unsaved_cancel: "Cancel", "Cancelar";
}

/// The description of a pack the editor creates, by language code: the manifest keeps every
/// language, so the pack reads right whatever language the game or a tool is in.
pub const NEW_PACK_DESCRIPTION: [(&str, &str); 2] = [
    ("en", "Map changes made with dwe-mapper"),
    ("es", "Cambios de mapas hechos con dwe-mapper"),
];

/// The keyboard shortcuts, as the help shows them: the keys and what they do.
pub fn shortcut_list() -> Vec<(&'static str, &'static str)> {
    vec![
        ("Ctrl+S", pick("Save (patch)", "Guardar (parche)")),
        ("Ctrl+Q", pick("Quit", "Salir")),
        (
            "M",
            pick(
                "Switch between the map and the world",
                "Cambiar entre el mapa y el mundo",
            ),
        ),
        ("+ / -", pick("Zoom in / out", "Acercar / alejar")),
        (
            pick("Home", "Inicio"),
            pick("Fit the map", "Encuadrar el mapa"),
        ),
        (
            "N",
            pick(
                "Next time of day (background)",
                "Siguiente hora del día (fondo)",
            ),
        ),
        (
            "Ctrl+G",
            pick(
                "Go where the selected exit leads",
                "Ir adonde lleva la salida seleccionada",
            ),
        ),
        (
            "Ctrl+D",
            pick(
                "Add an NPC under the cursor",
                "Añadir un NPC bajo el cursor",
            ),
        ),
        (
            pick("World: Shift+drag", "Mundo: Mayús+arrastrar"),
            pick(
                "From one map to another: connect their exits",
                "De un mapa a otro: conectar sus salidas",
            ),
        ),
        ("Ctrl+Z", pick("Undo", "Deshacer")),
        (
            pick("Ctrl+Y / Ctrl+Shift+Z", "Ctrl+Y / Ctrl+Mayús+Z"),
            pick("Redo", "Rehacer"),
        ),
        (
            "Ctrl+F",
            pick("Search for a map in the list", "Buscar un mapa en la lista"),
        ),
        ("V", pick("Select", "Seleccionar")),
        ("B", pick("Brush", "Pincel")),
        ("R", pick("Rectangle", "Rectángulo")),
        ("F", pick("Fill", "Relleno")),
        (
            "I",
            pick(
                "Eyedropper (or Alt+click with a paint tool)",
                "Cuentagotas (o Alt+clic con una herramienta de pintar)",
            ),
        ),
        (
            "[ / ]",
            pick("Smaller / larger brush", "Pincel más pequeño / más grande"),
        ),
        ("X", pick("Value: wall / free", "Valor: pared / libre")),
        (
            "Q / E",
            pick(
                "Rotate the selection 45° to one side / the other",
                "Girar la selección 45° a un lado / al otro",
            ),
        ),
        (
            pick("Del", "Supr"),
            pick(
                "Remove the selected waypoint",
                "Quitar el punto de ruta seleccionado",
            ),
        ),
        (
            "W",
            pick(
                "Add a waypoint to the selected NPC under the cursor",
                "Añadir un punto de ruta al NPC seleccionado bajo el cursor",
            ),
        ),
        (
            "C",
            pick(
                "Centre the view on the selection",
                "Centrar la vista en la selección",
            ),
        ),
        ("Esc", pick("Deselect", "Deseleccionar")),
        ("1 … 9, 0", layer_keys_hint()),
        (
            "Alt+0 … Alt+9",
            pick(
                "Select the exit with that number and centre it",
                "Seleccionar la salida de ese número y centrarla",
            ),
        ),
        (
            pick("PgUp / PgDn", "RePág / AvPág"),
            pick(
                "Previous / next map in the list",
                "Mapa anterior / siguiente de la lista",
            ),
        ),
        (
            pick("Ctrl+click", "Ctrl+clic"),
            pick("Go where the exit leads", "Ir adonde lleva la salida"),
        ),
        (
            pick("Shift+drag", "Mayús+arrastrar"),
            pick(
                "Face the selection towards the cursor",
                "Orientar la selección hacia el cursor",
            ),
        ),
        (
            pick("Click on an event", "Clic en un evento"),
            pick(
                "With the Events layer and Select: opens its section",
                "Con la capa Eventos y Seleccionar: abre su sección",
            ),
        ),
    ]
}

texts! {
    // What the number keys do, as the help and the layer bar say it.
    layer_keys_hint:
        "Show or hide a layer: 1 walls, 2 triggers, 3 exits, 4 NPCs, 5 waypoints, 6 doors, 7 \
         toilet, 8 grid, 9 game, 0 events",
        "Mostrar u ocultar una capa: 1 paredes, 2 disparadores, 3 salidas, 4 NPC, 5 puntos de \
         ruta, 6 puertas, 7 baño, 8 rejilla, 9 juego, 0 eventos";
}

/// The name of a tool.
pub fn tool_name(tool: crate::tools::Tool) -> &'static str {
    match tool {
        crate::tools::Tool::Select => tool_select(),
        crate::tools::Tool::Brush => tool_brush(),
        crate::tools::Tool::Rectangle => tool_rectangle(),
        crate::tools::Tool::Fill => tool_fill(),
        crate::tools::Tool::Eyedropper => tool_eyedropper(),
    }
}

/// What a save found, said for the problems panel.
pub fn problem(problem: &dwe_map::save::Problem) -> String {
    use dwe_map::save::Problem;
    match problem {
        Problem::TargetBelowData { data_pack } => say!(
            format!(
                "The target package is below '{data_pack}', which brings the whole map: its patch would not apply."
            ),
            format!(
                "El paquete destino está debajo de «{data_pack}», que trae el mapa entero: su parche no se aplicaría."
            )
        ),
        Problem::NoData => say!(
            "No package brings this map.".to_owned(),
            "Ningún paquete trae este mapa.".to_owned()
        ),
        Problem::DoesNotLoad(message) => say!(
            format!("What was written does not load: {message}"),
            format!("Lo escrito no carga: {message}")
        ),
        Problem::ExitConflict { exit } => say!(
            format!(
                "Exit {exit}: the id and the original's index name different maps (the game disables it)."
            ),
            format!(
                "Salida {exit}: el id y el índice del original nombran mapas distintos (el juego la desactiva)."
            )
        ),
        Problem::UnknownMap { exit, id } => say!(
            format!("Exit {exit}: '{id}' is not a map."),
            format!("Salida {exit}: «{id}» no es ningún mapa.")
        ),
        Problem::DeadExit { exit } => say!(
            format!("Exit {exit}: it has trigger cells but leads nowhere (dead warp)."),
            format!(
                "Salida {exit}: tiene celdas de disparo pero no lleva a ningún sitio (warp muerto)."
            )
        ),
        Problem::BadTargetExit { exit, target_exit } => say!(
            format!("Exit {exit}: the destination exit {target_exit} does not exist (0 to 9)."),
            format!("Salida {exit}: la salida de destino {target_exit} no existe (0 a 9).")
        ),
        Problem::BadPath { slot } => say!(
            format!(
                "NPC {slot}: the route does not fit in the record (8 parts, 8 points at most)."
            ),
            format!("NPC {slot}: la ruta no cabe en el registro (8 partes, como mucho 8 puntos).")
        ),
        Problem::NpcSlotsInPatch => say!(
            "NPCs were added or removed: a patch cannot say that, save the whole map.".to_owned(),
            "Hay NPC añadidos o quitados: un parche no puede decirlo, guarda el mapa entero."
                .to_owned()
        ),
        Problem::Overridden { packs } => say!(
            format!(
                "Warning: a patch above ({}) changes the same thing; the game will see theirs.",
                packs.join(", ")
            ),
            format!(
                "Aviso: un parche de encima ({}) cambia lo mismo; el juego verá el suyo.",
                packs.join(", ")
            )
        ),
    }
}

/// A short word for a trigger value's band.
pub fn band_name(band: dwe_map::TriggerBand) -> String {
    match band {
        dwe_map::TriggerBand::Step => band_step().to_owned(),
        dwe_map::TriggerBand::Confirm => band_confirm().to_owned(),
        dwe_map::TriggerBand::Exit(exit) => format!("{} {exit}", band_exit()),
        dwe_map::TriggerBand::Toilet => band_toilet().to_owned(),
        dwe_map::TriggerBand::Other => band_other().to_owned(),
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
        "day" => variant_day().to_owned(),
        "dusk" => variant_dusk().to_owned(),
        "night" => variant_night().to_owned(),
        other => other.to_owned(),
    }
}

/// The game's mode as `status` and `live` name it.
pub fn game_mode(mode: &str) -> &str {
    match mode {
        "field" => pick("field", "campo"),
        "battle" => pick("battle", "batalla"),
        "movie" => pick("movie", "vídeo"),
        "title" => pick("title", "título"),
        "interview" => pick("initial questions", "preguntas iniciales"),
        other => other,
    }
}

// The story.
texts! {
    layer_events: "Events", "Eventos";
    not_saved: "unsaved", "sin guardar";
    too_many_npcs:
        "The map already has 8 NPCs, the most the game allows.",
        "El mapa ya tiene 8 NPC, los que el juego admite.";
    no_npc_template:
        "There is no NPC to copy in the content.",
        "No hay ningún NPC que copiar en el contenido.";
    no_entry_section:
        "The map has no entry section (script 0): the new NPC will not appear until a section \
         makes it appear.",
        "El mapa no tiene sección de entrada (script 0): el NPC nuevo no aparecerá hasta que una \
         sección lo haga aparecer.";
    no_code:
        "The open section or routine has no code.",
        "La sección o la rutina abierta no tiene código.";
    npcs_save_whole:
        "NPCs were added or removed: the whole map is saved (patches name the NPCs by their \
         position).",
        "Hay NPC añadidos o quitados: el mapa se guarda entero (los parches nombran los NPC por \
         su posición).";
    warps_by_hand:
        "The save changes map jumps of the story; extract field/script_warps again with",
        "El guardado cambia saltos de mapa de la historia; vuelve a sacar field/script_warps con";
    warps_made: "field/script_warps extracted again", "field/script_warps sacado de nuevo";
    warps_failed: "dwx script-warps failed", "dwx script-warps falló";

// The events layer and panel.
    events: "Events", "Eventos";
    event_map_entry: "On map entry", "Al entrar al mapa";
    event_map_loaded: "Map loaded", "Mapa cargado";
    event_step: "Step", "Pisar";
    event_confirm: "Button", "Botón";
    event_talk: "Talk NPC", "Hablar NPC";
    events_filter_hint: "Filter by type or section", "Filtrar por tipo o sección";
    events_none: "This map has no events.", "Este mapa no tiene eventos.";
    event_missing:
        "The script has no such section: nothing happens (it is created when you edit it)",
        "El script no tiene esta sección: no pasa nada (se crea al editarla)";
    event_patched: "patched by", "parcheado por";
    event_open_hint:
        "Click: open the section and centre the view",
        "Clic: abrir la sección y centrar la vista";
    jump_hint:
        "Click: open the destination map at that exit",
        "Clic: abrir el mapa de destino en esa salida";
    jump_always: "Always", "Siempre";
    jump_when: "When", "Cuando";
    test_event: "Test", "Probar";
    test_event_hint:
        "Test the event in the connected game",
        "Probar el evento en el juego conectado";
    test_event_offline:
        "Connect a game (Live) to test events",
        "Conecta un juego (En vivo) para probar eventos";
}

/// The kind of an event, in words.
pub fn event_kind(kind: dwe_story::EventKind) -> String {
    use dwe_story::EventKind;
    match kind {
        EventKind::MapEntry => event_map_entry().to_owned(),
        EventKind::MapLoaded => event_map_loaded().to_owned(),
        EventKind::Step => event_step().to_owned(),
        EventKind::Confirm => event_confirm().to_owned(),
        EventKind::Talk { slot } => format!("{} {slot}", event_talk()),
    }
}

/// A section of a script: `script 12 · section 0x33`.
pub fn section_name(script: u16, section: u16) -> String {
    say!(
        format!("script {script} · section {section:#04x}"),
        format!("script {script} · sección {section:#04x}")
    )
}

/// Where a jump of the story goes: `→ area_b exit 2`, or another script's section.
pub fn jump_label(jump: &dwe_story::events::Jump) -> String {
    match &jump.to_map {
        Some(map) => say!(
            format!("→ {map} exit {}", jump.exit),
            format!("→ {map} salida {}", jump.exit)
        ),
        None if jump.file_jump => say!(
            format!("→ script {} section {:#04x}", jump.map_index, jump.section),
            format!("→ script {} sección {:#04x}", jump.map_index, jump.section)
        ),
        None => say!(
            format!("→ map {} exit {}", jump.map_index, jump.exit),
            format!("→ mapa {} salida {}", jump.map_index, jump.exit)
        ),
    }
}

/// The label of map changes whose arrows end at one point: the label with how many there are
/// when they are alike, else the first and how many more.
pub fn jump_pile(labels: &[&str]) -> String {
    match labels {
        [] => String::new(),
        [only] => (*only).to_owned(),
        [first, rest @ ..] if rest.iter().all(|label| label == first) => {
            format!("{first} ×{}", labels.len())
        }
        [first, rest @ ..] => say!(
            format!("{first} (+{} more)", rest.len()),
            format!("{first} (+{} más)", rest.len())
        ),
    }
}

/// How many jumps an event has, for its row.
pub fn jump_count(count: usize) -> String {
    match count {
        1 => pick("1 jump", "1 salto").to_owned(),
        count => say!(format!("{count} jumps"), format!("{count} saltos")),
    }
}

// The section editor.
texts! {
    section_editor: "Section", "Sección";
    origin_module: "Module", "Módulo";
    origin_patched: "Patched by", "Parcheado por";
    tab_structure: "Structure", "Estructura";
    tab_lua: "Lua", "Lua";
    no_section_code:
        "The script has no such section: editing it creates it in the draft.",
        "El script no tiene esta sección: al editarla se crea en el borrador.";
    empty_block: "(empty)", "(vacío)";
    arm_elseif: "Or if", "O si";
    arm_else: "Otherwise", "Si no";
    delete_stmt: "Delete", "Borrar";
    open_routine: "Open routine", "Abrir rutina";
    back_to_section: "Back to the section", "Volver a la sección";
    routine: "Routine", "Rutina";
    palette: "Palette", "Paleta";
    palette_filter_hint: "Filter commands", "Filtrar comandos";
    palette_plain: "Commands", "Comandos";
    palette_ends:
        "Change map or script (they end the section)",
        "Cambian de mapa o de script (acaban la sección)";
    palette_blocks: "Blocks", "Bloques";
    new_if: "If…", "Si…";
    new_end: "End", "Fin";
    insert_inside: "Insert inside the selected block", "Insertar dentro del bloque seleccionado";
    insert_hint:
        "The new line goes after the selected line (at the end if there is none)",
        "Lo nuevo va después de la línea seleccionada (al final si no hay ninguna)";
    operands: "Operands", "Operandos";
    nothing_to_edit:
        "This line has no fields: change it in the Lua tab.",
        "Esta línea no tiene campos: cámbiala en la pestaña Lua.";
    select_a_line: "Pick a line to change its fields.", "Elige una línea para cambiar sus campos.";
    text_key: "Key", "Clave";
    text_line: "Line", "Línea";
    new_line: "New line", "Línea nueva";
    new_line_hint:
        "Gives this command a key of its own with the current text, to change it without touching \
         other places",
        "Da a este comando una clave propia con el texto de ahora, para cambiarlo sin tocar otros \
         sitios";
    text_line_hint:
        "Applies when you leave the field; it changes the line in every place that uses the key",
        "Se aplica al salir del campo; cambia la línea en todos los sitios que usan la clave";
    entity_player: "player", "jugador";
    entity_partner: "partner", "compañero";
    entity_npc: "NPC", "NPC";
    section_none: "no section", "sin sección";
    condition: "Condition", "Condición";
    quick_trigger_on: "trigger on", "disparador encendido";
    quick_trigger_off: "trigger off", "disparador apagado";
    quick_var: "variable", "variable";
    quick_other: "other (write it in Lua)", "otra (escríbela en Lua)";
    apply: "Apply", "Aplicar";
    expr_hint: "Lua; Enter to apply", "Lua; Intro para aplicar";
    lua_ok: "Lua is valid", "Lua correcto";
    lua_apply_hint:
        "Replaces the section's code with this one (one undo step)",
        "Cambia el código de la sección por este (un paso de deshacer)";
    revert_section: "Revert section", "Revertir sección";
    revert_section_hint:
        "Removes the section from the target package's draft: it runs as the packages have it",
        "Quita la sección del borrador del paquete destino: corre como la tienen los paquetes";
    close: "Close", "Cerrar";
    section_no_target:
        "No target package: the changes show, but to save them pick one in File → Target.",
        "Sin paquete destino: los cambios se ven, pero para guardarlos elige uno en Archivo → \
         Destino.";
}

/// The origin line of the section editor.
pub fn origin(patched_by: Option<&str>) -> String {
    match patched_by {
        Some(patch) => format!("{} {patch}", origin_patched()),
        None => origin_module().to_owned(),
    }
}

/// A Lua error said for the editor: `line 3: …`.
pub fn lua_error(error: &dwe_story::Error) -> String {
    match error {
        dwe_story::Error::Syntax { line, message } => {
            say!(
                format!("line {line}: {message}"),
                format!("línea {line}: {message}")
            )
        }
        other => other.to_string(),
    }
}

/// The label of an arm of an `if` (the first is the statement's own line).
pub fn arm_label(arm: usize) -> &'static str {
    if arm == 0 { condition() } else { arm_elseif() }
}

// Trigger cells and their sections.
texts! {
    pick_section: "Section…", "Sección…";
    section_exists: "exists", "existe";
    section_missing: "does not exist", "no existe";
    no_map_script: "the map has no script", "el mapa no tiene script";
    open_section: "Open section", "Abrir sección";
    hover_event: "Event", "Evento";
    last_event_hint: "The last event under the cursor", "El último evento bajo el cursor";
}

/// What a trigger value runs: `Step → section 0x33 (script 12)`.
pub fn trigger_section(confirm: bool, section: u16, script: Option<u16>) -> String {
    let kind = if confirm {
        event_confirm()
    } else {
        event_step()
    };
    match script {
        Some(script) => say!(
            format!("{kind} → section {section:#04x} (script {script})"),
            format!("{kind} → sección {section:#04x} (script {script})")
        ),
        None => format!(
            "{kind} → {} {section:#04x} ({})",
            pick("section", "sección"),
            no_map_script()
        ),
    }
}

// "Probar evento".
texts! {
    test_title: "Test event", "Probar evento";
    test_writes: "Written to the game's story", "Se escribe en la historia del juego";
    test_no_writes: "Nothing to write", "Nada que escribir";
    test_unsaved:
        "There are unsaved changes: the game runs what is saved (Ctrl+S before testing).",
        "Hay cambios sin guardar: el juego corre lo guardado (Ctrl+S antes de probar).";
    test_to_selected:
        "To also reach the line picked in the editor",
        "Para llegar también a la línea elegida en el editor";
    test_others:
        "Conditions that cannot be set (meet them by hand)",
        "Condiciones que no se pueden poner (cúmplelas a mano)";
    test_place: "The player goes to", "El jugador va a";
    test_no_place:
        "The event runs on entry: only the map is reloaded",
        "El evento corre al entrar: solo se recarga el mapa";
    test_press: "Then confirm is pressed", "Después se pulsa confirmar";
    test_run: "Test", "Probar";
    test_cancel: "Cancel", "Cancelar";
    test_running: "Testing the event…", "Probando el evento…";
    test_busy:
        "The game is running a story script and does not allow writing to the story: wait for it \
         to end (or skip its dialogues) and test again.",
        "El juego está corriendo un script de historia y no deja escribir en ella: espera a que \
         acabe (o pasa sus diálogos) y vuelve a probar.";
    test_no_reload:
        "The game did not reload the map in time: try again.",
        "El juego no volvió a cargar el mapa a tiempo: prueba otra vez.";
    test_diff: "What the test changed", "Lo que cambió la prueba";
    test_diff_none: "The story did not change", "La historia no cambió";
    test_skipped: "Cannot be written (outside 0 to 255)", "No se puede escribir (fuera de 0 a 255)";
}

/// A story write of the test, said: `trigger 12 door_open: on`.
pub fn need_trigger(name: &str, on: bool) -> String {
    say!(
        format!("trigger {name}: {}", on_off(on)),
        format!("disparador {name}: {}", on_off(on))
    )
}

pub fn need_var(name: &str, value: i64) -> String {
    say!(
        format!("variable {name} = {value}"),
        format!("variable {name} = {value}")
    )
}

pub fn on_off(on: bool) -> &'static str {
    if on {
        pick("on", "encendido")
    } else {
        pick("off", "apagado")
    }
}

/// A trigger that changed: `trigger 12 door_open: off → on`.
pub fn trigger_change(name: &str, on: bool) -> String {
    say!(
        format!("trigger {name}: {} → {}", on_off(!on), on_off(on)),
        format!("disparador {name}: {} → {}", on_off(!on), on_off(on))
    )
}

pub fn var_change(name: &str, before: i64, after: i64) -> String {
    format!("variable {name}: {before} → {after}")
}

// NPC slots, doors and the toilet.
texts! {
    add_npc: "Add NPC", "Añadir NPC";
    add_npc_here: "Add an NPC here", "Añadir un NPC aquí";
    remove_npc: "Remove the last NPC", "Quitar el último NPC";
    remove_npc_hint:
        "Only the last one can be removed: the story names NPCs by their number",
        "Solo se puede quitar el último: la historia nombra los NPC por su número";
    door: "Door", "Puerta";
    door_model: "Model", "Modelo";
    door_no_model: "no model", "sin modelo";
    rotation: "Rotation", "Giro";
    add_door: "Add door", "Añadir puerta";
    no_free_door: "All the map's doors have a model", "Todas las puertas del mapa tienen modelo";
    toilet: "Toilet", "Baño";
    toilet_first: "Point 1 (x, z)", "Punto 1 (x, z)";
    toilet_second: "Point 2 (x, z)", "Punto 2 (x, z)";
    toilet_hint:
        "The partner runs to point 1 and then to point 2",
        "El compañero corre al punto 1 y después al 2";
    add_toilet: "Add toilet", "Añadir baño";
    remove_toilet: "Remove toilet", "Quitar baño";
    add_things: "Add to the map", "Añadir al mapa";

// The file menu and the settings.
    revert: "Revert", "Revertir";
    revert_question:
        "Discard this map's unsaved changes?",
        "¿Descartar los cambios sin guardar de este mapa?";
    settings: "Settings…", "Ajustes…";
    settings_title: "Settings", "Ajustes";
    dwx_path: "dwx program", "Programa dwx";
    dwx_path_hint: "/path/to/dwx", "/ruta/a/dwx";
    dwx_help:
        "With it, a save that changes map jumps of the story extracts field/script_warps again by \
         itself. Without it, the editor tells you the command to run:",
        "Con él, un guardado que cambia saltos de mapa de la historia vuelve a sacar \
         field/script_warps solo. Sin él, el editor dice el comando que hay que ejecutar:";
    dwx_clear: "Remove", "Quitar";
    pack_placeholder: "<package>", "<paquete>";
    story_problems: "Story problems when saving", "Problemas de la historia al guardar";

// The timeline of a section.
    cue_walk: "Walk here", "Andar aquí";
    cue_turn: "Turn towards here", "Girar hacia aquí";
    cue_delay: "Wait", "Esperar";
    cue_camera: "Camera here", "Cámara aquí";
    cue_camera_entity: "Camera on the character", "Cámara al personaje";
    cue_animation: "Animation", "Animación";
    cue_text: "Text", "Texto";
    cue_new_line: "…", "…";
    cue_keeps_end:
        "The section's end (return) stays last: it does not move or get passed",
        "El final de la sección (return) se queda el último: no se mueve ni se pasa por encima";
    no_section_open: "No section is open", "No hay ninguna sección abierta";
    play_from_routine:
        "\"Play from here\" starts at a line of the section, not of a routine: go back to the \
         section",
        "«Reproducir desde aquí» empieza en una línea de la sección, no de una rutina: vuelve a \
         la sección";
    play_header:
        "dwe mapper: play from here (sent to the game, never saved)",
        "dwe mapper: play from here (sent to the game, never saved)";
    timeline: "Timeline", "Línea de tiempo";
    timeline_hint:
        "Shows the section as a timeline under the map: who moves, how long each command lasts \
         and what waits for what",
        "Muestra la sección como una línea de tiempo bajo el mapa: quién se mueve, cuánto dura \
         cada orden y qué espera a qué";
    timeline_empty:
        "Open a section (Events or click an event) to see its timeline",
        "Abre una sección (Eventos o clic en un evento) para ver su línea de tiempo";
    timeline_zoom: "Zoom", "Zoom";
    timeline_add: "Add at the cursor:", "Añadir en el cursor:";
    timeline_add_hint:
        "Adds the command after the one running at the cursor, for the character of the picked \
         command (the player if it has none); \"here\" is where that character or the camera is \
         at the cursor (on the map, right click → \"here\" uses the clicked point)",
        "Añade la orden detrás de la que corre en el cursor, para el personaje de la orden \
         elegida (el jugador si no tiene); «aquí» es donde está ese personaje o la cámara en el \
         cursor (en el mapa, clic derecho → «aquí» usa el punto del clic)";
    cue_earlier: "Earlier", "Antes";
    cue_later: "Later", "Después";
    cue_move_hint:
        "Swaps the picked command with its neighbour in the same block",
        "Cambia la orden elegida de sitio con su vecina del mismo bloque";
    cue_edge_hint:
        "Drag the right edge to change the frames of the wait or the movement",
        "Arrastra el borde derecho para cambiar los cuadros de la espera o del movimiento";
    play: "Play", "Reproducir";
    play_hint:
        "Prepares the game like \"Test event\" (story, place, reload) and starts the section",
        "Prepara el juego como «Probar evento» (historia, sitio, recarga) y arranca la sección";
    play_from_here: "Play from here", "Reproducir desde aquí";
    play_from_here_hint:
        "Starts the section at the picked command: first it places everyone and the camera where \
         the model has them and applies the earlier state commands, without waits",
        "Arranca la sección en la orden elegida: antes coloca a todos y la cámara donde el modelo \
         los tiene y aplica las órdenes de estado anteriores, sin esperas";
    play_title: "Play in the game", "Reproducir en el juego";
    play_starts: "Then the section starts in the game.", "Después arranca la sección en el juego.";
    play_offline:
        "Connect a game (Live panel) to play",
        "Conecta un juego (panel En vivo) para reproducir";
    play_needs_cue:
        "Pick a command of the section (in the timeline or in the editor)",
        "Elige una orden de la sección (en la línea de tiempo o en el editor)";
    timeline_follow: "Follow the game", "Seguir al juego";
    timeline_follow_hint:
        "The cursor goes to the command the game is at (it reads the story state four times a \
         second)",
        "El cursor va a la orden por la que va el juego (lee el estado de la historia cuatro \
         veces por segundo)";
    timeline_frame: "+1 frame", "+1 cuadro";
    timeline_frame_hint:
        "With the game paused: advances one frame of the original (3 ticks)",
        "Con el juego en pausa: avanza un cuadro del original (3 ticks)";
    timeline_game_at: "The game is at", "El juego va por";
    timeline_game_idle: "The game is not running this section", "El juego no corre esta sección";
    drift_label: "Game-model drift", "Desvío juego-modelo";
    drift_hint:
        "Distance between where each character is in the game and where the model puts it at the \
         cursor (world units; 100 = one cell)",
        "Distancia entre donde está cada personaje en el juego y donde lo pone el modelo en el \
         cursor (unidades del mundo; 100 = una celda)";
    hold_runs:
        "does not block: the next command starts at the same time",
        "no bloquea: la siguiente orden empieza a la vez";
    hold_frames:
        "blocks: the section waits these frames",
        "bloquea: la sección espera estos cuadros";
    hold_player: "waits for the player", "espera al jugador";
    hold_movement: "waits for the movement to end", "espera a que acabe el movimiento";
    hold_ends: "ends the section", "termina la sección";
    cue_estimated: "estimated", "estimado";
    cue_cut:
        "cut by the next command of the same character",
        "cortado por la orden siguiente del mismo personaje";
    cue_from_routine: "from the routine", "de la rutina";
    track_player: "Player", "Jugador";
    track_partner: "Partner", "Compañero";
    track_camera: "Camera", "Cámara";
    track_text: "Text", "Texto";
    track_audio: "Audio", "Audio";
    track_wait: "Waits", "Esperas";
    track_story: "State", "Estado";

    script_warp: "script warp", "warp de script";
    check_missing_map: "leads to a map that does not exist", "lleva a un mapa que no existe";
    check_missing_exit: "leads to an exit that does not exist", "lleva a una salida que no existe";
    check_fallback:
        "its id is not any map's; the original's index leads to",
        "su id no es de ningún mapa; el índice del original lleva a";
    check_one_way: "one way only: no way back from", "solo de ida: no hay vuelta desde";
    check_unreachable:
        "cannot be reached from the starting map",
        "no se llega desde el mapa inicial";
    check_arrival_in_wall: "you arrive inside a wall", "se llega dentro de una pared";
    no_such_exit: "the map has no such exit", "el mapa no tiene esa salida";
    connected: "Connected", "Conectado";
    new_map_failed: "Could not create the map", "No se pudo crear el mapa";
    new_map_created: "Map created", "Mapa creado";
    not_own_map:
        "Only the maps the target package added can be deleted or renamed",
        "Solo se pueden borrar o renombrar los mapas que añadió el paquete destino";
    map_is_referenced:
        "Paths lead to this map; change them first",
        "Hay caminos que llevan a este mapa; cámbialos antes";
    map_deleted: "Map deleted", "Mapa borrado";
    map_renamed: "Map renamed", "Mapa renombrado";

    view_map: "Map", "Mapa";
    view_world: "World", "Mundo";
    view_hint: "M: switch between the map and the world", "M: cambiar entre el mapa y el mundo";
    world_search_hint: "Search for a map (Enter centres it)", "Buscar un mapa (Intro lo centra)";
    world_exits: "Exits", "Salidas";
    world_script_warps: "Script warps", "Warps de script";
    world_areas: "Areas", "Zonas";
    world_all: "All", "Todas";
    world_none: "None", "Ninguna";
    world_relayout: "Re-arrange", "Reordenar";
    world_relayout_hint:
        "Forgets where you put the maps and places them again (always the same for the same \
         content)",
        "Olvida dónde pusiste los mapas y los coloca otra vez (siempre igual para el mismo \
         contenido)";
    world_checks: "Checks", "Comprobaciones";
    world_errors: "Errors", "Errores";
    world_warnings: "Warnings", "Avisos";
    world_notes: "Notes", "Notas";
    world_no_checks: "Nothing to report.", "Nada que señalar.";
    world_check_hint: "Click: centre the map in the graph", "Clic: centrar el mapa en el grafo";
    world_if: "if", "si";
    world_area: "Area", "Zona";
    world_node_hint:
        "Double click: open · drag: move · Shift+drag or right button to another map: connect",
        "Doble clic: abrir · arrastrar: mover · Mayús+arrastrar o botón derecho hasta otro mapa: \
         conectar";
    world_edge_hint:
        "Click: open the exit or the section in its editor",
        "Clic: abrir la salida o la sección en su editor";
    world_connect_title: "Connect maps", "Conectar mapas";
    world_connect_from: "Exit of", "Salida de";
    world_connect_to: "Arrives at", "Llega a";
    world_connect_back:
        "And the way back (that exit leads to the first one)",
        "Y la vuelta (esa salida lleva a la primera)";
    world_connect_help:
        "The open map's exit stays as an unsaved change; any other map's exit is written right \
         away as a patch in the target package.",
        "La salida del mapa abierto queda como cambio sin guardar; la de cualquier otro mapa se \
         escribe ya como parche en el paquete destino.";
    world_connect: "Connect", "Conectar";
    menu_maps: "Maps", "Mapas";
    new_map: "New map…", "Nuevo mapa…";
    rename_map: "Rename map…", "Renombrar mapa…";
    delete_map: "Delete map…", "Borrar mapa…";
    new_map_title: "New map", "Nuevo mapa";
    new_map_name: "Id", "Id";
    new_map_name_hint: "a-z, 0-9 and _", "a-z, 0-9 y _";
    new_map_title_field: "Name", "Nombre";
    new_map_title_hint: "The name that is shown", "El nombre que se muestra";
    new_map_area: "Area", "Zona";
    new_map_template: "Template", "Plantilla";
    new_map_copy: "Copy of a map", "Copia de un mapa";
    new_map_blank: "Blank", "En blanco";
    new_map_copy_images:
        "Copy its images into the package (otherwise it uses the original's)",
        "Copiar sus imágenes al paquete (si no, las usa del original)";
    new_map_keep_exits: "Keep its exits", "Conservar sus salidas";
    new_map_size: "Size (128 px tiles)", "Tamaño (fichas de 128 px)";
    new_map_image: "Background", "Fondo";
    new_map_image_none: "(generated grey squares)", "(cuadros grises generados)";
    new_map_pick_image: "Pick PNG…", "Elegir PNG…";
    new_map_connect: "Connect with", "Conectar con";
    new_map_connect_exit: "exit", "salida";
    new_map_own_exit: "through the new map's exit", "por la salida del nuevo mapa";
    new_map_create: "Create", "Crear";
    new_map_needs_target:
        "Pick a target package first (a mod or the team's package): the map is written there.",
        "Elige antes un paquete destino (un mod o el paquete del equipo): el mapa se escribe allí.";
    new_map_id_taken: "That id already exists", "Ese id ya existe";
    new_map_bad_name: "Only a-z, 0-9 and _ (1 to 64)", "Solo a-z, 0-9 y _ (1 a 64)";
    rename_map_title: "Rename map", "Renombrar mapa";
    delete_map_title: "Delete map", "Borrar mapa";
    delete_map_question:
        "Its files, its entry and its name are deleted from the target package. This cannot be \
         undone.",
        "Se borran sus ficheros, su entrada y su nombre del paquete destino. No se puede deshacer.";
    no_own_maps:
        "The target package has not added any maps.",
        "El paquete destino no ha añadido mapas.";
}

/// The counts under the world's tools.
pub fn world_stats(maps: usize, exits: usize, warps: usize) -> String {
    say!(
        format!("{maps} maps · {exits} exits · {warps} script warps"),
        format!("{maps} mapas · {exits} salidas · {warps} warps de script")
    )
}

/// A timeline track's name.
pub fn track_name(track: dwe_story::Track) -> String {
    use dwe_story::Track;
    use dwe_story::sequence::{PARTNER, PLAYER};
    match track {
        Track::Actor(PLAYER) => track_player().to_owned(),
        Track::Actor(PARTNER) => track_partner().to_owned(),
        Track::Actor(id) => format!("NPC {id}"),
        Track::Camera => track_camera().to_owned(),
        Track::Text => track_text().to_owned(),
        Track::Audio => track_audio().to_owned(),
        Track::Wait => track_wait().to_owned(),
        Track::Story => track_story().to_owned(),
    }
}

/// A length in frames of the original, and in seconds.
pub fn frames(frames: u32) -> String {
    let seconds = frames as f32 / dwe_story::sequence::FRAMES_PER_SECOND as f32;
    say!(
        format!("{frames} frames ({seconds:.1} s)"),
        format!("{frames} cuadros ({seconds:.1} s)")
    )
}

/// What the link says once the game started a section.
pub fn story_ran(script: u16, section: u16, sent: bool) -> String {
    let name = section_name(script, section);
    match (sent, language()) {
        (false, Language::English) => format!("{name} running"),
        (true, Language::English) => format!("{name} running from here"),
        (false, Language::Spanish) => format!("{name} en marcha"),
        (true, Language::Spanish) => format!("{name} en marcha desde aquí"),
    }
}

/// A character's drift: its name and the distance.
pub fn drift(name: &str, units: f32) -> String {
    format!("{name} {units:.0}")
}
