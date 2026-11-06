//! The story's commands for people: each `story.<name>` the interpreter runs, its operands and a
//! Spanish label (packages/formats/src/script/disassembly.cpp names the operands; the story API
//! renames a few, docs/modding/historia.md#nombres). The editor's palette and its forms come from
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
    pub label: &'static str,
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
    label: &'static str,
    operands: &'static [Operand],
    palette: bool,
) -> Command {
    Command {
        name,
        label,
        operands,
        flow: Flow::Plain,
        palette,
    }
}

const fn ends(name: &'static str, label: &'static str, operands: &'static [Operand]) -> Command {
    Command {
        name,
        label,
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
        "Mostrar texto",
        &[op("text", K::Text)],
        true,
    ),
    cmd("set_dialog_owner", "Quién habla", &[ENTITY], true),
    cmd("set_trigger", "Activar disparador", &[TRIGGER], true),
    cmd("unset_trigger", "Quitar disparador", &[TRIGGER], true),
    cmd("set_var", "Poner variable", &[VAR, n("value")], true),
    cmd("add_to_var", "Sumar a variable", &[VAR, n("value")], true),
    cmd("reduce_var", "Restar a variable", &[VAR, n("value")], true),
    cmd(
        "copy_var",
        "Copiar variable",
        &[op("source", K::Var), op("destination", K::Var)],
        false,
    ),
    cmd(
        "store_random",
        "Número al azar en variable",
        &[VAR, n("max")],
        true,
    ),
    cmd("give_item", "Dar objeto", &[ITEM, n("amount")], true),
    cmd("remove_item", "Quitar objeto", &[ITEM, n("amount")], true),
    cmd("add_money", "Dar dinero", &[n("amount")], true),
    cmd("reduce_money", "Quitar dinero", &[n("amount")], true),
    cmd("give_card", "Dar carta", &[n("card")], true),
    cmd("take_card", "Quitar carta", &[n("card")], false),
    cmd("learn_move", "Aprender técnica", &[n("move")], false),
    cmd("delay", "Esperar fotogramas", &[n("frames")], true),
    cmd("fadeout_hud", "Cerrar caja de texto", &[n("box")], true),
    cmd("play_sound", "Sonido", &[n("bank"), n("sound")], true),
    cmd("set_bgm", "Música", &[n("track")], true),
    cmd("stop_bgm", "Parar música", &[n("unknown01")], false),
    cmd("start_battle", "Empezar combate", &[n("unknown01")], true),
    cmd(
        "load_species_model",
        "Cargar modelo de especie",
        &[n("species")],
        true,
    ),
    cmd(
        "unload_species_model",
        "Descargar modelo de especie",
        &[n("model")],
        false,
    ),
    cmd(
        "spawn_npc",
        "Aparecer NPC",
        &[n("species"), n("slot"), n("autotalk")],
        true,
    ),
    cmd("unload_entity", "Quitar entidad", &[ENTITY], true),
    cmd("wait_for_entity", "Esperar a la entidad", &[ENTITY], true),
    cmd(
        "entity_look_at_entity",
        "Mirar a",
        &[ENTITY, TARGET_ENTITY],
        true,
    ),
    cmd(
        "entity_set_rotation",
        "Girar entidad",
        &[ENTITY, n("rotation")],
        true,
    ),
    cmd(
        "entity_walk_to",
        "Andar hasta",
        &[ENTITY, n("x"), n("z"), n("animation"), n("unknown07")],
        true,
    ),
    cmd(
        "entity_walk_to_with_camera",
        "Andar hasta (con cámara)",
        &[ENTITY, n("x"), n("z"), n("animation"), n("unknown07")],
        false,
    ),
    cmd(
        "entity_walk_to_entity",
        "Andar hasta entidad",
        &[ENTITY, n("animation"), TARGET_ENTITY],
        false,
    ),
    cmd(
        "entity_walk_to_entity_with_camera",
        "Andar hasta entidad (con cámara)",
        &[ENTITY, n("animation"), TARGET_ENTITY],
        false,
    ),
    cmd(
        "entity_move_to",
        "Mover hasta",
        &[ENTITY, n("x"), n("z"), n("speed"), n("unknown07")],
        false,
    ),
    cmd(
        "entity_move_to_entity",
        "Mover hasta entidad",
        &[ENTITY, TARGET_ENTITY, n("speed")],
        false,
    ),
    cmd(
        "entity_move_to_with_camera",
        "Mover hasta (con cámara)",
        &[ENTITY, n("x"), n("z"), n("speed"), n("unknown07")],
        false,
    ),
    cmd(
        "entity_move_to_entity_with_camera",
        "Mover hasta entidad (con cámara)",
        &[ENTITY, TARGET_ENTITY, n("speed")],
        false,
    ),
    cmd(
        "entity_move_to_axis",
        "Mover en un eje",
        &[ENTITY, n("position"), n("axis"), n("speed")],
        false,
    ),
    cmd(
        "entity_move_to_axis_with_camera",
        "Mover en un eje (con cámara)",
        &[ENTITY, n("position"), n("axis"), n("speed")],
        false,
    ),
    cmd(
        "play_animation",
        "Animación",
        &[ENTITY, n("animation")],
        true,
    ),
    cmd(
        "move_camera_to",
        "Mover cámara",
        &[n("speed"), n("x"), n("z")],
        true,
    ),
    cmd(
        "move_camera_to_entity",
        "Cámara a entidad",
        &[ENTITY, n("speed")],
        true,
    ),
    cmd(
        "set_obj_visibility",
        "Visibilidad de objeto",
        &[n("object"), n("visible")],
        true,
    ),
    cmd(
        "spawn_item",
        "Poner objeto en el suelo",
        &[ITEM, n("x"), n("z")],
        true,
    ),
    cmd(
        "spawn_chest",
        "Poner cofre",
        &[ITEM, n("x"), n("y"), n("z"), n("w"), TRIGGER],
        false,
    ),
    cmd("set_autotalk", "Habla sola", &[ENTITY, n("enabled")], false),
    cmd(
        "set_textbox_mode",
        "Modo de la caja de texto",
        &[n("mode"), n("frames")],
        false,
    ),
    cmd(
        "set_textbox_size",
        "Tamaño de la caja de texto",
        &[ENTITY, n("columns"), n("rows")],
        false,
    ),
    cmd(
        "set_textbox_origin",
        "Origen de la caja de texto",
        &[n("x"), n("y"), n("z")],
        false,
    ),
    cmd(
        "add_stat",
        "Subir atributo",
        &[n("stat"), n("value")],
        false,
    ),
    cmd(
        "reduce_stat",
        "Bajar atributo",
        &[n("stat"), n("value")],
        false,
    ),
    cmd(
        "set_stat",
        "Poner atributo",
        &[n("stat"), n("value")],
        false,
    ),
    cmd("call_routine", "Rutina del motor", &[n("routine")], false),
    cmd(
        "call_partner_routine",
        "Rutina del compañero",
        &[n("routine")],
        false,
    ),
    cmd("store_date", "Guardar fecha en variable", &[VAR], false),
    cmd(
        "store_partner_species",
        "Guardar especie del compañero",
        &[VAR],
        false,
    ),
    cmd(
        "store_species_type",
        "Guardar tipo de especie",
        &[op("source", K::Var), op("destination", K::Var)],
        false,
    ),
    cmd("advance_to_date_at", "Avanzar a la fecha de", &[VAR], false),
    cmd(
        "add_minutes_to_date_at",
        "Sumar minutos a la fecha de",
        &[VAR, n("minutes")],
        false,
    ),
    cmd(
        "set_inventory_size",
        "Tamaño del inventario",
        &[n("size")],
        false,
    ),
    cmd("deal_damage", "Hacer daño", &[VAR], false),
    cmd("remove_condition", "Quitar estado", &[n("mask")], false),
    cmd(
        "move_object_to",
        "Mover objeto",
        &[n("slot"), ENTITY, n("speed"), TARGET_ENTITY, n("x"), n("z")],
        false,
    ),
    cmd(
        "rotate_3d_object",
        "Girar objeto 3D",
        &[ENTITY, n("unknown02"), n("target")],
        false,
    ),
    cmd(
        "spawn_sprite_at_entity",
        "Sprite en entidad",
        &[ENTITY, n("node"), n("sprite")],
        false,
    ),
    cmd(
        "spawn_sprite_at_location",
        "Sprite en un punto",
        &[n("sprite"), n("x"), n("y"), n("z"), n("w")],
        false,
    ),
    cmd("spawn_boulder", "Aparecer roca", &[n("unknown01")], false),
    cmd(
        "move_boulder",
        "Mover roca",
        &[n("unknown01"), n("x"), n("z")],
        false,
    ),
    cmd("despawn_boulder", "Quitar roca", &[n("unknown01")], false),
    cmd(
        "set_rect_impassable",
        "Rectángulo impasable",
        &[n("unknown01"), n("x"), n("z"), n("width"), n("height")],
        false,
    ),
    cmd(
        "section_on_exit",
        "Sección al salir",
        &[op("section", K::Section)],
        false,
    ),
    cmd(
        "reset_entity_origin",
        "Reiniciar origen de entidad",
        &[ENTITY],
        false,
    ),
    cmd(
        "set_script",
        "Script del mapa",
        &[op("script", K::Script), op("map", K::Map)],
        false,
    ),
    cmd("set_merit", "Poner méritos", &[n("merit")], false),
    cmd("add_merit", "Sumar méritos", &[n("merit")], false),
    cmd("reduce_merit", "Restar méritos", &[n("merit")], false),
    cmd("store_map_id", "Guardar mapa en variable", &[VAR], false),
    ends(
        "warp_to",
        "Ir al mapa",
        &[
            op("screen", K::Map),
            op("exit", K::Exit),
            op("section", K::Section),
        ],
    ),
    ends(
        "jump_to_file",
        "Saltar a otro script",
        &[op("script", K::Script), op("section", K::Number)],
    ),
    ends("teleport", "Teletransportar", &[VAR]),
];

