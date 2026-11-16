//! The story's commands for people: each `story.<name>` the interpreter runs, its operands and a
//! label in English and Spanish (packages/formats/src/script/disassembly.cpp names the operands; the story API
//! renames a few, docs/modding/story.md#names). The editor's palette and its forms come from
//! here, and so does the comment the emitter writes after a call of two or more operands.

/// What an operand is, so the editor can show and pick it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OperandKind {
    /// A number.
    Number,
    /// An entity: `PLAYER`, `PARTNER` or an NPC slot.
    Entity,
    Trigger,
    Var,
    /// A text key (`story_<n>.text.<offset>`).
    Text,
    /// A map by its original index.
    Map,
    /// An exit of a map (0-9).
    Exit,
    /// A section of the map's script (255: none).
    Section,
    /// A script id.
    Script,
    Item,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Operand {
    pub name: &'static str,
    pub kind: OperandKind,
}

/// How a command ends the flow, for the editor's palette.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Flow {
    Plain,
    /// `return story.<name>(...)`: a map change or a jump to another script.
    Ends,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Command {
    /// The name in `story`.
    pub name: &'static str,
    /// The label in English; [`Command::label`] gives the one in the current language.
    pub label_en: &'static str,
    /// The label in Spanish.
    pub label_es: &'static str,
    pub operands: &'static [Operand],
    pub flow: Flow,
    /// Whether the palette offers it (the rest show and edit but are rare or for the engine).
    pub palette: bool,
}

const fn op(name: &'static str, kind: OperandKind) -> Operand {
    Operand { name, kind }
}

const fn n(name: &'static str) -> Operand {
    op(name, OperandKind::Number)
}

const ENTITY: Operand = op("entity", OperandKind::Entity);
const TARGET_ENTITY: Operand = op("target_entity", OperandKind::Entity);
const TRIGGER: Operand = op("trigger", OperandKind::Trigger);
const VAR: Operand = op("var", OperandKind::Var);
const ITEM: Operand = op("item", OperandKind::Item);

const fn cmd(
    name: &'static str,
    label_en: &'static str,
    label_es: &'static str,
    operands: &'static [Operand],
    palette: bool,
) -> Command {
    Command {
        name,
        label_en,
        label_es,
        operands,
        flow: Flow::Plain,
        palette,
    }
}

const fn ends(
    name: &'static str,
    label_en: &'static str,
    label_es: &'static str,
    operands: &'static [Operand],
) -> Command {
    Command {
        name,
        label_en,
        label_es,
        operands,
        flow: Flow::Ends,
        palette: true,
    }
}

use OperandKind as K;

