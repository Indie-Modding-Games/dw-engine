//! Reads story Lua into the tree of [`crate::ast`]. It reads the forms the extractor's emitter
//! writes and story patches use (locals, `function routines.<name>()`, `if`/`elseif`/`else`,
//! `while`, `do`, `break`, `return`, assignments, calls, tables and anonymous functions) and fails on
//! anything else with the line, like the engine's reader.

use crate::ast::{BinOp, Block, Expr, Stmt, StmtKind, TableField};
use crate::lex::{Comment, Token, TokenKind, tokenize};
use crate::{Error, Result};

/// Parses a whole chunk.
pub fn parse_chunk(code: &str) -> Result<Block> {
    let (tokens, comments) = tokenize(code)?;
    let mut parser = Parser {
        tokens,
        comments,
        at: 0,
        next_comment: 0,
        last_line: 0,
    };
    let block = parser.block()?;
    if !parser.is_end() {
        return Err(parser.unexpected("the end of the code"));
    }
    Ok(block)
}

/// Parses the statements of a block on their own (a section's body in the raw editor).
pub fn parse_block(code: &str) -> Result<Block> {
    parse_chunk(code)
}

/// Parses one expression on its own (a condition or an operand typed in the editor).
pub fn parse_expr(code: &str) -> Result<Expr> {
    let block = parse_chunk(&format!("return {code}"))?;
    match block.as_slice() {
        [
            Stmt {
                kind: StmtKind::Return(values),
                ..
            },
        ] if values.len() == 1 => Ok(values[0].clone()),
        _ => Err(Error::Invalid("not one expression".to_owned())),
    }
}

struct Parser {
    tokens: Vec<Token>,
    comments: Vec<Comment>,
    at: usize,
    next_comment: usize,
    /// The line of the last token taken.
    last_line: usize,
}

impl Parser {
    fn peek(&self) -> &Token {
        &self.tokens[self.at.min(self.tokens.len() - 1)]
    }

    fn peek_at(&self, ahead: usize) -> &Token {
        &self.tokens[(self.at + ahead).min(self.tokens.len() - 1)]
    }

    fn next(&mut self) -> Token {
        let token = self.peek().clone();
        if self.at < self.tokens.len() - 1 {
            self.at += 1;
        }
        self.last_line = token.line;
        token
    }

    fn is_end(&self) -> bool {
        self.peek().kind == TokenKind::End
    }

    fn is_name(&self, name: &str) -> bool {
        matches!(&self.peek().kind, TokenKind::Name(n) if n == name)
    }

    fn is_symbol(&self, symbol: &str) -> bool {
        matches!(&self.peek().kind, TokenKind::Symbol(s) if *s == symbol)
    }

    fn accept_name(&mut self, name: &str) -> bool {
        let found = self.is_name(name);
        if found {
            self.next();
        }
        found
    }

    fn accept_symbol(&mut self, symbol: &str) -> bool {
        let found = self.is_symbol(symbol);
        if found {
            self.next();
        }
        found
    }

    fn expect_name(&mut self, name: &str) -> Result<()> {
        if self.accept_name(name) {
            Ok(())
        } else {
            Err(self.unexpected(&format!("'{name}'")))
        }
    }

    fn expect_symbol(&mut self, symbol: &str) -> Result<()> {
        if self.accept_symbol(symbol) {
            Ok(())
        } else {
            Err(self.unexpected(&format!("'{symbol}'")))
        }
    }

    fn identifier(&mut self) -> Result<String> {
        match &self.peek().kind {
            TokenKind::Name(name) if !is_keyword(name) => {
                let name = name.clone();
                self.next();
                Ok(name)
            }
            _ => Err(self.unexpected("a name")),
        }
    }

    fn unexpected(&self, wanted: &str) -> Error {
        let token = self.peek();
        let found = match &token.kind {
            TokenKind::Name(name) => format!("'{name}'"),
            TokenKind::Number(number) => format!("'{number}'"),
            TokenKind::Str(_) => "a string".to_owned(),
            TokenKind::Symbol(symbol) => format!("'{symbol}'"),
            TokenKind::End => "the end of the code".to_owned(),
        };
        Error::at(token.line, format!("{found} where {wanted} goes"))
    }

