//! A story file: a module (`story/<n>.lua`, as `dwx` writes it) or a story patch
//! (`story/patches/<name>.lua`, docs/modding/historia.md#parches-por-sección). Both are a few locals,
//! an optional `routines` table with its functions and a returned
//! `{ script = <n>, sections = { [<id>] = ... } }`; a module's sections name its routines, a patch's
//! are functions that get the original section.

use std::collections::{BTreeMap, BTreeSet};

use crate::ast::{Block, Expr, Stmt, StmtKind, TableField, rename_routines, walk_exprs};
use crate::parse::parse_chunk;
use crate::write::write_block;
use crate::{Error, Result};

/// The table a file's shared functions live in.
pub const ROUTINES: &str = "routines";

/// A function of the `routines` table.
#[derive(Clone, Debug, PartialEq)]
pub struct Routine {
    pub name: String,
    /// The comment lines just above it (`-- section 0x0051`).
    pub leading: Vec<String>,
    pub body: Block,
}

/// What a section runs.
#[derive(Clone, Debug, PartialEq)]
pub enum SectionBody {
    /// A routine of the file (a module: `[0x0051] = routines.at_0040`).
    Routine(String),
    /// A function of its own (a patch: `[0x0051] = function(original) ... end`).
    Function { params: Vec<String>, body: Block },
}

#[derive(Clone, Debug, PartialEq)]
pub struct Section {
    pub id: u16,
    pub body: SectionBody,
}

/// A module or a patch.
#[derive(Clone, Debug, PartialEq)]
pub struct StoryFile {
    /// Header comments and the locals at the top.
    pub preamble: Block,
    /// Whether it has `local routines = {}` (a patch without private routines has not).
    pub has_routines: bool,
    pub routines: Vec<Routine>,
    pub script: u16,
    pub sections: Vec<Section>,
}

/// The locals every generated module and every written patch starts with.
pub const STORY_LOCALS: [(&str, &str); 4] = [
    ("story", "story"),
    ("ENDED", "story.ENDED"),
    ("PLAYER", "story.PLAYER"),
    ("PARTNER", "story.PARTNER"),
];

impl StoryFile {
    /// Reads a module or a patch.
    pub fn parse(code: &str) -> Result<Self> {
        let chunk = parse_chunk(code)?;
        let mut file = Self {
            preamble: Block::new(),
            has_routines: false,
            routines: Vec::new(),
            script: 0,
            sections: Vec::new(),
        };
        let mut leading = Vec::new();
        let mut returned = false;
        for stmt in chunk {
            if returned {
                if matches!(stmt.kind, StmtKind::Comment(_)) {
                    continue;
                }
                return Err(Error::Invalid("code after the returned table".to_owned()));
            }
            match stmt.kind {
                StmtKind::Comment(text) if file.has_routines => leading.push(text),
                StmtKind::Local {
                    ref name,
                    value: Expr::Table(ref fields),
                    ..
                } if name == ROUTINES && fields.is_empty() => {
                    if file.has_routines {
                        return Err(Error::Invalid("two `local routines = {}`".to_owned()));
                    }
                    file.has_routines = true;
                    // Comments between the locals and the table were the preamble's.
                }
                StmtKind::Function {
                    table,
                    name,
                    params,
                    body,
                } => {
                    if table != ROUTINES || !file.has_routines {
                        return Err(Error::Invalid(format!(
                            "function {table}.{name} is not in a `local routines = {{}}` table"
                        )));
                    }
                    if !params.is_empty() {
                        return Err(Error::Invalid(format!("routine {name} takes parameters")));
                    }
                    if file.routine(&name).is_some() {
                        return Err(Error::Invalid(format!("routine {name} is there twice")));
                    }
                    file.routines.push(Routine {
                        name,
                        leading: std::mem::take(&mut leading),
                        body,
                    });
                }
                StmtKind::Return(values) => {
                    let [Expr::Table(fields)] = values.as_slice() else {
                        return Err(Error::Invalid(
                            "the file does not return { script = <id>, sections = { ... } }"
                                .to_owned(),
                        ));
                    };
                    file.read_returned(fields)?;
                    returned = true;
                }
                kind if !file.has_routines && file.routines.is_empty() => {
                    file.preamble.push(Stmt {
                        kind,
                        comment: stmt.comment,
                    })
                }
                _ => {
                    return Err(Error::Invalid(
                        "a statement between the routines that is not a routine".to_owned(),
                    ));
                }
            }
        }
        if !returned {
            return Err(Error::Invalid(
                "the file does not return { script = <id>, sections = { ... } }".to_owned(),
            ));
        }
        Ok(file)
    }

