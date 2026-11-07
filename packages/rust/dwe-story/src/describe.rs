//! Story code in words, for the editor's list: one Spanish line a statement, with the names of
//! `story/names`, the dialogue lines of the text tables and the map ids of `data/maps`.

use dwe_map::{MapTable, StoryNames};

use crate::ast::{BinOp, Expr, Stmt, StmtKind};
use crate::commands::{self, OperandKind};
use crate::texts::StoryTexts;
use crate::write::{expr_text, is_link};

/// What the descriptions look names up in.
#[derive(Clone, Copy)]
pub struct Names<'a> {
    pub story: &'a StoryNames,
    pub texts: &'a StoryTexts,
    pub maps: &'a MapTable,
}

/// Longest dialogue line shown in a description.
const TEXT_PREVIEW: usize = 60;

impl Names<'_> {
    pub fn trigger(&self, number: i64) -> String {
        match u32::try_from(number)
            .ok()
            .and_then(|n| self.story.trigger(n))
        {
            Some(name) => format!("{number} {name}"),
            None => number.to_string(),
        }
    }

    pub fn var(&self, number: i64) -> String {
        match u32::try_from(number)
            .ok()
            .and_then(|n| self.story.variable(n))
        {
            Some(name) => format!("{number} {name}"),
            None => number.to_string(),
        }
    }

    /// A map by its original index: its id, or the number.
    pub fn map(&self, index: i64) -> String {
        i32::try_from(index)
            .ok()
            .and_then(|index| self.maps.find_by_original_index(index))
            .map_or_else(|| format!("mapa {index}"), |entry| entry.id.clone())
    }

    /// A text key with the start of its line.
    pub fn text(&self, key: &str) -> String {
        match self.texts.text(key) {
            Some(line) => {
                let line = line.replace('\n', " ");
                let short: String = line.chars().take(TEXT_PREVIEW).collect();
                let more = if line.chars().count() > TEXT_PREVIEW {
                    "…"
                } else {
                    ""
                };
                format!("«{short}{more}»")
            }
            None => key.to_owned(),
        }
    }

    pub fn entity(&self, expr: &Expr) -> String {
        match expr.unparen() {
            Expr::Name(name) if name == "PLAYER" => "jugador".to_owned(),
            Expr::Name(name) if name == "PARTNER" => "compañero".to_owned(),
            Expr::Number(number) => format!("NPC {number}"),
            other => expr_text(other),
        }
    }

    /// An operand as its kind reads.
    pub fn operand(&self, kind: OperandKind, expr: &Expr) -> String {
        let number = expr.as_number();
        match (kind, number, expr.unparen()) {
            (OperandKind::Entity, ..) => self.entity(expr),
            (OperandKind::Trigger, Some(n), _) => format!("disparador {}", self.trigger(n)),
            (OperandKind::Var, Some(n), _) => format!("variable {}", self.var(n)),
            (OperandKind::Map, Some(n), _) => self.map(n),
            (OperandKind::Exit, Some(n), _) => format!("salida {n}"),
            (OperandKind::Section, Some(255), _) => "sin sección".to_owned(),
            (OperandKind::Section, Some(n), _) => format!("sección {n:#04x}"),
            (OperandKind::Item, Some(n), _) => format!("objeto {n}"),
            (OperandKind::Text, _, Expr::Str(key)) => self.text(key),
            _ => expr_text(expr),
        }
    }

    /// A call of the story API in words.
    pub fn call(&self, call: &Expr) -> String {
        let Some((name, arguments)) = call.story_call_name() else {
            if let Some(routine) = call.routine_call_name() {
                return format!("Rutina {routine}");
            }
            if matches!(call, Expr::Call(callee, _) if matches!(callee.as_ref(), Expr::Name(n) if n == "original"))
            {
                return "Sección original".to_owned();
            }
            return expr_text(call);
        };
        let Some(command) = commands::command(name) else {
            return format!("{name}({})", join(arguments.iter().map(expr_text)));
        };
        let operands = arguments.iter().enumerate().map(|(index, argument)| {
            match command.operands.get(index) {
                Some(operand) if is_named(operand.kind) => self.operand(operand.kind, argument),
                Some(operand) => format!("{} {}", operand.name, expr_text(argument)),
                None => expr_text(argument),
            }
        });
        format!("{}: {}", command.label, join(operands))
    }

    /// A condition in words.
    pub fn condition(&self, expr: &Expr) -> String {
        match expr {
            Expr::Paren(inner) => format!("({})", self.condition(inner)),
            Expr::Not(inner) => match inner.story_call_name() {
                Some(("trigger", [n])) => format!(
                    "disparador {} apagado",
                    n.as_number()
                        .map_or_else(|| expr_text(n), |n| self.trigger(n))
                ),
                _ => format!("no {}", self.condition(inner)),
            },
            Expr::Binary(op @ (BinOp::And | BinOp::Or), left, right) => format!(
                "{} {} {}",
                self.condition(left),
                if *op == BinOp::And { "y" } else { "o" },
                self.condition(right)
            ),
            Expr::Binary(op, left, right) if op.is_comparison() => {
                if let Some(routine) = left.routine_call_name() {
                    return format!("la rutina {routine} acaba el script");
                }
                format!(
                    "{} {} {}",
                    self.read(left),
                    comparison(*op),
                    expr_text(right)
                )
            }
            other => self.read(other),
        }
    }

    /// A read of the story's state (`story.var(3)`) or a local, in words.
    fn read(&self, expr: &Expr) -> String {
        if let Some((name, arguments)) = expr.story_call_name() {
            let argument = arguments.first().and_then(Expr::as_number);
            return match (name, argument) {
                ("trigger", Some(n)) => format!("disparador {} encendido", self.trigger(n)),
                ("var", Some(n)) => format!("variable {}", self.var(n)),
                ("money", _) => "dinero".to_owned(),
                (name, Some(n)) => match commands::read(name) {
                    Some(read) => format!("{} {n}", read.label),
                    None => expr_text(expr),
                },
                _ => expr_text(expr),
            };
        }
        match expr {
            Expr::Name(name) if name == "choice" => "opción elegida".to_owned(),
            Expr::Name(name) if name == "value" => "valor".to_owned(),
            Expr::Name(name) if name.starts_with("state") => {
                format!("estado {}", &name["state".len()..])
            }
            other => expr_text(other),
        }
    }

    /// A statement in one line (the blocks inside go on lines of their own).
    pub fn stmt(&self, stmt: &Stmt) -> String {
        if let Some(routine) = is_link(&stmt.kind) {
            return format!("Llamar a la rutina {routine} (si acaba el script, acaba aquí)");
        }
        match &stmt.kind {
            StmtKind::Call(call) => self.call(call),
            StmtKind::If { arms, .. } => format!("Si {}", self.condition(&arms[0].0)),
            StmtKind::While { condition, .. } => match condition {
                Expr::Bool(true) => "Repetir".to_owned(),
                other => format!("Mientras {}", self.condition(other)),
            },
            StmtKind::Do(_) => "Bloque".to_owned(),
            StmtKind::Break => "Salir del bucle".to_owned(),
            StmtKind::Return(values) => match values.as_slice() {
                [] => "Volver a quien llamó".to_owned(),
                [Expr::Name(name)] if name == "ENDED" => "Fin del script".to_owned(),
                [call @ Expr::Call(..)] => {
                    if let Some(routine) = call.routine_call_name() {
                        format!("Seguir en la rutina {routine}")
                    } else {
                        format!("{} y fin", self.call(call))
                    }
                }
                values => format!("Devolver {}", join(values.iter().map(expr_text))),
            },
            StmtKind::Local { name, value, .. } => match value.story_call_name() {
                Some(("set_selection", [key, count])) => format!(
                    "Elegir entre {} opciones: {}",
                    expr_text(count),
                    match key {
                        Expr::Str(key) => self.text(key),
                        other => expr_text(other),
                    }
                ),
                _ => format!(
                    "{} = {}",
                    self.read(&Expr::name(name)),
                    self.condition(value)
                ),
            },
            StmtKind::Assign { name, value } => {
                format!("{} = {}", self.read(&Expr::name(name)), expr_text(value))
            }
            StmtKind::Function { name, .. } => format!("Rutina {name}"),
            StmtKind::Comment(text) => format!("-- {text}"),
        }
    }
}