/// The reads a condition can test, with their operands; the comparison goes after (`>= 1`)
/// except for the yes/no ones.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Read {
    pub name: &'static str,
    pub label: &'static str,
    pub operand: Option<OperandKind>,
    /// Whether it is a yes/no test (no comparison).
    pub boolean: bool,
}

pub const READS: &[Read] = &[
    Read {
        name: "trigger",
        label: "disparador",
        operand: Some(K::Trigger),
        boolean: true,
    },
    Read {
        name: "var",
        label: "variable",
        operand: Some(K::Var),
        boolean: false,
    },
    Read {
        name: "items",
        label: "objetos",
        operand: Some(K::Item),
        boolean: false,
    },
    Read {
        name: "money",
        label: "dinero",
        operand: None,
        boolean: false,
    },
    Read {
        name: "stat",
        label: "atributo",
        operand: Some(K::Number),
        boolean: false,
    },
    Read {
        name: "cards",
        label: "cartas",
        operand: Some(K::Number),
        boolean: false,
    },
    Read {
        name: "knows_move",
        label: "sabe la técnica",
        operand: Some(K::Number),
        boolean: true,
    },
    Read {
        name: "has_condition",
        label: "tiene el estado",
        operand: Some(K::Number),
        boolean: true,
    },
];

pub fn command(name: &str) -> Option<&'static Command> {
    COMMANDS.iter().find(|command| command.name == name)
}

pub fn read(name: &str) -> Option<&'static Read> {
    READS.iter().find(|read| read.name == name)
}

impl Command {
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