    fn read_returned(&mut self, fields: &[TableField]) -> Result<()> {
        let mut script = None;
        let mut sections = None;
        for field in fields {
            match field {
                TableField::Named(name, value) if name == "script" => {
                    script = value
                        .as_number()
                        .and_then(|number| u16::try_from(number).ok());
                    if script.is_none() {
                        return Err(Error::Invalid(
                            "a script that is not a number from 0 to 65535".to_owned(),
                        ));
                    }
                }
                TableField::Named(name, Expr::Table(entries)) if name == "sections" => {
                    sections = Some(entries)
                }
                _ => {
                    return Err(Error::Invalid(
                        "the returned table has only script and sections".to_owned(),
                    ));
                }
            }
        }
        let (Some(script), Some(entries)) = (script, sections) else {
            return Err(Error::Invalid(
                "the file does not return { script = <id>, sections = { ... } }".to_owned(),
            ));
        };
        self.script = script;
        for entry in entries {
            let TableField::Keyed(key, value) = entry else {
                return Err(Error::Invalid(
                    "a section that is not [<id>] = ...".to_owned(),
                ));
            };
            let Some(id) = key.as_number().and_then(|n| u16::try_from(n).ok()) else {
                return Err(Error::Invalid(
                    "a section that is not a number from 0 to 65535".to_owned(),
                ));
            };
            if self.section(id).is_some() {
                return Err(Error::Invalid(format!("section {id:#06x} is there twice")));
            }
            let body = match value {
                Expr::Field(object, name) if matches!(object.as_ref(), Expr::Name(n) if n == ROUTINES) =>
                {
                    if self.routine(name).is_none() {
                        return Err(Error::Invalid(format!(
                            "section {id:#06x} names routine {name}, which is not there"
                        )));
                    }
                    SectionBody::Routine(name.clone())
                }
                Expr::Function { params, body } => SectionBody::Function {
                    params: params.clone(),
                    body: body.clone(),
                },
                _ => {
                    return Err(Error::Invalid(format!(
                        "section {id:#06x} is neither routines.<name> nor function(original) ... end"
                    )));
                }
            };
            self.sections.push(Section { id, body });
        }
        Ok(())
    }

    /// An empty patch of a script, with a header comment.
    pub fn new_patch(script: u16, header: &str) -> Self {
        let mut preamble = vec![Stmt::new(StmtKind::Comment(header.to_owned()))];
        for (name, value) in STORY_LOCALS {
            let value = match value.split_once('.') {
                Some((object, field)) => Expr::field(Expr::name(object), field),
                None => Expr::name(value),
            };
            preamble.push(Stmt::new(StmtKind::Local {
                name: name.to_owned(),
                attrib: Some("const".to_owned()),
                value,
            }));
        }
        Self {
            preamble,
            has_routines: false,
            routines: Vec::new(),
            script,
            sections: Vec::new(),
        }
    }

    pub fn routine(&self, name: &str) -> Option<&Routine> {
        self.routines.iter().find(|routine| routine.name == name)
    }

    pub fn routine_mut(&mut self, name: &str) -> Option<&mut Routine> {
        self.routines
            .iter_mut()
            .find(|routine| routine.name == name)
    }

    pub fn section(&self, id: u16) -> Option<&Section> {
        self.sections.iter().find(|section| section.id == id)
    }

    /// The code a section runs first: its routine's body or its function's.
    pub fn section_block(&self, id: u16) -> Option<&Block> {
        match &self.section(id)?.body {
            SectionBody::Routine(name) => self.routine(name).map(|routine| &routine.body),
            SectionBody::Function { body, .. } => Some(body),
        }
    }

    /// The routines a section reaches, in the order they are first called, transitively.
    pub fn reached_routines(&self, id: u16) -> Vec<String> {
        let Some(block) = self.section_block(id) else {
            return Vec::new();
        };
        let mut reached = Vec::new();
        let mut seen = BTreeSet::new();
        if let SectionBody::Routine(name) = &self.section(id).expect("found").body {
            seen.insert(name.clone());
        }
        let mut pending = vec![block];
        while let Some(block) = pending.pop() {
            let mut called = Vec::new();
            walk_exprs(block, &mut |expr| {
                if let Expr::Field(object, name) = expr
                    && matches!(object.as_ref(), Expr::Name(n) if n == ROUTINES)
                {
                    called.push(name.clone());
                }
            });
            for name in called {
                if seen.insert(name.clone())
                    && let Some(routine) = self.routine(&name)
                {
                    reached.push(name);
                    pending.push(&routine.body);
                }
            }
        }
        reached
    }