    /// The comments on lines of their own before the next token, as statements.
    fn own_line_comments(&mut self, block: &mut Block) {
        let line = self.peek().line;
        let ends = self.is_end();
        while let Some(comment) = self.comments.get(self.next_comment) {
            if comment.line >= line && !ends {
                break;
            }
            // A comment after code that no statement took (after `then`, say) lands here too, so
            // it is not lost.
            block.push(Stmt {
                line: comment.line,
                ..Stmt::new(StmtKind::Comment(comment.text.clone()))
            });
            self.next_comment += 1;
        }
    }

    /// The comment at the end of the line of the statement just read, when no code follows it on
    /// that line.
    fn trailing_comment(&mut self) -> Option<String> {
        let line = self.last_line;
        let comment = self.comments.get(self.next_comment)?;
        if comment.line != line || (self.peek().line == line && !self.is_end()) {
            return None;
        }
        self.next_comment += 1;
        Some(comment.text.clone())
    }

    fn block_ends(&self) -> bool {
        self.is_end() || self.is_name("end") || self.is_name("else") || self.is_name("elseif")
    }

    fn block(&mut self) -> Result<Block> {
        let mut block = Block::new();
        loop {
            self.own_line_comments(&mut block);
            if self.block_ends() {
                // Comments before the closing keyword on their own lines belong here.
                break;
            }
            let line = self.peek().line;
            let kind = self.statement()?;
            let comment = self.trailing_comment();
            block.push(Stmt {
                kind,
                comment,
                line,
            });
            self.accept_symbol(";");
        }
        Ok(block)
    }

    fn statement(&mut self) -> Result<StmtKind> {
        if self.accept_name("local") {
            let name = self.identifier()?;
            let attrib = if self.accept_symbol("<") {
                let attrib = self.identifier()?;
                self.expect_symbol(">")?;
                Some(attrib)
            } else {
                None
            };
            self.expect_symbol("=")?;
            let value = self.expr()?;
            return Ok(StmtKind::Local {
                name,
                attrib,
                value,
            });
        }
        if self.accept_name("function") {
            let table = self.identifier()?;
            self.expect_symbol(".")?;
            let name = self.identifier()?;
            let params = self.params()?;
            let body = self.block()?;
            self.expect_name("end")?;
            return Ok(StmtKind::Function {
                table,
                name,
                params,
                body,
            });
        }
        if self.accept_name("if") {
            let mut arms = Vec::new();
            let condition = self.expr()?;
            self.expect_name("then")?;
            arms.push((condition, self.block()?));
            let mut otherwise = None;
            loop {
                if self.accept_name("elseif") {
                    let condition = self.expr()?;
                    self.expect_name("then")?;
                    arms.push((condition, self.block()?));
                } else if self.accept_name("else") {
                    otherwise = Some(self.block()?);
                    self.expect_name("end")?;
                    break;
                } else {
                    self.expect_name("end")?;
                    break;
                }
            }
            return Ok(StmtKind::If { arms, otherwise });
        }
        if self.accept_name("while") {
            let condition = self.expr()?;
            self.expect_name("do")?;
            let body = self.block()?;
            self.expect_name("end")?;
            return Ok(StmtKind::While { condition, body });
        }
        if self.accept_name("do") {
            let body = self.block()?;
            self.expect_name("end")?;
            return Ok(StmtKind::Do(body));
        }
        if self.accept_name("break") {
            return Ok(StmtKind::Break);
        }
        if self.accept_name("return") {
            let mut values = Vec::new();
            if !self.block_ends() && !self.is_symbol(";") {
                values.push(self.expr()?);
                while self.accept_symbol(",") {
                    values.push(self.expr()?);
                }
            }
            return Ok(StmtKind::Return(values));
        }
        if matches!(&self.peek().kind, TokenKind::Name(name) if !is_keyword(name))
            && matches!(self.peek_at(1).kind, TokenKind::Symbol("="))
        {
            let name = self.identifier()?;
            self.next();
            let value = self.expr()?;
            return Ok(StmtKind::Assign { name, value });
        }
        let line = self.peek().line;
        let expr = self.suffixed()?;
        if !matches!(expr, Expr::Call(..)) {
            return Err(Error::at(line, "a statement that is not a call"));
        }
        Ok(StmtKind::Call(expr))
    }