/// Whether an operand of this kind reads better by its name than as `name value`.
fn is_named(kind: OperandKind) -> bool {
    !matches!(kind, OperandKind::Number | OperandKind::Script)
}

fn comparison(op: BinOp) -> &'static str {
    match op {
        BinOp::Eq => "=",
        BinOp::Ne => "≠",
        BinOp::Lt => "<",
        BinOp::Le => "≤",
        BinOp::Gt => ">",
        BinOp::Ge => "≥",
        other => other.symbol(),
    }
}

fn join(parts: impl Iterator<Item = String>) -> String {
    parts.collect::<Vec<_>>().join(", ")
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parse::parse_chunk;

    fn names() -> (StoryNames, StoryTexts, MapTable) {
        let mut story = StoryNames::default();
        story.triggers.insert(12, "door_open".to_owned());
        let mut texts = StoryTexts::new("en");
        texts.set("story_001.text.0010", Some("Hello there"));
        (story, texts, MapTable::default())
    }

    #[test]
    fn says_statements_in_spanish() {
        let (story, texts, maps) = names();
        let names = Names {
            story: &story,
            texts: &texts,
            maps: &maps,
        };
        let block = parse_chunk(
            "story.show_textbox(\"story_001.text.0010\")\nstory.set_trigger(12)\nif not story.trigger(12) and story.var(3) >= 2 then return ENDED end\nstory.set_dialog_owner(PLAYER)\nreturn story.warp_to(5, 2, 255)\n",
        )
        .unwrap();
        let lines: Vec<String> = block.iter().map(|stmt| names.stmt(stmt)).collect();
        assert_eq!(lines[0], "Mostrar texto: «Hello there»");
        assert_eq!(lines[1], "Activar disparador: disparador 12 door_open");
        assert_eq!(
            lines[2],
            "Si disparador 12 door_open apagado y variable 3 ≥ 2"
        );
        assert_eq!(lines[3], "Quién habla: jugador");
        assert_eq!(lines[4], "Ir al mapa: mapa 5, salida 2, sin sección y fin");
    }
}