    /// The sections whose code reaches a routine.
    pub fn sections_reaching(&self, routine: &str) -> Vec<u16> {
        self.sections
            .iter()
            .filter(|section| {
                matches!(&section.body, SectionBody::Routine(name) if name == routine)
                    || self
                        .reached_routines(section.id)
                        .iter()
                        .any(|n| n == routine)
            })
            .map(|section| section.id)
            .collect()
    }

    /// Drops the routines no section reaches.
    pub fn drop_unreached_routines(&mut self) {
        let mut used = BTreeSet::new();
        for section in &self.sections {
            if let SectionBody::Routine(name) = &section.body {
                used.insert(name.clone());
            }
            used.extend(self.reached_routines(section.id));
        }
        self.routines.retain(|routine| used.contains(&routine.name));
    }

    /// Writes the file in the emitter's layout.
    pub fn write(&self) -> String {
        let mut out = String::new();
        write_block(&self.preamble, 0, &mut out);
        if self.has_routines || !self.routines.is_empty() {
            out.push_str("\nlocal routines = {}\n");
            for routine in &self.routines {
                out.push('\n');
                for comment in &routine.leading {
                    out.push_str(&format!("-- {comment}\n"));
                }
                out.push_str(&format!("function routines.{}()\n", routine.name));
                write_block(&routine.body, 1, &mut out);
                out.push_str("end\n");
            }
        }
        out.push_str(&format!(
            "\nreturn {{\n  script = {},\n  sections = {{\n",
            self.script
        ));
        for section in &self.sections {
            out.push_str(&format!("    [{:#06x}] = ", section.id));
            match &section.body {
                SectionBody::Routine(name) => out.push_str(&format!("routines.{name},\n")),
                SectionBody::Function { params, body } => {
                    out.push_str(&format!("function({})\n", params.join(", ")));
                    write_block(body, 3, &mut out);
                    out.push_str("    end,\n");
                }
            }
        }
        out.push_str("  },\n}\n");
        out
    }

    /// Checks what the game and `dwx` need of a patch: every `routines.<name>` it uses is one of
    /// its own routines (the module's are out of its reach), `original` only as `original()`.
    pub fn check_patch(&self) -> Result<()> {
        let own: BTreeSet<&str> = self.routines.iter().map(|r| r.name.as_str()).collect();
        let mut missing = BTreeMap::new();
        let mut check = |block: &Block, place: String| {
            walk_exprs(block, &mut |expr| {
                if let Expr::Field(object, name) = expr
                    && matches!(object.as_ref(), Expr::Name(n) if n == ROUTINES)
                    && !own.contains(name.as_str())
                {
                    missing.entry(name.clone()).or_insert_with(|| place.clone());
                }
            });
        };
        for routine in &self.routines {
            check(&routine.body, format!("routine {}", routine.name));
        }
        for section in &self.sections {
            match &section.body {
                SectionBody::Function { body, .. } => {
                    check(body, format!("section {:#06x}", section.id))
                }
                SectionBody::Routine(_) => {
                    return Err(Error::Invalid(format!(
                        "section {:#06x} of a patch must be a function",
                        section.id
                    )));
                }
            }
        }
        match missing.into_iter().next() {
            Some((name, place)) => Err(Error::Invalid(format!(
                "{place} calls routines.{name}, which the patch does not have"
            ))),
            None => Ok(()),
        }
    }

