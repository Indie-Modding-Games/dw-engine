//! What the story must hold for a section to get past its early ends: the conditions of the `if`s
//! at its top whose arms do nothing but end the section (`if story.trigger(12) then return ENDED
//! end`), and
//! the trigger and variable writes that make them hold, for an editor that runs the section in a
//! game to see it.

use crate::ast::{BinOp, Block, Expr, StmtKind};
use crate::events::negate;
use crate::file::StoryFile;

/// A story write that makes a condition hold.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum Need {
    Trigger { number: i64, on: bool },
    Var { number: i64, value: i64 },
}

/// The writes a section needs, and the conditions no write here makes hold (items, money...).
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Needs {
    pub writes: Vec<Need>,
    pub others: Vec<Expr>,
}

/// The conditions that must hold for a section's code to get past its early ends, following a
/// `return routines.<name>()` at the top into the routine.
pub fn conditions(file: &StoryFile, block: &Block) -> Vec<Expr> {
    let mut found = Vec::new();
    let mut block = block;
    let mut seen = Vec::new();
    'blocks: loop {
        for stmt in block {
            match &stmt.kind {
                StmtKind::If { arms, otherwise } if otherwise.is_none() => {
                    if !arms.iter().all(|(_, body)| only_ends(body)) {
                        break 'blocks;
                    }
                    found.extend(arms.iter().map(|(condition, _)| negate(condition)));
                }
                StmtKind::Comment(_) | StmtKind::Local { .. } | StmtKind::Assign { .. } => {}
                StmtKind::Call(_) => {}
                StmtKind::Return(values) => {
                    let routine = values.first().and_then(Expr::routine_call_name);
                    match routine.and_then(|name| file.routine(name)) {
                        Some(routine) if !seen.contains(&routine.name) => {
                            seen.push(routine.name.clone());
                            block = &routine.body;
                            continue 'blocks;
                        }
                        _ => break 'blocks,
                    }
                }
                _ => break 'blocks,
            }
        }
        break;
    }
    found
}

/// Whether a block does nothing but end the section (an early end, not an outcome to see).
fn only_ends(block: &Block) -> bool {
    let mut code = block
        .iter()
        .filter(|stmt| !matches!(stmt.kind, StmtKind::Comment(_)));
    let ends = code.next().is_some_and(|stmt| match &stmt.kind {
        StmtKind::Return(values) => matches!(values.as_slice(), [] | [Expr::Name(_)]),
        _ => false,
    });
    ends && code.next().is_none()
}

/// The writes that make conditions hold: a trigger on or off, a variable at a value that passes
/// its comparison; of an `or`, its first side.
pub fn needs(conditions: &[Expr]) -> Needs {
    let mut needs = Needs::default();
    for condition in conditions {
        add(condition, &mut needs);
    }
    needs.writes.sort();
    needs.writes.dedup();
    needs
}

fn add(condition: &Expr, needs: &mut Needs) {
    match condition.unparen() {
        Expr::Binary(BinOp::And, left, right) => {
            add(left, needs);
            add(right, needs);
        }
        Expr::Binary(BinOp::Or, left, _) => add(left, needs),
        Expr::Not(inner) => match inner.unparen() {
            Expr::Binary(BinOp::And, left, _) => add(&negate(left), needs),
            Expr::Binary(BinOp::Or, left, right) => {
                add(&negate(left), needs);
                add(&negate(right), needs);
            }
            other => match trigger_of(other) {
                Some(number) => needs.writes.push(Need::Trigger { number, on: false }),
                None => needs.others.push(condition.clone()),
            },
        },
        Expr::Binary(op, left, right) if op.is_comparison() => {
            match (var_of(left), right.as_number()) {
                (Some(number), Some(value)) => needs.writes.push(Need::Var {
                    number,
                    value: passing(*op, value),
                }),
                _ => needs.others.push(condition.clone()),
            }
        }
        other => match trigger_of(other) {
            Some(number) => needs.writes.push(Need::Trigger { number, on: true }),
            None => needs.others.push(condition.clone()),
        },
    }
}

fn trigger_of(expr: &Expr) -> Option<i64> {
    match expr.story_call_name()? {
        ("trigger", [number]) => number.as_number(),
        _ => None,
    }
}

fn var_of(expr: &Expr) -> Option<i64> {
    match expr.unparen().story_call_name()? {
        ("var", [number]) => number.as_number(),
        _ => None,
    }
}

/// A value that passes `value <op> limit`.
fn passing(op: BinOp, limit: i64) -> i64 {
    match op {
        BinOp::Lt => (limit - 1).max(0),
        BinOp::Gt => limit + 1,
        BinOp::Ne => {
            if limit == 0 {
                1
            } else {
                0
            }
        }
        _ => limit,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn finds_what_gets_past_the_early_ends() {
        let file = StoryFile::parse(
            "local routines = {}\n\
             function routines.at_0010()\n\
               if story.trigger(3) or story.var(2) >= 5 then\n    return ENDED\n  end\n\
               if not story.trigger(4) then\n    return\n  elseif story.var(1) == 2 then\n    return ENDED\n  end\n\
               if story.has_item(7) then\n    return ENDED\n  end\n\
               story.set_trigger(9)\n\
               if story.trigger(5) then\n    return ENDED\n  end\n\
               return ENDED\n\
             end\n\
             return { script = 1, sections = { [0x0051] = routines.at_0010 } }\n",
        )
        .unwrap();
        let block = file.section_block(0x51).unwrap();
        let conditions = conditions(&file, block);
        assert_eq!(conditions.len(), 5);
        let needs = needs(&conditions);
        assert_eq!(
            needs.writes,
            vec![
                Need::Trigger {
                    number: 3,
                    on: false
                },
                Need::Trigger {
                    number: 4,
                    on: true
                },
                Need::Trigger {
                    number: 5,
                    on: false
                },
                Need::Var {
                    number: 1,
                    value: 0
                },
                Need::Var {
                    number: 2,
                    value: 4
                },
            ]
        );
        assert_eq!(needs.others.len(), 1);
    }
}