    fn params(&mut self) -> Result<Vec<String>> {
        self.expect_symbol("(")?;
        let mut params = Vec::new();
        if !self.is_symbol(")") {
            params.push(self.identifier()?);
            while self.accept_symbol(",") {
                params.push(self.identifier()?);
            }
        }
        self.expect_symbol(")")?;
        Ok(params)
    }

    fn expr(&mut self) -> Result<Expr> {
        self.binary(1)
    }

    fn binary(&mut self, lowest: u8) -> Result<Expr> {
        let mut left = self.unary()?;
        while let Some(op) = self.binop() {
            if op.precedence() < lowest {
                break;
            }
            self.next();
            let right = self.binary(op.precedence() + 1)?;
            left = Expr::Binary(op, Box::new(left), Box::new(right));
        }
        Ok(left)
    }

    fn binop(&self) -> Option<BinOp> {
        Some(match &self.peek().kind {
            TokenKind::Symbol("==") => BinOp::Eq,
            TokenKind::Symbol("~=") => BinOp::Ne,
            TokenKind::Symbol("<") => BinOp::Lt,
            TokenKind::Symbol("<=") => BinOp::Le,
            TokenKind::Symbol(">") => BinOp::Gt,
            TokenKind::Symbol(">=") => BinOp::Ge,
            TokenKind::Symbol("+") => BinOp::Add,
            TokenKind::Symbol("-") => BinOp::Sub,
            TokenKind::Name(name) if name == "and" => BinOp::And,
            TokenKind::Name(name) if name == "or" => BinOp::Or,
            _ => return None,
        })
    }

    fn unary(&mut self) -> Result<Expr> {
        if self.accept_name("not") {
            return Ok(Expr::Not(Box::new(self.unary()?)));
        }
        if self.is_symbol("-") {
            if let TokenKind::Number(number) = self.peek_at(1).kind {
                self.next();
                self.next();
                return Ok(Expr::Number(-number));
            }
            return Err(Error::at(
                self.peek().line,
                "a minus that is not before a number",
            ));
        }
        self.suffixed()
    }

    fn suffixed(&mut self) -> Result<Expr> {
        let mut expr = self.primary()?;
        loop {
            if self.accept_symbol(".") {
                let name = self.identifier()?;
                expr = Expr::Field(Box::new(expr), name);
            } else if self.is_symbol("(") {
                self.next();
                let mut arguments = Vec::new();
                if !self.is_symbol(")") {
                    arguments.push(self.expr()?);
                    while self.accept_symbol(",") {
                        arguments.push(self.expr()?);
                    }
                }
                self.expect_symbol(")")?;
                expr = Expr::Call(Box::new(expr), arguments);
            } else {
                return Ok(expr);
            }
        }
    }

    fn primary(&mut self) -> Result<Expr> {
        let token = self.peek().clone();
        match token.kind {
            TokenKind::Number(number) => {
                self.next();
                Ok(Expr::Number(number))
            }
            TokenKind::Str(text) => {
                self.next();
                Ok(Expr::Str(text))
            }
            TokenKind::Name(name) => match name.as_str() {
                "nil" => {
                    self.next();
                    Ok(Expr::Nil)
                }
                "true" | "false" => {
                    self.next();
                    Ok(Expr::Bool(name == "true"))
                }
                "function" => {
                    self.next();
                    let params = self.params()?;
                    let body = self.block()?;
                    self.expect_name("end")?;
                    Ok(Expr::Function { params, body })
                }
                _ if is_keyword(&name) => Err(self.unexpected("a value")),
                _ => {
                    self.next();
                    Ok(Expr::Name(name))
                }
            },
            TokenKind::Symbol("(") => {
                self.next();
                let inner = self.expr()?;
                self.expect_symbol(")")?;
                Ok(Expr::Paren(Box::new(inner)))
            }
            TokenKind::Symbol("{") => {
                self.next();
                self.table()
            }
            _ => Err(self.unexpected("a value")),
        }
    }

