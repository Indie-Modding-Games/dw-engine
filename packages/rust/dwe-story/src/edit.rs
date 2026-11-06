//! Edits of a section's code by place: a [`StmtPath`] names a statement inside nested blocks
//! (an `if`'s arms and `else`, a loop's or a `do`'s body), and the functions here change the
//! statement there. The editor records the file before and after as one undo step.

use crate::ast::{BinOp, Block, Expr, Stmt, StmtKind};
use crate::commands::Command;

/// A statement inside a block: the blocks to go into (statement, child block) and its position.
/// The child blocks of an `if` are its arms in order and then its `else`; a loop or a `do` has
/// one.
#[derive(Clone, Debug, Default, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct StmtPath {
    pub blocks: Vec<(usize, usize)>,
    pub index: usize,
}

impl StmtPath {
    pub fn top(index: usize) -> Self {
        Self {
            blocks: Vec::new(),
            index,
        }
    }

    /// The path of the first statement of a child block of this statement.
    pub fn child(&self, block: usize, index: usize) -> Self {
        let mut blocks = self.blocks.clone();
        blocks.push((self.index, block));
        Self { blocks, index }
    }
}

/// The child blocks of a statement.
pub fn children(stmt: &Stmt) -> Vec<&Block> {
    match &stmt.kind {
        StmtKind::If { arms, otherwise } => arms
            .iter()
            .map(|(_, body)| body)
            .chain(otherwise.iter())
            .collect(),
        StmtKind::While { body, .. } | StmtKind::Do(body) | StmtKind::Function { body, .. } => {
            vec![body]
        }
        _ => Vec::new(),
    }
}

fn child_mut(stmt: &mut Stmt, index: usize) -> Option<&mut Block> {
    match &mut stmt.kind {
        StmtKind::If { arms, otherwise } => {
            if index < arms.len() {
                Some(&mut arms[index].1)
            } else if index == arms.len() {
                otherwise.as_mut()
            } else {
                None
            }
        }
        StmtKind::While { body, .. } | StmtKind::Do(body) | StmtKind::Function { body, .. } => {
            (index == 0).then_some(body)
        }
        _ => None,
    }
}

/// The block a path's statement is in.
pub fn block_of<'a>(root: &'a Block, path: &StmtPath) -> Option<&'a Block> {
    let mut block = root;
    for &(stmt, child) in &path.blocks {
        block = children(block.get(stmt)?).get(child).copied()?;
    }
    Some(block)
}

pub fn block_of_mut<'a>(root: &'a mut Block, path: &StmtPath) -> Option<&'a mut Block> {
    let mut block = root;
    for &(stmt, child) in &path.blocks {
        block = child_mut(block.get_mut(stmt)?, child)?;
    }
    Some(block)
}

pub fn stmt_at<'a>(root: &'a Block, path: &StmtPath) -> Option<&'a Stmt> {
    block_of(root, path)?.get(path.index)
}

/// Replaces the statement at a path.
pub fn replace(root: &mut Block, path: &StmtPath, stmt: Stmt) -> bool {
    match block_of_mut(root, path).and_then(|block| block.get_mut(path.index)) {
        Some(known) => {
            *known = stmt;
            true
        }
        None => false,
    }
}

/// Inserts a statement at a path (before what is there; at the end when past it).
pub fn insert(root: &mut Block, path: &StmtPath, stmt: Stmt) -> bool {
    match block_of_mut(root, path) {
        Some(block) if path.index <= block.len() => {
            block.insert(path.index, stmt);
            true
        }
        _ => false,
    }
}

pub fn remove(root: &mut Block, path: &StmtPath) -> Option<Stmt> {
    let block = block_of_mut(root, path)?;
    (path.index < block.len()).then(|| block.remove(path.index))
}

/// Swaps a statement with the one after or before it in its block; returns its new path.
pub fn move_stmt(root: &mut Block, path: &StmtPath, later: bool) -> Option<StmtPath> {
    let block = block_of_mut(root, path)?;
    let other = if later {
        path.index.checked_add(1).filter(|&i| i < block.len())?
    } else {
        path.index.checked_sub(1)?
    };
    if path.index >= block.len() {
        return None;
    }
    block.swap(path.index, other);
    Some(StmtPath {
        blocks: path.blocks.clone(),
        index: other,
    })
}

/// Sets the condition of an arm of the `if` at a path.
pub fn set_condition(root: &mut Block, path: &StmtPath, arm: usize, condition: Expr) -> bool {
    let Some(stmt) = block_of_mut(root, path).and_then(|block| block.get_mut(path.index)) else {
        return false;
    };
    match &mut stmt.kind {
        StmtKind::If { arms, .. } if arm < arms.len() => {
            arms[arm].0 = condition;
            true
        }
        StmtKind::While {
            condition: known, ..
        } if arm == 0 => {
            *known = condition;
            true
        }
        _ => false,
    }
}

/// A new call of a command with its operands at 0 (the text key empty, entities the player), and
/// the emitter's comment; a command that ends the flow comes as `return story.<name>(...)`.
pub fn new_command(command: &Command) -> Stmt {
    use crate::commands::{Flow, OperandKind};
    let arguments = command
        .operands
        .iter()
        .map(|operand| match operand.kind {
            OperandKind::Entity => Expr::name("PLAYER"),
            OperandKind::Text => Expr::Str(String::new()),
            OperandKind::Section => Expr::Number(255),
            _ => Expr::Number(0),
        })
        .collect();
    let call = Expr::story_call(command.name, arguments);
    let kind = match command.flow {
        Flow::Ends => StmtKind::Return(vec![call]),
        Flow::Plain => StmtKind::Call(call),
    };
    Stmt {
        kind,
        comment: command.hint(),
        line: 0,
    }
}

