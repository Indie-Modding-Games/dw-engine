//! Writes the tree of [`crate::ast`] back as Lua, in the emitter's layout: two spaces a level, one
//! statement a line, the comment at the end of its line, and the `jump_and_link` form
//! (`if routines.<name>() == ENDED then return ENDED end`) on one line.

use crate::ast::{BinOp, Block, Expr, Stmt, StmtKind, TableField};
use crate::lex::quote;

/// Writes a block at an indentation level.
pub fn write_block(block: &Block, level: usize, out: &mut String) {
    for stmt in block {
        write_stmt(stmt, level, out);
    }
}

/// A block as text, from level 0 (the raw editor).
pub fn block_text(block: &Block) -> String {
    let mut out = String::new();
    write_block(block, 0, &mut out);
    out
}

fn indent(level: usize, out: &mut String) {
    for _ in 0..level {
        out.push_str("  ");
    }
}

fn end_line(comment: &Option<String>, out: &mut String) {
    if let Some(comment) = comment {
        out.push_str(" -- ");
        out.push_str(comment);
    }
    out.push('\n');
}

/// Whether an `if` is the one-line call of a routine whose end ends the section.
pub fn is_link(stmt: &StmtKind) -> Option<&str> {
    let StmtKind::If { arms, otherwise } = stmt else {
        return None;
    };
    if otherwise.is_some() || arms.len() != 1 {
        return None;
    }
    let (condition, body) = &arms[0];
    let Expr::Binary(BinOp::Eq, call, ended) = condition else {
        return None;
    };
    let routine = call.routine_call_name()?;
    let returns_ended = matches!(ended.as_ref(), Expr::Name(n) if n == "ENDED")
        && body.len() == 1
        && body[0].comment.is_none()
        && matches!(&body[0].kind, StmtKind::Return(values)
            if values.len() == 1 && matches!(&values[0], Expr::Name(n) if n == "ENDED"));
    returns_ended.then_some(routine)
}

pub fn write_stmt(stmt: &Stmt, level: usize, out: &mut String) {
    indent(level, out);
    match &stmt.kind {
        StmtKind::Local {
            name,
            attrib,
            value,
        } => {
            out.push_str("local ");
            out.push_str(name);
            if let Some(attrib) = attrib {
                out.push_str(&format!(" <{attrib}>"));
            }
            out.push_str(" = ");
            write_expr(value, level, out);
            end_line(&stmt.comment, out);
        }
        StmtKind::Function {
            table,
            name,
            params,
            body,
        } => {
            out.push_str(&format!("function {table}.{name}({})", params.join(", ")));
            end_line(&stmt.comment, out);
            write_block(body, level + 1, out);
            indent(level, out);
            out.push_str("end\n");
        }
        StmtKind::If { arms, otherwise } => {
            if is_link(&stmt.kind).is_some() {
                out.push_str("if ");
                write_expr(&arms[0].0, level, out);
                out.push_str(" then return ENDED end");
                end_line(&stmt.comment, out);
                return;
            }
            for (index, (condition, body)) in arms.iter().enumerate() {
                if index > 0 {
                    indent(level, out);
                    out.push_str("else");
                }
                out.push_str("if ");
                write_expr(condition, level, out);
                out.push_str(" then");
                if index == 0 {
                    end_line(&stmt.comment, out);
                } else {
                    out.push('\n');
                }
                write_block(body, level + 1, out);
            }
            if let Some(body) = otherwise {
                indent(level, out);
                out.push_str("else\n");
                write_block(body, level + 1, out);
            }
            indent(level, out);
            out.push_str("end\n");
        }
        StmtKind::While { condition, body } => {
            out.push_str("while ");
            write_expr(condition, level, out);
            out.push_str(" do");
            end_line(&stmt.comment, out);
            write_block(body, level + 1, out);
            indent(level, out);
            out.push_str("end\n");
        }
        StmtKind::Do(body) => {
            out.push_str("do");
            end_line(&stmt.comment, out);
            write_block(body, level + 1, out);
            indent(level, out);
            out.push_str("end\n");
        }
        StmtKind::Break => {
            out.push_str("break");
            end_line(&stmt.comment, out);
        }
        StmtKind::Return(values) => {
            out.push_str("return");
            for (index, value) in values.iter().enumerate() {
                out.push_str(if index == 0 { " " } else { ", " });
                write_expr(value, level, out);
            }
            end_line(&stmt.comment, out);
        }
        StmtKind::Assign { name, value } => {
            out.push_str(name);
            out.push_str(" = ");
            write_expr(value, level, out);
            end_line(&stmt.comment, out);
        }
        StmtKind::Call(call) => {
            write_expr(call, level, out);
            end_line(&stmt.comment, out);
        }
        StmtKind::Comment(text) => {
            out.push_str("--");
            if !text.is_empty() {
                out.push(' ');
                out.push_str(text);
            }
            out.push('\n');
        }
    }
}

