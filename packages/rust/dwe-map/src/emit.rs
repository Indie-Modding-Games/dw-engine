//! Writes a YAML tree the way the engine's tools write their documents (core::emitYaml over
//! libyaml, as `dwx` writes `map/<id>/data`): block mappings and sequences indented by two, the
//! items of a sequence inside a mapping at the mapping's own indentation, keys plain, text always
//! double-quoted, numbers and null plain, empty lists `[]` and empty mappings `{}`. A document the
//! extractor wrote comes back byte for byte, so the diffs of an edited map stay small.

use serde_yaml_ng::{Mapping, Value};

/// The text of a tree, ending with a line break.
pub fn to_yaml(value: &Value) -> String {
    let mut out = String::new();
    match value {
        Value::Mapping(mapping) if !mapping.is_empty() => write_mapping(&mut out, mapping, 0),
        Value::Sequence(items) if !items.is_empty() => write_sequence(&mut out, items, 0),
        other => {
            out.push_str(&scalar(other));
            out.push('\n');
        }
    }
    out
}

/// Whether a node is written on its own lines below its key or dash.
fn is_block(value: &Value) -> bool {
    match value {
        Value::Mapping(mapping) => !mapping.is_empty(),
        Value::Sequence(items) => !items.is_empty(),
        Value::Tagged(tagged) => is_block(&tagged.value),
        _ => false,
    }
}

fn write_mapping(out: &mut String, mapping: &Mapping, indent: usize) {
    for (index, (key, value)) in mapping.iter().enumerate() {
        // The first key of a mapping inside a sequence item follows its dash.
        if index > 0 || !out.ends_with("- ") {
            push_indent(out, indent);
        }
        out.push_str(&key_text(key));
        out.push(':');
        write_value(out, value, indent, true);
    }
}

fn write_sequence(out: &mut String, items: &[Value], indent: usize) {
    for (index, item) in items.iter().enumerate() {
        if index > 0 || !out.ends_with("- ") {
            push_indent(out, indent);
        }
        out.push_str("- ");
        match item {
            Value::Mapping(mapping) if !mapping.is_empty() => {
                write_mapping(out, mapping, indent + 2);
            }
            Value::Sequence(inner) if !inner.is_empty() => write_sequence(out, inner, indent + 2),
            other => {
                out.push_str(&scalar(other));
                out.push('\n');
            }
        }
    }
}

/// The value after `key:`: a scalar on the same line, a block below it.
fn write_value(out: &mut String, value: &Value, indent: usize, in_mapping: bool) {
    let value = match value {
        Value::Tagged(tagged) => &tagged.value,
        other => other,
    };
    if !is_block(value) {
        out.push(' ');
        out.push_str(&scalar(value));
        out.push('\n');
        return;
    }
    out.push('\n');
    match value {
        Value::Mapping(mapping) => write_mapping(out, mapping, indent + 2),
        // A sequence under a mapping key is not indented (libyaml's default).
        Value::Sequence(items) => {
            write_sequence(out, items, if in_mapping { indent } else { indent + 2 })
        }
        _ => {}
    }
}

fn push_indent(out: &mut String, indent: usize) {
    out.extend(std::iter::repeat_n(' ', indent));
}

fn key_text(key: &Value) -> String {
    match key {
        Value::String(text) if is_plain_key(text) => text.clone(),
        Value::String(text) => quote(text),
        other => scalar(other),
    }
}

/// A key libyaml writes plain: letters, digits, `_`, `-`, `.` and `/`, not starting with a
/// sign or a digit that would read as a number.
fn is_plain_key(text: &str) -> bool {
    !text.is_empty()
        && text
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'_' | b'-' | b'.' | b'/'))
        && !matches!(text, "null" | "true" | "false" | "~")
        && text.parse::<f64>().is_err()
}

/// A scalar or an empty collection on one line.
fn scalar(value: &Value) -> String {
    match value {
        Value::Null => "null".to_owned(),
        Value::Bool(value) => value.to_string(),
        Value::Number(number) => number.to_string(),
        Value::String(text) => quote(text),
        Value::Sequence(_) => "[]".to_owned(),
        Value::Mapping(_) => "{}".to_owned(),
        Value::Tagged(tagged) => scalar(&tagged.value),
    }
}

/// A double-quoted scalar with libyaml's escapes (unicode is written as is).
fn quote(text: &str) -> String {
    let mut out = String::with_capacity(text.len() + 2);
    out.push('"');
    for character in text.chars() {
        match character {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\0' => out.push_str("\\0"),
            '\u{7}' => out.push_str("\\a"),
            '\u{8}' => out.push_str("\\b"),
            '\t' => out.push_str("\\t"),
            '\n' => out.push_str("\\n"),
            '\u{b}' => out.push_str("\\v"),
            '\u{c}' => out.push_str("\\f"),
            '\r' => out.push_str("\\r"),
            '\u{1b}' => out.push_str("\\e"),
            '\u{85}' => out.push_str("\\N"),
            '\u{a0}' => out.push_str("\\_"),
            '\u{2028}' => out.push_str("\\L"),
            '\u{2029}' => out.push_str("\\P"),
            control if control.is_control() => {
                out.push_str(&format!("\\x{:02X}", u32::from(control)));
            }
            other => out.push(other),
        }
    }
    out.push('"');
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    const DOCUMENT: &str = r#"camera:
  viewpoint:
    x: 0
    y: -4301
    z: -5973
  projection: 1813
lights:
- direction:
    x: 0
    y: 170
  color:
    r: 178
images: []
objects: null
regions:
  a:
  - -1
  - 2
warps:
- exit: 0
  target_map_id: null
  name: "a \"b\" c"
npcs:
- type_id: 117
  moves:
  - 46
  - 51
  waypoints:
  - x: 10
    y: 0
nested:
- - 1
  - 2
- {}
rows:
- "0 1 2"
"#;

    #[test]
    fn writes_back_what_dwx_wrote() {
        let tree: Value = serde_yaml_ng::from_str(DOCUMENT).unwrap();
        let text = to_yaml(&tree);
        assert_eq!(text, DOCUMENT);
        let back: Value = serde_yaml_ng::from_str(&text).unwrap();
        assert_eq!(back, tree);
    }

    #[test]
    fn quotes_what_needs_it() {
        assert_eq!(quote("a\tb\n"), "\"a\\tb\\n\"");
        assert_eq!(quote("ñandú"), "\"ñandú\"");
        let mut mapping = Mapping::new();
        mapping.insert(Value::from("12"), Value::from(1));
        mapping.insert(Value::from("a key"), Value::from(true));
        let text = to_yaml(&Value::Mapping(mapping));
        assert_eq!(text, "\"12\": 1\n\"a key\": true\n");
        assert_eq!(to_yaml(&Value::Null), "null\n");
    }
}
