//! The tokens of the story's Lua: names, numbers, strings and symbols, with the comments apart
//! (they are kept, but they are not code). The same tokens as the engine's reader
//! (packages/formats/src/script/lua_reader.cpp: tokenize).

use crate::{Error, Result};

#[derive(Clone, Debug, PartialEq)]
pub enum TokenKind {
    Name(String),
    Number(i64),
    Str(String),
    Symbol(&'static str),
    End,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Token {
    pub kind: TokenKind,
    /// The line it is on, from 1.
    pub line: usize,
}

/// A `--` comment: its text after the dashes (trimmed) and its line.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Comment {
    pub text: String,
    pub line: usize,
}

/// The symbols, longest first so `==` is not read as two `=`.
const SYMBOLS: [&str; 22] = [
    "==", "~=", "<=", ">=", "..", "(", ")", "{", "}", "[", "]", ",", ";", "=", "<", ">", ".", "+",
    "-", "*", "/", "#",
];

/// Splits Lua code into tokens (ending with [`TokenKind::End`]) and comments.
pub fn tokenize(code: &str) -> Result<(Vec<Token>, Vec<Comment>)> {
    let bytes = code.as_bytes();
    let mut tokens = Vec::new();
    let mut comments = Vec::new();
    let mut line = 1;
    let mut at = 0;
    while at < bytes.len() {
        let c = bytes[at];
        if c == b'\n' {
            line += 1;
            at += 1;
            continue;
        }
        if c.is_ascii_whitespace() {
            at += 1;
            continue;
        }
        if code[at..].starts_with("--") {
            if code[at + 2..].starts_with("[[") {
                return Err(Error::at(line, "long comments (--[[ ]]) are not read"));
            }
            let end = code[at..].find('\n').map_or(bytes.len(), |n| at + n);
            comments.push(Comment {
                text: code[at + 2..end].trim().to_owned(),
                line,
            });
            at = end;
            continue;
        }
        if c.is_ascii_alphabetic() || c == b'_' {
            let start = at;
            while at < bytes.len() && (bytes[at].is_ascii_alphanumeric() || bytes[at] == b'_') {
                at += 1;
            }
            tokens.push(Token {
                kind: TokenKind::Name(code[start..at].to_owned()),
                line,
            });
            continue;
        }
        if c.is_ascii_digit() {
            let (number, end) = read_number(code, at, line)?;
            tokens.push(Token {
                kind: TokenKind::Number(number),
                line,
            });
            at = end;
            continue;
        }
        if c == b'"' || c == b'\'' {
            let (text, end, lines) = read_string(code, at, line)?;
            tokens.push(Token {
                kind: TokenKind::Str(text),
                line,
            });
            line += lines;
            at = end;
            continue;
        }
        let Some(symbol) = SYMBOLS.iter().find(|symbol| code[at..].starts_with(**symbol)) else {
            return Err(Error::at(
                line,
                format!("'{}' is not Lua the story uses", code[at..].chars().next().unwrap_or(' ')),
            ));
        };
        tokens.push(Token {
            kind: TokenKind::Symbol(symbol),
            line,
        });
        at += symbol.len();
    }
    tokens.push(Token {
        kind: TokenKind::End,
        line,
    });
    Ok((tokens, comments))
}

fn read_number(code: &str, start: usize, line: usize) -> Result<(i64, usize)> {
    let bytes = code.as_bytes();
    let mut at = start;
    let hex = code[start..].starts_with("0x") || code[start..].starts_with("0X");
    if hex {
        at += 2;
    }
    let digits = at;
    while at < bytes.len() && (bytes[at].is_ascii_alphanumeric() || bytes[at] == b'.') {
        at += 1;
    }
    let text = &code[digits..at];
    let parsed = if hex {
        i64::from_str_radix(text, 16)
    } else {
        text.parse()
    };
    parsed
        .map(|number| (number, at))
        .map_err(|_| Error::at(line, format!("'{}' is not a whole number", &code[start..at])))
}

/// Reads a quoted string; returns its value, where it ends and the newlines it crossed.
fn read_string(code: &str, start: usize, line: usize) -> Result<(String, usize, usize)> {
    let quote = code.as_bytes()[start] as char;
    let mut text = String::new();
    let mut chars = code[start + 1..].char_indices();
    while let Some((offset, c)) = chars.next() {
        match c {
            c if c == quote => return Ok((text, start + 1 + offset + 1, 0)),
            '\n' => return Err(Error::at(line, "a string that does not end on its line")),
            '\\' => {
                let Some((_, escaped)) = chars.next() else { break };
                text.push(match escaped {
                    'n' => '\n',
                    't' => '\t',
                    'r' => '\r',
                    '0' => '\0',
                    other => other,
                });
            }
            c => text.push(c),
        }
    }
    Err(Error::at(line, "a string that does not end"))
}

/// A string as Lua code, in double quotes.
pub fn quote(text: &str) -> String {
    let mut quoted = String::with_capacity(text.len() + 2);
    quoted.push('"');
    for c in text.chars() {
        match c {
            '"' => quoted.push_str("\\\""),
            '\\' => quoted.push_str("\\\\"),
            '\n' => quoted.push_str("\\n"),
            '\t' => quoted.push_str("\\t"),
            '\r' => quoted.push_str("\\r"),
            '\0' => quoted.push_str("\\0"),
            c => quoted.push(c),
        }
    }
    quoted.push('"');
    quoted
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_tokens_and_comments() {
        let (tokens, comments) =
            tokenize("story.set_var(5, 0x10) -- var, value\nlocal a <const> = \"x\\\"y\"").unwrap();
        assert_eq!(tokens[0].kind, TokenKind::Name("story".into()));
        assert_eq!(tokens[6].kind, TokenKind::Number(16));
        assert_eq!(comments[0].text, "var, value");
        assert_eq!(comments[0].line, 1);
        assert!(tokens.iter().any(|t| t.kind == TokenKind::Str("x\"y".into()) && t.line == 2));
        assert_eq!(tokens.last().unwrap().kind, TokenKind::End);
    }

    #[test]
    fn quotes_back() {
        assert_eq!(quote("a\"b\\c\n"), "\"a\\\"b\\\\c\\n\"");
        let (tokens, _) = tokenize(&quote("a\"b\\c\n")).unwrap();
        assert_eq!(tokens[0].kind, TokenKind::Str("a\"b\\c\n".into()));
    }

    #[test]
    fn names_the_line_of_a_bad_character() {
        let error = tokenize("a\nb @").unwrap_err();
        assert_eq!(error.to_string(), "line 2: '@' is not Lua the story uses");
    }
}