/// An expression as text (a function or table inside it at an indentation level).
pub fn expr_text(expr: &Expr) -> String {
    let mut out = String::new();
    write_expr(expr, 0, &mut out);
    out
}

pub fn write_expr(expr: &Expr, level: usize, out: &mut String) {
    match expr {
        Expr::Nil => out.push_str("nil"),
        Expr::Bool(value) => out.push_str(if *value { "true" } else { "false" }),
        Expr::Number(number) => out.push_str(&number.to_string()),
        Expr::Str(text) => out.push_str(&quote(text)),
        Expr::Name(name) => out.push_str(name),
        Expr::Field(object, name) => {
            write_operand(object, u8::MAX, level, out);
            out.push('.');
            out.push_str(name);
        }
        Expr::Call(callee, arguments) => {
            write_operand(callee, u8::MAX, level, out);
            out.push('(');
            for (index, argument) in arguments.iter().enumerate() {
                if index > 0 {
                    out.push_str(", ");
                }
                write_expr(argument, level, out);
            }
            out.push(')');
        }
        Expr::Paren(inner) => {
            out.push('(');
            write_expr(inner, level, out);
            out.push(')');
        }
        Expr::Not(inner) => {
            out.push_str("not ");
            // `not` binds tighter than any binary operator.
            write_operand(inner, NOT_PRECEDENCE, level, out);
        }
        Expr::Binary(op, left, right) => {
            write_operand(left, op.precedence(), level, out);
            out.push(' ');
            out.push_str(op.symbol());
            out.push(' ');
            write_operand(right, op.precedence() + 1, level, out);
        }
        Expr::Table(fields) => {
            if fields.is_empty() {
                out.push_str("{}");
                return;
            }
            out.push_str("{ ");
            for (index, field) in fields.iter().enumerate() {
                if index > 0 {
                    out.push_str(", ");
                }
                match field {
                    TableField::Named(name, value) => {
                        out.push_str(name);
                        out.push_str(" = ");
                        write_expr(value, level, out);
                    }
                    TableField::Keyed(key, value) => {
                        out.push('[');
                        write_expr(key, level, out);
                        out.push_str("] = ");
                        write_expr(value, level, out);
                    }
                    TableField::Positional(value) => write_expr(value, level, out),
                }
            }
            out.push_str(" }");
        }
        Expr::Function { params, body } => {
            out.push_str(&format!("function({})\n", params.join(", ")));
            write_block(body, level + 1, out);
            indent(level, out);
            out.push_str("end");
        }
    }
}

/// How tightly `not` binds: above every binary operator.
const NOT_PRECEDENCE: u8 = 5;

/// Writes an operand, in parentheses when it binds looser than its place needs (only trees built
/// by the editor: a parsed tree keeps its own parentheses).
fn write_operand(expr: &Expr, needs: u8, level: usize, out: &mut String) {
    let binds = match expr {
        Expr::Binary(op, ..) => op.precedence(),
        Expr::Not(_) => NOT_PRECEDENCE,
        _ => u8::MAX,
    };
    let wrap = binds < needs && !matches!(expr, Expr::Paren(_));
    if wrap {
        out.push('(');
        write_expr(expr, level, out);
        out.push(')');
    } else {
        write_expr(expr, level, out);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parse::parse_chunk;

    #[test]
    fn writes_back_what_it_read() {
        let code = "\
story.set_var(5, 1) -- var, value
if (story.var(1) >= 7 and story.var(1) < 19) and not story.trigger(232) then
  story.spawn_npc(152, 1, 0)
elseif story.trigger(2) then
  return story.warp_to(111, 3, 255)
else
  if routines.at_0010() == ENDED then return ENDED end -- jump_and_link
  return -- jump_return
end
do
  local choice <const> = story.set_selection(\"story_001.text.0010\", 2)
  while true do
    state1 = -2
    break
  end
end
";
        let block = parse_chunk(code).unwrap();
        assert_eq!(block_text(&block), code);
    }

    #[test]
    fn adds_the_parentheses_a_built_tree_needs() {
        let a = Expr::story_call("trigger", vec![Expr::Number(1)]);
        let b = Expr::story_call("trigger", vec![Expr::Number(2)]);
        let c = Expr::story_call("trigger", vec![Expr::Number(3)]);
        let or = Expr::Binary(BinOp::Or, Box::new(b), Box::new(c));
        let and = Expr::Binary(BinOp::And, Box::new(a), Box::new(or));
        assert_eq!(
            expr_text(&and),
            "story.trigger(1) and (story.trigger(2) or story.trigger(3))"
        );
        let not = Expr::Not(Box::new(and));
        assert!(expr_text(&not).starts_with("not (story.trigger(1)"));
    }
}
