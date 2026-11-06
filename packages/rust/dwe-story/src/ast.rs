//! The Lua the story uses, as a tree: the emitter's subset (docs/architecture/interprete-historia.md
//! §La forma del Lua) read generically, so a hand-written section in the same forms reads too. The
//! comments stay with their statements and are written back.

/// A comparison or a logical operator.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum BinOp {
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    And,
    Or,
    Add,
    Sub,
}

impl BinOp {
    pub fn symbol(self) -> &'static str {
        match self {
            Self::Eq => "==",
            Self::Ne => "~=",
            Self::Lt => "<",
            Self::Le => "<=",
            Self::Gt => ">",
            Self::Ge => ">=",
            Self::And => "and",
            Self::Or => "or",
            Self::Add => "+",
            Self::Sub => "-",
        }
    }

    /// Lua's precedence: higher binds tighter.
    pub fn precedence(self) -> u8 {
        match self {
            Self::Or => 1,
            Self::And => 2,
            Self::Eq | Self::Ne | Self::Lt | Self::Le | Self::Gt | Self::Ge => 3,
            Self::Add | Self::Sub => 4,
        }
    }

    pub fn is_comparison(self) -> bool {
        self.precedence() == 3
    }

    /// The comparison that holds when this one does not.
    pub fn negated(self) -> Option<Self> {
        Some(match self {
            Self::Eq => Self::Ne,
            Self::Ne => Self::Eq,
            Self::Lt => Self::Ge,
            Self::Ge => Self::Lt,
            Self::Le => Self::Gt,
            Self::Gt => Self::Le,
            _ => return None,
        })
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum Expr {
    Nil,
    Bool(bool),
    Number(i64),
    Str(String),
    Name(String),
    /// `object.name`.
    Field(Box<Expr>, String),
    /// `callee(arguments)`.
    Call(Box<Expr>, Vec<Expr>),
    /// `(expr)`: kept, so the code reads back as it was written.
    Paren(Box<Expr>),
    Not(Box<Expr>),
    Binary(BinOp, Box<Expr>, Box<Expr>),
    Table(Vec<TableField>),
    Function {
        params: Vec<String>,
        body: Block,
    },
}

#[derive(Clone, Debug, PartialEq)]
pub enum TableField {
    /// `name = value`.
    Named(String, Expr),
    /// `[key] = value`.
    Keyed(Expr, Expr),
    Positional(Expr),
}

/// A statement and the comment at the end of its line.
#[derive(Clone, Debug, PartialEq)]
pub struct Stmt {
    pub kind: StmtKind,
    pub comment: Option<String>,
}

impl Stmt {
    pub fn new(kind: StmtKind) -> Self {
        Self {
            kind,
            comment: None,
        }
    }

    pub fn with_comment(kind: StmtKind, comment: impl Into<String>) -> Self {
        Self {
            kind,
            comment: Some(comment.into()),
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum StmtKind {
    /// `local name <attrib> = value`.
    Local {
        name: String,
        attrib: Option<String>,
        value: Expr,
    },
    /// `function table.name(params) ... end`.
    Function {
        table: String,
        name: String,
        params: Vec<String>,
        body: Block,
    },
    If {
        arms: Vec<(Expr, Block)>,
        otherwise: Option<Block>,
    },
    While {
        condition: Expr,
        body: Block,
    },
    Do(Block),
    Break,
    Return(Vec<Expr>),
    /// `name = value`.
    Assign {
        name: String,
        value: Expr,
    },
    /// A call as a statement.
    Call(Expr),
    /// A comment on a line of its own.
    Comment(String),
}

pub type Block = Vec<Stmt>;

impl Expr {
    pub fn name(name: &str) -> Self {
        Self::Name(name.to_owned())
    }

    pub fn field(object: Expr, name: &str) -> Self {
        Self::Field(Box::new(object), name.to_owned())
    }

    /// `story.<name>`.
    pub fn story(name: &str) -> Self {
        Self::field(Self::name("story"), name)
    }

    /// `story.<name>(arguments)`.
    pub fn story_call(name: &str, arguments: Vec<Expr>) -> Self {
        Self::Call(Box::new(Self::story(name)), arguments)
    }

    /// `routines.<name>()`.
    pub fn routine_call(name: &str) -> Self {
        Self::Call(
            Box::new(Self::field(Self::name("routines"), name)),
            Vec::new(),
        )
    }

    /// The name of the story function this calls (`story.<name>(...)`).
    pub fn story_call_name(&self) -> Option<(&str, &[Expr])> {
        match self {
            Self::Call(callee, arguments) => match callee.as_ref() {
                Self::Field(object, name) if matches!(object.as_ref(), Self::Name(n) if n == "story") => {
                    Some((name, arguments))
                }
                _ => None,
            },
            _ => None,
        }
    }

    /// The routine this calls (`routines.<name>()`).
    pub fn routine_call_name(&self) -> Option<&str> {
        match self {
            Self::Call(callee, arguments) if arguments.is_empty() => match callee.as_ref() {
                Self::Field(object, name)
                    if matches!(object.as_ref(), Self::Name(n) if n == "routines") =>
                {
                    Some(name)
                }
                _ => None,
            },
            _ => None,
        }
    }

    /// The expression without the parentheses around it.
    pub fn unparen(&self) -> &Expr {
        match self {
            Self::Paren(inner) => inner.unparen(),
            other => other,
        }
    }

    pub fn as_number(&self) -> Option<i64> {
        match self.unparen() {
            Self::Number(number) => Some(*number),
            _ => None,
        }
    }
}

/// Calls `visit` on every expression of a block, statements inside statements included.
pub fn walk_exprs<'a>(block: &'a Block, visit: &mut impl FnMut(&'a Expr)) {
    for stmt in block {
        walk_stmt_exprs(stmt, visit);
    }
}

fn walk_stmt_exprs<'a>(stmt: &'a Stmt, visit: &mut impl FnMut(&'a Expr)) {
    match &stmt.kind {
        StmtKind::Local { value, .. } | StmtKind::Assign { value, .. } => walk_expr(value, visit),
        StmtKind::Function { body, .. } | StmtKind::Do(body) => walk_exprs(body, visit),
        StmtKind::If { arms, otherwise } => {
            for (condition, body) in arms {
                walk_expr(condition, visit);
                walk_exprs(body, visit);
            }
            if let Some(body) = otherwise {
                walk_exprs(body, visit);
            }
        }
        StmtKind::While { condition, body } => {
            walk_expr(condition, visit);
            walk_exprs(body, visit);
        }
        StmtKind::Return(values) => values.iter().for_each(|value| walk_expr(value, visit)),
        StmtKind::Call(call) => walk_expr(call, visit),
        StmtKind::Break | StmtKind::Comment(_) => {}
    }
}

fn walk_expr<'a>(expr: &'a Expr, visit: &mut impl FnMut(&'a Expr)) {
    visit(expr);
    match expr {
        Expr::Field(object, _) => walk_expr(object, visit),
        Expr::Call(callee, arguments) => {
            walk_expr(callee, visit);
            arguments.iter().for_each(|argument| walk_expr(argument, visit));
        }
        Expr::Paren(inner) | Expr::Not(inner) => walk_expr(inner, visit),
        Expr::Binary(_, left, right) => {
            walk_expr(left, visit);
            walk_expr(right, visit);
        }
        Expr::Table(fields) => {
            for field in fields {
                match field {
                    TableField::Named(_, value) | TableField::Positional(value) => {
                        walk_expr(value, visit)
                    }
                    TableField::Keyed(key, value) => {
                        walk_expr(key, visit);
                        walk_expr(value, visit);
                    }
                }
            }
        }
        Expr::Function { body, .. } => walk_exprs(body, visit),
        Expr::Nil | Expr::Bool(_) | Expr::Number(_) | Expr::Str(_) | Expr::Name(_) => {}
    }
}

/// Renames the routines a block calls (`routines.<old>` to `routines.<new>`).
pub fn rename_routines(block: &mut Block, rename: &impl Fn(&str) -> Option<String>) {
    for stmt in block {
        rename_in_stmt(stmt, rename);
    }
}

fn rename_in_stmt(stmt: &mut Stmt, rename: &impl Fn(&str) -> Option<String>) {
    match &mut stmt.kind {
        StmtKind::Local { value, .. } | StmtKind::Assign { value, .. } => {
            rename_in_expr(value, rename)
        }
        StmtKind::Function { body, .. } | StmtKind::Do(body) => rename_routines(body, rename),
        StmtKind::If { arms, otherwise } => {
            for (condition, body) in arms {
                rename_in_expr(condition, rename);
                rename_routines(body, rename);
            }
            if let Some(body) = otherwise {
                rename_routines(body, rename);
            }
        }
        StmtKind::While { condition, body } => {
            rename_in_expr(condition, rename);
            rename_routines(body, rename);
        }
        StmtKind::Return(values) => values
            .iter_mut()
            .for_each(|value| rename_in_expr(value, rename)),
        StmtKind::Call(call) => rename_in_expr(call, rename),
        StmtKind::Break | StmtKind::Comment(_) => {}
    }
}

fn rename_in_expr(expr: &mut Expr, rename: &impl Fn(&str) -> Option<String>) {
    match expr {
        Expr::Field(object, name) => {
            if matches!(object.as_ref(), Expr::Name(n) if n == "routines")
                && let Some(new) = rename(name)
            {
                *name = new;
            }
            rename_in_expr(object, rename);
        }
        Expr::Call(callee, arguments) => {
            rename_in_expr(callee, rename);
            arguments
                .iter_mut()
                .for_each(|argument| rename_in_expr(argument, rename));
        }
        Expr::Paren(inner) | Expr::Not(inner) => rename_in_expr(inner, rename),
        Expr::Binary(_, left, right) => {
            rename_in_expr(left, rename);
            rename_in_expr(right, rename);
        }
        Expr::Table(fields) => {
            for field in fields {
                match field {
                    TableField::Named(_, value) | TableField::Positional(value) => {
                        rename_in_expr(value, rename)
                    }
                    TableField::Keyed(key, value) => {
                        rename_in_expr(key, rename);
                        rename_in_expr(value, rename);
                    }
                }
            }
        }
        Expr::Function { body, .. } => rename_routines(body, rename),
        Expr::Nil | Expr::Bool(_) | Expr::Number(_) | Expr::Str(_) | Expr::Name(_) => {}
    }
}