    fn table(&mut self) -> Result<Expr> {
        let mut fields = Vec::new();
        while !self.is_symbol("}") {
            if self.accept_symbol("[") {
                let key = self.expr()?;
                self.expect_symbol("]")?;
                self.expect_symbol("=")?;
                fields.push(TableField::Keyed(key, self.expr()?));
            } else if matches!(&self.peek().kind, TokenKind::Name(name) if !is_keyword(name))
                && matches!(self.peek_at(1).kind, TokenKind::Symbol("="))
            {
                let name = self.identifier()?;
                self.next();
                fields.push(TableField::Named(name, self.expr()?));
            } else {
                fields.push(TableField::Positional(self.expr()?));
            }
            // Comments inside a table constructor (the generated sections table has none) are
            // skipped.
            if !self.accept_symbol(",") && !self.accept_symbol(";") {
                break;
            }
            self.skip_comments_before_next();
        }
        self.expect_symbol("}")?;
        Ok(Expr::Table(fields))
    }

    fn skip_comments_before_next(&mut self) {
        let line = self.peek().line;
        while self
            .comments
            .get(self.next_comment)
            .is_some_and(|comment| comment.line < line)
        {
            self.next_comment += 1;
        }
    }
}

fn is_keyword(name: &str) -> bool {
    matches!(
        name,
        "and"
            | "break"
            | "do"
            | "else"
            | "elseif"
            | "end"
            | "false"
            | "for"
            | "function"
            | "goto"
            | "if"
            | "in"
            | "local"
            | "nil"
            | "not"
            | "or"
            | "repeat"
            | "return"
            | "then"
            | "true"
            | "until"
            | "while"
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_one_expression() {
        let condition = parse_expr("not story.trigger(3) and story.var(2) >= 1").unwrap();
        assert!(matches!(condition, Expr::Binary(BinOp::And, ..)));
        assert!(parse_expr("1, 2").is_err());
        assert!(matches!(
            parse_expr("story.var(2) >="),
            Err(Error::Syntax { line: 1, .. })
        ));
    }

    #[test]
    fn reads_statements_with_their_comments() {
        let block = parse_chunk(
            "-- header\nstory.set_var(5, 1) -- var, value\nif routines.at_0010() == ENDED then return ENDED end -- jump_and_link\nreturn -- jump_return\n",
        )
        .unwrap();
        assert_eq!(block[0].kind, StmtKind::Comment("header".into()));
        assert_eq!(block[1].comment.as_deref(), Some("var, value"));
        let StmtKind::If { arms, otherwise } = &block[2].kind else {
            panic!("{:?}", block[2]);
        };
        assert!(otherwise.is_none());
        assert_eq!(arms[0].1[0].comment, None);
        assert_eq!(block[2].comment.as_deref(), Some("jump_and_link"));
        assert_eq!(block[3].kind, StmtKind::Return(vec![]));
        assert_eq!(block[3].comment.as_deref(), Some("jump_return"));
    }

    #[test]
    fn binds_like_lua() {
        let block = parse_chunk("x = not a and b or c == 1").unwrap();
        let StmtKind::Assign { value, .. } = &block[0].kind else {
            panic!()
        };
        let Expr::Binary(BinOp::Or, left, right) = value else {
            panic!("{value:?}")
        };
        assert!(
            matches!(left.as_ref(), Expr::Binary(BinOp::And, l, _) if matches!(l.as_ref(), Expr::Not(_)))
        );
        assert!(matches!(right.as_ref(), Expr::Binary(BinOp::Eq, _, _)));
    }

    #[test]
    fn fails_with_the_line() {
        let error = parse_chunk("story.a()\nfor i = 1, 2 do end").unwrap_err();
        assert_eq!(error.to_string(), "line 2: 'for' where a value goes");
        let error = parse_chunk("story.a()\nx").unwrap_err();
        assert_eq!(error.to_string(), "line 2: a statement that is not a call");
    }
}
