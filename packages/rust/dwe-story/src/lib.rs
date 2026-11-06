//! dwe's story scripts for the tools. The story runs as Lua modules (`story/<n>.lua`, written by
//! `dwx extract`) that packs patch section by section (`story/patches/*.lua`,
//! docs/modding/historia.md). This crate reads that Lua into a tree ([`ast`], [`parse`]), writes it
//! back ([`write`]), applies the patches of the pack stack like the game's `StoryLuaHost`
//! ([`script`]), names the commands for people ([`commands`]), reads the dialogue texts ([`texts`])
//! and lists what can run a section on a map ([`events`]).

pub mod ast;
pub mod commands;
pub mod describe;
pub mod edit;
pub mod events;
pub mod file;
pub mod lex;
pub mod parse;
pub mod save;
pub mod script;
pub mod texts;
pub mod write;

pub use ast::{BinOp, Block, Expr, Stmt, StmtKind};
pub use events::{Event, EventKind, MapEvents};
pub use file::{Routine, Section, SectionBody, StoryFile};
pub use script::{LoadedScript, SectionOrigin, StoryFileSource};
pub use save::{ScriptDraft, StoryProblem, StorySavePlan};
pub use texts::StoryTexts;

/// What can go wrong reading story Lua.
#[derive(Debug, thiserror::Error)]
pub enum Error {
    /// Code that does not read, with its line.
    #[error("line {line}: {message}")]
    Syntax { line: usize, message: String },
    #[error("{0}")]
    Invalid(String),
    #[error(transparent)]
    Pack(#[from] dwe_pack::Error),
}

impl Error {
    pub fn at(line: usize, message: impl Into<String>) -> Self {
        Self::Syntax {
            line,
            message: message.into(),
        }
    }
}

pub type Result<T> = std::result::Result<T, Error>;