/// Every command, in the palette's order (the common ones first).
pub const COMMANDS: &[Command] = &[
    cmd(
        "show_textbox",
        "Show text",
        "Mostrar texto",
        &[op("text", K::Text)],
        true,
    ),
    cmd(
        "set_dialog_owner",
        "Who speaks",
        "Quién habla",
        &[ENTITY],
        true,
    ),
    cmd(
        "set_trigger",
        "Turn on trigger",
        "Activar disparador",
        &[TRIGGER],
        true,
    ),
    cmd(
        "unset_trigger",
        "Turn off trigger",
        "Quitar disparador",
        &[TRIGGER],
        true,
    ),
    cmd(
        "set_var",
        "Set variable",
        "Poner variable",
        &[VAR, n("value")],
        true,
    ),
    cmd(
        "add_to_var",
        "Add to variable",
        "Sumar a variable",
        &[VAR, n("value")],
        true,
    ),
    cmd(
        "reduce_var",
        "Subtract from variable",
        "Restar a variable",
        &[VAR, n("value")],
        true,
    ),
    cmd(
        "copy_var",
        "Copy variable",
        "Copiar variable",
        &[op("source", K::Var), op("destination", K::Var)],
        false,
    ),
    cmd(
        "store_random",
        "Random number into variable",
        "Número al azar en variable",
        &[VAR, n("max")],
        true,
    ),
    cmd(
        "give_item",
        "Give item",
        "Dar objeto",
        &[ITEM, n("amount")],
        true,
    ),
    cmd(
        "remove_item",
        "Remove item",
        "Quitar objeto",
        &[ITEM, n("amount")],
        true,
    ),
    cmd(
        "add_money",
        "Give money",
        "Dar dinero",
        &[n("amount")],
        true,
    ),
    cmd(
        "reduce_money",
        "Take money",
        "Quitar dinero",
        &[n("amount")],
        true,
    ),
    cmd("give_card", "Give card", "Dar carta", &[n("card")], true),
    cmd(
        "take_card",
        "Take card",
        "Quitar carta",
        &[n("card")],
        false,
    ),
    cmd(
        "learn_move",
        "Learn move",
        "Aprender técnica",
        &[n("move")],
        false,
    ),
    cmd(
        "delay",
        "Wait frames",
        "Esperar fotogramas",
        &[n("frames")],
        true,
    ),
    cmd(
        "fadeout_hud",
        "Close text box",
        "Cerrar caja de texto",
        &[n("box")],
        true,
    ),
    cmd(
        "play_sound",
        "Sound",
        "Sonido",
        &[n("bank"), n("sound")],
        true,
    ),
    cmd("set_bgm", "Music", "Música", &[n("track")], true),
    cmd(
        "stop_bgm",
        "Stop music",
        "Parar música",
        &[n("unknown01")],
        false,
    ),
    cmd(
        "start_battle",
        "Start battle",
        "Empezar combate",
        &[n("unknown01")],
        true,
    ),
    cmd(
        "load_species_model",
        "Load species model",
        "Cargar modelo de especie",
        &[n("species")],
        true,
    ),
    cmd(
        "unload_species_model",
        "Unload species model",
        "Descargar modelo de especie",
        &[n("model")],
        false,
    ),
    cmd(
        "spawn_npc",
        "Spawn NPC",
        "Aparecer NPC",
        &[n("species"), n("slot"), n("autotalk")],
        true,
    ),
    cmd(
        "unload_entity",
        "Remove entity",
        "Quitar entidad",
        &[ENTITY],
        true,
    ),
    cmd(
        "wait_for_entity",
        "Wait for the entity",
        "Esperar a la entidad",
        &[ENTITY],
        true,
    ),
    cmd(
        "entity_look_at_entity",
        "Look at",
        "Mirar a",
        &[ENTITY, TARGET_ENTITY],
        true,
    ),
    cmd(
        "entity_set_rotation",
        "Rotate entity",
        "Girar entidad",
        &[ENTITY, n("rotation")],
        true,
    ),
    cmd(
        "entity_walk_to",
        "Walk to",
        "Andar hasta",
        &[ENTITY, n("x"), n("z"), n("animation"), n("unknown07")],
        true,
    ),
    cmd(
        "entity_walk_to_with_camera",
        "Walk to (with camera)",
        "Andar hasta (con cámara)",
        &[ENTITY, n("x"), n("z"), n("animation"), n("unknown07")],
        false,
    ),
    cmd(
        "entity_walk_to_entity",
        "Walk to entity",
        "Andar hasta entidad",
        &[ENTITY, n("animation"), TARGET_ENTITY],
        false,
    ),
    cmd(
        "entity_walk_to_entity_with_camera",
        "Walk to entity (with camera)",
        "Andar hasta entidad (con cámara)",
        &[ENTITY, n("animation"), TARGET_ENTITY],
        false,
    ),
    cmd(
        "entity_move_to",
        "Move to",
        "Mover hasta",
        &[ENTITY, n("x"), n("z"), n("speed"), n("unknown07")],
        false,
    ),
    cmd(
        "entity_move_to_entity",
        "Move to entity",
        "Mover hasta entidad",
        &[ENTITY, TARGET_ENTITY, n("speed")],
        false,
    ),
    cmd(
        "entity_move_to_with_camera",
        "Move to (with camera)",
        "Mover hasta (con cámara)",
        &[ENTITY, n("x"), n("z"), n("speed"), n("unknown07")],
        false,
    ),
    cmd(
        "entity_move_to_entity_with_camera",
        "Move to entity (with camera)",
        "Mover hasta entidad (con cámara)",
        &[ENTITY, TARGET_ENTITY, n("speed")],
        false,
    ),
    cmd(
        "entity_move_to_axis",
        "Move along an axis",
        "Mover en un eje",
        &[ENTITY, n("position"), n("axis"), n("speed")],
        false,
    ),
    cmd(
        "entity_move_to_axis_with_camera",
        "Move along an axis (with camera)",
        "Mover en un eje (con cámara)",
        &[ENTITY, n("position"), n("axis"), n("speed")],
        false,
    ),
    cmd(
        "play_animation",
        "Animation",
        "Animación",
        &[ENTITY, n("animation")],
        true,
    ),
    cmd(
        "move_camera_to",
        "Move camera",
        "Mover cámara",
        &[n("speed"), n("x"), n("z")],
        true,
    ),
    cmd(
        "move_camera_to_entity",
        "Camera to entity",
        "Cámara a entidad",
        &[ENTITY, n("speed")],
        true,
    ),
    cmd(
        "set_obj_visibility",
        "Object visibility",
        "Visibilidad de objeto",
        &[n("object"), n("visible")],
        true,
    ),
    cmd(
        "spawn_item",
        "Put item on the ground",
        "Poner objeto en el suelo",
        &[ITEM, n("x"), n("z")],
        true,
    ),
    cmd(
        "spawn_chest",
        "Put chest",
        "Poner cofre",
        &[ITEM, n("x"), n("y"), n("z"), n("w"), TRIGGER],
        false,
    ),
    cmd(
        "set_autotalk",
        "Talks by itself",
        "Habla sola",
        &[ENTITY, n("enabled")],
        false,
    ),
    cmd(
        "set_textbox_mode",
        "Text box mode",
        "Modo de la caja de texto",
        &[n("mode"), n("frames")],
        false,
    ),
    cmd(
        "set_textbox_size",
        "Text box size",
        "Tamaño de la caja de texto",
        &[ENTITY, n("columns"), n("rows")],
        false,
    ),
    cmd(
        "set_textbox_origin",
        "Text box origin",
        "Origen de la caja de texto",
        &[n("x"), n("y"), n("z")],
        false,
    ),
    cmd(
        "add_stat",
        "Raise stat",
        "Subir atributo",
        &[n("stat"), n("value")],
        false,
    ),
    cmd(
        "reduce_stat",
        "Lower stat",
        "Bajar atributo",
        &[n("stat"), n("value")],
        false,
    ),
    cmd(
        "set_stat",
        "Set stat",
        "Poner atributo",
        &[n("stat"), n("value")],
        false,
    ),
    cmd(
        "call_routine",
        "Engine routine",
        "Rutina del motor",
        &[n("routine")],
        false,
    ),
    cmd(
        "call_partner_routine",
        "Partner routine",
        "Rutina del compañero",
        &[n("routine")],
        false,
    ),
    cmd(
        "store_date",
        "Store date in variable",
        "Guardar fecha en variable",
        &[VAR],
        false,
    ),
    cmd(
        "store_partner_species",
        "Store partner species",
        "Guardar especie del compañero",
        &[VAR],
        false,
    ),
    cmd(
        "store_species_type",
        "Store species type",
        "Guardar tipo de especie",
        &[op("source", K::Var), op("destination", K::Var)],
        false,
    ),
    cmd(
        "advance_to_date_at",
        "Advance to the date of",
        "Avanzar a la fecha de",
        &[VAR],
        false,
    ),
    cmd(
        "add_minutes_to_date_at",
        "Add minutes to the date of",
        "Sumar minutos a la fecha de",
        &[VAR, n("minutes")],
        false,
    ),
    cmd(
        "set_inventory_size",
        "Inventory size",
        "Tamaño del inventario",
        &[n("size")],
        false,
    ),
    cmd("deal_damage", "Deal damage", "Hacer daño", &[VAR], false),
    cmd(
        "remove_condition",
        "Remove condition",
        "Quitar estado",
        &[n("mask")],
        false,
    ),
    cmd(
        "move_object_to",
        "Move object",
        "Mover objeto",
        &[n("slot"), ENTITY, n("speed"), TARGET_ENTITY, n("x"), n("z")],
        false,
    ),
    cmd(
        "rotate_3d_object",
        "Rotate 3D object",
        "Girar objeto 3D",
        &[ENTITY, n("unknown02"), n("target")],
        false,
    ),
    cmd(
        "spawn_sprite_at_entity",
        "Sprite on entity",
        "Sprite en entidad",
        &[ENTITY, n("node"), n("sprite")],
        false,
    ),
    cmd(
        "spawn_sprite_at_location",
        "Sprite at a point",
        "Sprite en un punto",
        &[n("sprite"), n("x"), n("y"), n("z"), n("w")],
        false,
    ),
    cmd(
        "spawn_boulder",
        "Spawn boulder",
        "Aparecer roca",
        &[n("unknown01")],
        false,
    ),
    cmd(
        "move_boulder",
        "Move boulder",
        "Mover roca",
        &[n("unknown01"), n("x"), n("z")],
        false,
    ),
    cmd(
        "despawn_boulder",
        "Remove boulder",
        "Quitar roca",
        &[n("unknown01")],
        false,
    ),
    cmd(
        "set_rect_impassable",
        "Impassable rectangle",
        "Rectángulo impasable",
        &[n("unknown01"), n("x"), n("z"), n("width"), n("height")],
        false,
    ),
    cmd(
        "section_on_exit",
        "Section on exit",
        "Sección al salir",
        &[op("section", K::Section)],
        false,
    ),
    cmd(
        "reset_entity_origin",
        "Reset entity origin",
        "Reiniciar origen de entidad",
        &[ENTITY],
        false,
    ),
    cmd(
        "set_script",
        "Map script",
        "Script del mapa",
        &[op("script", K::Script), op("map", K::Map)],
        false,
    ),
    cmd(
        "set_merit",
        "Set merit",
        "Poner méritos",
        &[n("merit")],
        false,
    ),
    cmd(
        "add_merit",
        "Add merit",
        "Sumar méritos",
        &[n("merit")],
        false,
    ),
    cmd(
        "reduce_merit",
        "Subtract merit",
        "Restar méritos",
        &[n("merit")],
        false,
    ),
    cmd(
        "store_map_id",
        "Store map in variable",
        "Guardar mapa en variable",
        &[VAR],
        false,
    ),
    ends(
        "warp_to",
        "Go to map",
        "Ir al mapa",
        &[
            op("screen", K::Map),
            op("exit", K::Exit),
            op("section", K::Section),
        ],
    ),
    ends(
        "jump_to_file",
        "Jump to another script",
        "Saltar a otro script",
        &[op("script", K::Script), op("section", K::Number)],
    ),
    ends("teleport", "Teleport", "Teletransportar", &[VAR]),
];