/// The story call of a statement (`story.x(...)` or `return story.x(...)`) to edit in place.
pub fn call_of_mut(stmt: &mut Stmt) -> Option<(&str, &mut Vec<Expr>)> {
    let call = match &mut stmt.kind {
        StmtKind::Call(call) => call,
        StmtKind::Return(values) if values.len() == 1 => &mut values[0],
        StmtKind::Local { value, .. } => value,
        _ => return None,
    };
    match call {
        Expr::Call(callee, arguments) => match &**callee {
            Expr::Field(object, name) if matches!(&**object, Expr::Name(n) if n == "story") => {
                Some((name.as_str(), arguments))
            }
            _ => None,
        },
        _ => None,
    }
}

/// The blank statements the palette adds besides commands.
pub fn new_if(condition: Expr) -> Stmt {
    Stmt::new(StmtKind::If {
        arms: vec![(condition, Block::new())],
        otherwise: None,
    })
}

pub fn end_script() -> Stmt {
    Stmt::with_comment(StmtKind::Return(vec![Expr::name("ENDED")]), "end_section")
}

/// A condition the editor's quick fields change: a trigger on or off, or a variable compared
/// with a number.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SimpleCondition {
    Trigger { number: i64, on: bool },
    Var { number: i64, op: BinOp, value: i64 },
}

impl SimpleCondition {
    /// The condition an expression is, when it is that simple.
    pub fn of(expr: &Expr) -> Option<Self> {
        let number_of = |expr: &Expr, read: &str| match expr.story_call_name() {
            Some((name, [number])) if name == read => number.as_number(),
            _ => None,
        };
        match expr.unparen() {
            Expr::Not(inner) => number_of(inner.unparen(), "trigger")
                .map(|number| Self::Trigger { number, on: false }),
            Expr::Binary(op, left, right) if op.is_comparison() => Some(Self::Var {
                number: number_of(left.unparen(), "var")?,
                op: *op,
                value: right.as_number()?,
            }),
            other => number_of(other, "trigger").map(|number| Self::Trigger { number, on: true }),
        }
    }

    pub fn expr(self) -> Expr {
        match self {
            Self::Trigger { number, on } => {
                let read = Expr::story_call("trigger", vec![Expr::Number(number)]);
                if on { read } else { Expr::Not(Box::new(read)) }
            }
            Self::Var { number, op, value } => Expr::Binary(
                op,
                Box::new(Expr::story_call("var", vec![Expr::Number(number)])),
                Box::new(Expr::Number(value)),
            ),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::commands::command;
    use crate::parse::parse_chunk;
    use crate::write::block_text;

    #[test]
    fn simple_conditions_read_and_write_back() {
        use crate::parse::parse_expr;
        use crate::write::expr_text;
        for code in [
            "story.trigger(12)",
            "not story.trigger(12)",
            "story.var(3) >= 2",
        ] {
            let condition = SimpleCondition::of(&parse_expr(code).unwrap()).unwrap();
            assert_eq!(expr_text(&condition.expr()), code);
        }
        let other = parse_expr("story.trigger(1) and story.trigger(2)").unwrap();
        assert_eq!(SimpleCondition::of(&other), None);
        assert_eq!(
            SimpleCondition::of(&parse_expr("story.items(3) > 0").unwrap()),
            None
        );
    }

    #[test]
    fn edits_by_path() {
        let mut block = parse_chunk(
            "if story.trigger(1) then\n  story.set_var(1, 2)\nelse\n  story.delay(3)\nend\nreturn ENDED\n",
        )
        .unwrap();
        let inner = StmtPath::top(0).child(1, 0);
        assert!(insert(
            &mut block,
            &StmtPath::top(0).child(0, 1),
            new_command(command("set_trigger").unwrap())
        ));
        let moved = move_stmt(&mut block, &StmtPath::top(0).child(0, 1), false).unwrap();
        assert_eq!(moved.index, 0);
        let mut delay = stmt_at(&block, &inner).unwrap().clone();
        if let Some((_, arguments)) = call_of_mut(&mut delay) {
            arguments[0] = Expr::Number(9);
        }
        assert!(replace(&mut block, &inner, delay));
        assert!(set_condition(
            &mut block,
            &StmtPath::top(0),
            0,
            Expr::Not(Box::new(Expr::story_call("trigger", vec![Expr::Number(2)])))
        ));
        assert!(remove(&mut block, &StmtPath::top(1)).is_some());
        assert_eq!(
            block_text(&block),
            "if not story.trigger(2) then\n  story.set_trigger(0)\n  story.set_var(1, 2) -- var, value\nelse\n  story.delay(9)\nend\n"
                .replace(" -- var, value", "")
        );
        assert!(insert(&mut block, &StmtPath::top(1), end_script()));
        assert!(block_text(&block).ends_with("return ENDED -- end_section\n"));
        assert!(new_command(command("warp_to").unwrap()).kind != StmtKind::Break);
    }
}
