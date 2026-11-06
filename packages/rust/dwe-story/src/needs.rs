//! What the story must hold for a section to get past its early ends (the conditions of the `if`s
//! at its top whose arms do nothing but end the section: `if story.trigger(12) then return ENDED
//! end`) and to reach a chosen statement (the conditions of the `if` arms on the way to it), and
//! the trigger and variable writes that make them hold, for an editor that runs the section in a
//! game to see it.

use crate::ast::{BinOp, Block, Expr, Stmt, StmtKind};
use crate::edit::StmtPath;
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

/// The conditions on the way to a statement: for each `if` the path goes into, the condition of
/// the arm it takes and the negation of the arms before it (all of them for the `else`).
pub fn path_conditions(block: &Block, path: &StmtPath) -> Vec<Expr> {
    let mut found = Vec::new();
    let mut block = block;
    for &(stmt, child) in &path.blocks {
        let Some(stmt) = block.get(stmt) else {
            break;
        };
        if let StmtKind::If { arms, .. } = &stmt.kind {
            found.extend(
                arms.iter()
                    .take(child)
                    .map(|(condition, _)| negate(condition)),
            );
            if let Some((condition, _)) = arms.get(child) {
                found.push(condition.clone());
            }
        }
        match crate::edit::children(stmt).get(child) {
            Some(inner) => block = inner,
            None => break,
        }
    }
    found
}

/// The conditions under which a block reaches the first statement `wanted` picks (an NPC's
/// `spawn_npc` in a map's entry section): the early ends passed on the way, the `if` arms taken
/// and the routines called (`routines.x()`), or None when it never gets there.
pub fn conditions_to(
    file: &StoryFile,
    block: &Block,
    wanted: &dyn Fn(&Stmt) -> bool,
) -> Option<Vec<Expr>> {
    let mut seen = Vec::new();
    reach(file, block, wanted, &mut seen)
}

fn reach(
    file: &StoryFile,
    block: &Block,
    wanted: &dyn Fn(&Stmt) -> bool,
    seen: &mut Vec<String>,
) -> Option<Vec<Expr>> {
    let mut passed = Vec::new();
    for stmt in block {
        if wanted(stmt) {
            return Some(passed);
        }
        let inner = match &stmt.kind {
            StmtKind::If { arms, otherwise } => {
                let blocks = arms.iter().map(|(_, body)| body).chain(otherwise.iter());
                blocks.enumerate().find_map(|(taken, body)| {
                    let mut found: Vec<Expr> = arms
                        .iter()
                        .take(taken)
                        .map(|(condition, _)| negate(condition))
                        .collect();
                    found.extend(arms.get(taken).map(|(condition, _)| condition.clone()));
                    found.extend(reach(file, body, wanted, seen)?);
                    Some(found)
                })
            }
            StmtKind::While { body, .. } | StmtKind::Do(body) => reach(file, body, wanted, seen),
            StmtKind::Return(values) => values
                .first()
                .and_then(|value| routine_reach(file, value, wanted, seen)),
            StmtKind::Call(call) => routine_reach(file, call, wanted, seen),
            _ => None,
        };
        if let Some(inner) = inner {
            passed.extend(inner);
            return Some(passed);
        }
        if let StmtKind::If {
            arms,
            otherwise: None,
        } = &stmt.kind
            && arms.iter().all(|(_, body)| only_ends(body))
        {
            passed.extend(arms.iter().map(|(condition, _)| negate(condition)));
        }
        if matches!(stmt.kind, StmtKind::Return(_)) {
            return None;
        }
    }
    None
}

fn routine_reach(
    file: &StoryFile,
    call: &Expr,
    wanted: &dyn Fn(&Stmt) -> bool,
    seen: &mut Vec<String>,
) -> Option<Vec<Expr>> {
    let routine = file.routine(call.routine_call_name()?)?;
    if seen.contains(&routine.name) {
        return None;
    }
    seen.push(routine.name.clone());
    reach(file, &routine.body, wanted, seen)
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

    #[test]
    fn finds_what_reaches_a_statement() {
        let file = StoryFile::parse(
            "return { script = 1, sections = { [0x0051] = function(original)\n\
               if story.trigger(3) then\n    story.set_var(1, 1)\n\
               elseif story.var(2) == 4 then\n    story.set_var(1, 2)\n\
               else\n    while true do\n      story.set_var(1, 3)\n    end\n  end\n\
             end } }\n",
        )
        .unwrap();
        let block = file.section_block(0x51).unwrap();
        let second = needs(&path_conditions(block, &StmtPath::top(0).child(1, 0)));
        assert_eq!(
            second.writes,
            vec![
                Need::Trigger {
                    number: 3,
                    on: false
                },
                Need::Var {
                    number: 2,
                    value: 4
                },
            ]
        );
        let path = StmtPath::top(0).child(2, 0).child(0, 0);
        let otherwise = path_conditions(block, &path);
        assert_eq!(otherwise.len(), 2);
        assert!(path_conditions(block, &StmtPath::top(0)).is_empty());
    }

    #[test]
    fn finds_what_reaches_a_spawn() {
        let file = StoryFile::parse(
            "local routines = {}\n\
             function routines.at_0010()\n\
               if story.trigger(1) then\n    return ENDED\n  end\n\
               if story.trigger(2) then\n    story.spawn_npc(5, 0, 0)\n    return ENDED\n\
               else\n    if not story.trigger(3) then\n      story.spawn_npc(5, 1, 0)\n\
               else\n      return routines.at_0020()\n    end\n  end\n\
             end\n\
             function routines.at_0020()\n\
               story.spawn_npc(6, 2, 0)\n\
             end\n\
             return { script = 0, sections = { [0x0000] = routines.at_0010 } }\n",
        )
        .unwrap();
        let block = file.section_block(0).unwrap();
        let spawns = |slot: i64| {
            move |stmt: &Stmt| match &stmt.kind {
                StmtKind::Call(call) => matches!(
                    call.story_call_name(),
                    Some(("spawn_npc", [_, number, _])) if number.as_number() == Some(slot)
                ),
                _ => false,
            }
        };
        let writes = |slot| needs(&conditions_to(&file, block, &spawns(slot)).unwrap()).writes;
        let trigger = |number, on| Need::Trigger { number, on };
        assert_eq!(writes(0), vec![trigger(1, false), trigger(2, true)]);
        assert_eq!(
            writes(2),
            vec![trigger(1, false), trigger(2, false), trigger(3, true)]
        );
        assert!(conditions_to(&file, block, &spawns(7)).is_none());
    }
}