/// The reads a condition can test, with their operands; the comparison goes after (`>= 1`)
/// except for the yes/no ones.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Read {
    pub name: &'static str,
    /// The label in English; [`Command::label`] gives the one in the current language.
    pub label_en: &'static str,
    /// The label in Spanish.
    pub label_es: &'static str,
    pub operand: Option<OperandKind>,
    /// Whether it is a yes/no test (no comparison).
    pub boolean: bool,
}

pub const READS: &[Read] = &[
    Read {
        name: "trigger",
        label_en: "trigger",
        label_es: "disparador",
        operand: Some(K::Trigger),
        boolean: true,
    },
    Read {
        name: "var",
        label_en: "variable",
        label_es: "variable",
        operand: Some(K::Var),
        boolean: false,
    },
    Read {
        name: "items",
        label_en: "items",
        label_es: "objetos",
        operand: Some(K::Item),
        boolean: false,
    },
    Read {
        name: "money",
        label_en: "money",
        label_es: "dinero",
        operand: None,
        boolean: false,
    },
    Read {
        name: "stat",
        label_en: "stat",
        label_es: "atributo",
        operand: Some(K::Number),
        boolean: false,
    },
    Read {
        name: "cards",
        label_en: "cards",
        label_es: "cartas",
        operand: Some(K::Number),
        boolean: false,
    },
    Read {
        name: "knows_move",
        label_en: "knows the move",
        label_es: "sabe la técnica",
        operand: Some(K::Number),
        boolean: true,
    },
    Read {
        name: "has_condition",
        label_en: "has the condition",
        label_es: "tiene el estado",
        operand: Some(K::Number),
        boolean: true,
    },
];