    /// Copies a section of another file (the module, or a patch below) into this patch as a
    /// function, with private copies of the routines it reaches. A routine this patch already
    /// has with the same code is shared; one with other code is copied under a new name.
    pub fn take_section(&mut self, from: &StoryFile, id: u16) -> Result<()> {
        let Some(block) = from.section_block(id) else {
            return Err(Error::Invalid(format!(
                "script {} has no section {id:#06x}",
                from.script
            )));
        };
        let mut renames = BTreeMap::new();
        for name in from.reached_routines(id) {
            let routine = from.routine(&name).expect("reached routines exist");
            let target = match self.routine(&name) {
                None => name.clone(),
                Some(own) if own.body == routine.body => continue,
                Some(_) => self.free_routine_name(&name),
            };
            if target != name {
                renames.insert(name.clone(), target.clone());
            }
            self.routines.push(Routine {
                name: target,
                leading: Vec::new(),
                body: routine.body.clone(),
            });
            self.has_routines = true;
        }
        let rename = |name: &str| renames.get(name).cloned();
        let copied: Vec<String> = from
            .reached_routines(id)
            .into_iter()
            .map(|name| renames.get(&name).cloned().unwrap_or(name))
            .collect();
        for routine in &mut self.routines {
            if copied.contains(&routine.name) {
                rename_routines(&mut routine.body, &rename);
            }
        }
        let mut body = block.clone();
        rename_routines(&mut body, &rename);
        let params = match &from.section(id).expect("found").body {
            SectionBody::Function { params, .. } => params.clone(),
            SectionBody::Routine(_) => vec!["original".to_owned()],
        };
        let section = Section {
            id,
            body: SectionBody::Function { params, body },
        };
        match self.sections.iter_mut().find(|s| s.id == id) {
            Some(known) => *known = section,
            None => self.sections.push(section),
        }
        self.sort_routines();
        Ok(())
    }

    /// A routine name this file does not use yet, from a wanted one.
    pub fn free_routine_name(&self, wanted: &str) -> String {
        (2..)
            .map(|n| format!("{wanted}_{n}"))
            .find(|name| self.routine(name).is_none())
            .expect("a free name")
    }

    /// Keeps the routines in name order, as the emitter's offsets order them.
    fn sort_routines(&mut self) {
        self.routines.sort_by(|a, b| a.name.cmp(&b.name));
    }

    /// The body of a patch's section, to edit.
    pub fn section_body_mut(&mut self, id: u16) -> Option<&mut Block> {
        let index = self.sections.iter().position(|s| s.id == id)?;
        match &self.sections[index].body {
            SectionBody::Routine(name) => {
                let name = name.clone();
                self.routine_mut(&name).map(|routine| &mut routine.body)
            }
            SectionBody::Function { .. } => match &mut self.sections[index].body {
                SectionBody::Function { body, .. } => Some(body),
                SectionBody::Routine(_) => None,
            },
        }
    }

    /// Removes a section from a patch, and the routines only it reached.
    pub fn remove_section(&mut self, id: u16) {
        self.sections.retain(|section| section.id != id);
        self.drop_unreached_routines();
        if self.routines.is_empty() {
            self.has_routines = false;
        }
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    /// The synthetic module of packages/session/tests/data/written_story.
    pub(crate) const MODULE: &str =
        include_str!("../../../../packages/session/tests/data/written_story/story/900.lua");

    #[test]
    fn reads_a_module() {
        let file = StoryFile::parse(MODULE).unwrap();
        assert_eq!(file.script, 900);
        assert_eq!(file.routines.len(), 3);
        assert_eq!(file.routines[0].leading, vec!["section 0x0051".to_owned()]);
        assert_eq!(file.sections.len(), 2);
        assert_eq!(file.reached_routines(0x51), vec!["at_0100".to_owned()]);
        assert_eq!(file.sections_reaching("at_0100"), vec![0x51, 0x52]);
        assert_eq!(file.write(), MODULE);
    }

    #[test]
    fn takes_a_section_with_its_routines() {
        let module = StoryFile::parse(MODULE).unwrap();
        let mut patch = StoryFile::new_patch(900, "test");
        patch.take_section(&module, 0x51).unwrap();
        assert!(patch.check_patch().is_ok());
        assert_eq!(patch.routines.len(), 1);
        // A routine with other code under the same name is copied under a new one.
        patch.routine_mut("at_0100").unwrap().body.clear();
        patch.take_section(&module, 0x52).unwrap();
        assert_eq!(patch.routines.len(), 2);
        assert_eq!(patch.routines[1].name, "at_0100_2");
        let text = patch.write();
        assert!(text.contains("return routines.at_0100_2()"), "{text}");
        assert_eq!(StoryFile::parse(&text).unwrap(), patch);
        patch.remove_section(0x52);
        assert_eq!(patch.routines.len(), 1);
    }

    #[test]
    fn a_patch_cannot_reach_the_module() {
        let patch = StoryFile::parse(
            "return { script = 1, sections = { [1] = function() return routines.at_0010() end } }",
        )
        .unwrap();
        assert_eq!(
            patch.check_patch().unwrap_err().to_string(),
            "section 0x0001 calls routines.at_0010, which the patch does not have"
        );
    }
}
