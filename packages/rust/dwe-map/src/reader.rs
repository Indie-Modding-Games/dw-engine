//! Checks of a YAML tree with the engine's words (packages/engine/assets/src/yaml_reader.cpp), for
//! the documents the tools check key by key as the engine does: map patches and the field's
//! trigger bands. A problem is its message; the first one stops the reading, as in the engine.

use serde_yaml_ng::{Mapping, Value};

pub(crate) type Checked<T> = std::result::Result<T, String>;

/// The mapping of a node with exactly these keys: every required one and no key but the optional
/// ones (YamlReader::expectKeys).
pub(crate) fn expect_keys<'a>(
    node: &'a Value,
    what: &str,
    required: &[&str],
    optional: &[&str],
) -> Checked<&'a Mapping> {
    let Value::Mapping(mapping) = node else {
        return Err(format!("{what} must be a mapping"));
    };
    for key in mapping.keys() {
        let name = key_text(key);
        if !required.contains(&name.as_str()) && !optional.contains(&name.as_str()) {
            return Err(format!("unknown key '{name}' in {what}"));
        }
    }
    if let Some(missing) = required.iter().find(|key| !mapping.contains_key(**key)) {
        return Err(format!("{what} has no '{missing}'"));
    }
    Ok(mapping)
}

/// The value at a key of a mapping that has it (YamlReader::field).
pub(crate) fn field<'a>(mapping: &'a Mapping, key: &str) -> Checked<&'a Value> {
    mapping.get(key).ok_or_else(|| format!("missing '{key}'"))
}

/// The items of a list (YamlReader::sequence).
pub(crate) fn sequence<'a>(node: &'a Value, what: &str) -> Checked<&'a [Value]> {
    match node {
        Value::Sequence(items) => Ok(items),
        _ => Err(format!("{what} must be a list")),
    }
}

/// A whole number that fits in T (YamlReader::integer).
pub(crate) fn integer<T: TryFrom<i64>>(node: &Value) -> Checked<T> {
    let text = match node {
        Value::Number(number) => number.to_string(),
        Value::String(text) => text.clone(),
        Value::Bool(value) => value.to_string(),
        Value::Null => "null".to_owned(),
        Value::Sequence(_) | Value::Mapping(_) | Value::Tagged(_) => {
            return Err("expected a number".to_owned());
        }
    };
    let value = whole_number(&text).ok_or_else(|| format!("'{text}' is not a whole number"))?;
    T::try_from(value).map_err(|_| "the number is out of range".to_owned())
}

/// The number at a key a mapping may leave out, or None when it is missing or null
/// (YamlReader::optionalInteger).
pub(crate) fn optional_integer<T: TryFrom<i64>>(
    mapping: &Mapping,
    key: &str,
) -> Checked<Option<T>> {
    match mapping.get(key) {
        None | Some(Value::Null) => Ok(None),
        Some(value) => integer(value).map(Some),
    }
}

/// A decimal number as `std::from_chars` reads it: an optional minus and digits, nothing else.
pub(crate) fn whole_number(text: &str) -> Option<i64> {
    let digits = text.strip_prefix('-').unwrap_or(text);
    if digits.is_empty() || !digits.bytes().all(|byte| byte.is_ascii_digit()) {
        return None;
    }
    text.parse().ok()
}

/// A mapping key as text.
pub(crate) fn key_text(key: &Value) -> String {
    match key {
        Value::String(text) => text.clone(),
        Value::Number(number) => number.to_string(),
        Value::Bool(value) => value.to_string(),
        Value::Null => "null".to_owned(),
        other => format!("{other:?}"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn yaml(text: &str) -> Value {
        serde_yaml_ng::from_str(text).unwrap()
    }

    #[test]
    fn reads_like_the_engine() {
        let node = yaml("{a: 1, b: 2}");
        assert!(expect_keys(&node, "it", &["a"], &["b"]).is_ok());
        assert_eq!(
            expect_keys(&node, "it", &["a"], &[]).unwrap_err(),
            "unknown key 'b' in it"
        );
        assert_eq!(
            expect_keys(&node, "it", &["a", "b", "c"], &[]).unwrap_err(),
            "it has no 'c'"
        );
        assert_eq!(
            expect_keys(&yaml("- 1"), "it", &[], &[]).unwrap_err(),
            "it must be a mapping"
        );
        assert_eq!(integer::<i32>(&yaml("-12")), Ok(-12));
        assert_eq!(integer::<i32>(&yaml("\"7\"")), Ok(7));
        assert_eq!(
            integer::<i32>(&yaml("north")).unwrap_err(),
            "'north' is not a whole number"
        );
        assert_eq!(
            integer::<i32>(&yaml("1.5")).unwrap_err(),
            "'1.5' is not a whole number"
        );
        assert_eq!(
            integer::<u8>(&yaml("256")).unwrap_err(),
            "the number is out of range"
        );
        assert_eq!(
            integer::<i32>(&yaml("[1]")).unwrap_err(),
            "expected a number"
        );
        let mapping = yaml("{w: null, h: 3}");
        let mapping = mapping.as_mapping().unwrap();
        assert_eq!(optional_integer::<i32>(mapping, "w"), Ok(None));
        assert_eq!(optional_integer::<i32>(mapping, "h"), Ok(Some(3)));
        assert_eq!(optional_integer::<i32>(mapping, "d"), Ok(None));
    }
}