impl Read {
    /// The label in the current language.
    pub fn label(&self) -> &'static str {
        crate::language::pick(self.label_en, self.label_es)
    }
}

pub fn command(name: &str) -> Option<&'static Command> {
    COMMANDS.iter().find(|command| command.name == name)
}

pub fn read(name: &str) -> Option<&'static Read> {
    READS.iter().find(|read| read.name == name)
}

impl Command {
    /// The label in the current language.
    pub fn label(&self) -> &'static str {
        crate::language::pick(self.label_en, self.label_es)
    }

    /// The comment the emitter writes after a call: the operand names, for two or more.
    pub fn hint(&self) -> Option<String> {
        (self.operands.len() >= 2).then(|| {
            self.operands
                .iter()
                .map(|operand| operand.name)
                .collect::<Vec<_>>()
                .join(", ")
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn names_are_unique_and_hints_match_the_emitter() {
        let mut names: Vec<_> = COMMANDS.iter().map(|c| c.name).collect();
        names.sort_unstable();
        names.dedup();
        assert_eq!(names.len(), COMMANDS.len());
        assert_eq!(command("set_var").unwrap().hint().unwrap(), "var, value");
        assert_eq!(
            command("entity_walk_to").unwrap().hint().unwrap(),
            "entity, x, z, animation, unknown07"
        );
        assert_eq!(command("show_textbox").unwrap().hint(), None);
    }
}
