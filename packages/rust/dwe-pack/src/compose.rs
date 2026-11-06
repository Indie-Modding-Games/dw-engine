use std::collections::{BTreeMap, BTreeSet, HashMap};

use serde_yaml_ng::{Mapping, Value};

use crate::{AssetId, ContentId, Error, Layer, PackOrigin, Result, Vfs, has_base_rights};

/// The key of an entry's number, which the engine gives to each new entry.
const INDEX_KEY: &str = "index";

/// A section of a table that is a list of entries, each named by the content id in `entry_key`
/// (table_composition.hpp).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct EntrySection {
    pub section: &'static str,
    pub entry_key: &'static str,
    /// Whether `index` is the entry's own number, unique in the section, which the engine gives
    /// to a new entry.
    pub numbered: bool,
    /// Whether the key is the entry's own id, so a pack that adds an entry names it in its
    /// namespace.
    pub owns_ids: bool,
}

/// The sections of a table that compose entry by entry; any other section of a higher pack
/// replaces the whole section below it.
#[derive(Clone, Copy, Debug)]
pub struct Composition {
    pub sections: &'static [EntrySection],
}

/// `data/maps`: maps by id; doors, toilets, map names and light updates go by position or by map
/// index, so a pack that has them replaces each of those sections whole (map_table.hpp).
pub const MAP_COMPOSITION: Composition = Composition {
    sections: &[EntrySection {
        section: "maps",
        entry_key: "id",
        numbered: true,
        owns_ids: true,
    }],
};

/// One pack's file of a table.
#[derive(Clone, Debug)]
pub struct Contribution {
    pub pack_id: String,
    pub layer: Layer,
    pub origin: PackOrigin,
    /// The file, for the errors.
    pub file: String,
    pub document: Value,
}

impl Contribution {
    fn has_base_rights(&self) -> bool {
        has_base_rights(self.origin, self.layer)
    }

    fn fail<T>(&self, message: impl std::fmt::Display) -> Result<T> {
        Err(Error::invalid(format!("{}: {message}", self.file)))
    }
}

/// A composed table and the entries of numbered sections whose index the engine gave.
#[derive(Clone, Debug, PartialEq)]
pub struct ComposedTable {
    pub value: Value,
    pub engine_numbered: BTreeSet<String>,
}

/// Composes the files of a table from the bottom of the stack up
/// (docs/architecture/vfs.md#cómo-se-componen). An entry whose id is already there replaces that
/// entry in its place and keeps its index; a new one goes at the end with the next free index.
pub fn compose_table(
    contributions: &[Contribution],
    composition: &Composition,
) -> Result<ComposedTable> {
    let mut table = Mapping::new();
    let mut composers: HashMap<String, SectionComposer> = HashMap::new();
    for contribution in contributions {
        let Value::Mapping(document) = &contribution.document else {
            return contribution.fail("a table must be a mapping of sections");
        };
        for (key, value) in document {
            let Some(name) = key.as_str() else {
                return contribution.fail("a section name must be text");
            };
            let rule = composition
                .sections
                .iter()
                .find(|rule| rule.section == name);
            let Some(rule) = rule else {
                composers.remove(name);
                table.insert(key.clone(), value.clone());
                continue;
            };
            let Value::Sequence(entries) = value else {
                return contribution.fail(format!("section '{name}' must be a list of entries"));
            };
            let composer = composers.entry(name.to_owned()).or_insert_with(|| {
                table.insert(key.clone(), Value::Sequence(Vec::new()));
                SectionComposer::new(*rule)
            });
            composer.add(contribution, entries)?;
        }
    }
    let mut engine_numbered = BTreeSet::new();
    for (name, composer) in &composers {
        composer.add_engine_numbered(&mut engine_numbered);
        table.insert(
            Value::from(name.as_str()),
            Value::Sequence(composer.entries.clone()),
        );
    }
    Ok(ComposedTable {
        value: Value::Mapping(table),
        engine_numbered,
    })
}

impl Vfs {
    /// A table of the packs composed from every pack that has it (readComposedTableText).
    pub fn read_composed_table(
        &self,
        id: &AssetId,
        composition: &Composition,
    ) -> Result<ComposedTable> {
        let sources = self.sources(id);
        if sources.is_empty() {
            return Err(Error::invalid(format!("no pack has the asset '{id}'")));
        }
        let mut contributions = Vec::with_capacity(sources.len());
        for pack in sources {
            let file = pack.file(id).expect("a source has the file");
            let text = std::fs::read_to_string(file).map_err(|error| Error::io(file, error))?;
            let document: Value =
                serde_yaml_ng::from_str(&text).map_err(|error| Error::yaml(file, &error))?;
            contributions.push(Contribution {
                pack_id: pack.id().to_owned(),
                layer: pack.manifest.layer,
                origin: pack.origin,
                file: file.display().to_string(),
                document,
            });
        }
        compose_table(&contributions, composition)
    }
}

struct SectionComposer {
    rule: EntrySection,
    entries: Vec<Value>,
    indices: Vec<Option<i64>>,
    /// Whether the base gave the entry its index.
    original: Vec<bool>,
    positions: BTreeMap<String, usize>,
    /// The id that has each index of a numbered section.
    owners: BTreeMap<i64, String>,
    next_index: i64,
}

impl SectionComposer {
    fn new(rule: EntrySection) -> Self {
        Self {
            rule,
            entries: Vec::new(),
            indices: Vec::new(),
            original: Vec::new(),
            positions: BTreeMap::new(),
            owners: BTreeMap::new(),
            next_index: 0,
        }
    }

    fn add(&mut self, contribution: &Contribution, entries: &[Value]) -> Result<()> {
        let mut listed = BTreeSet::new();
        for entry in entries {
            let id = self.id_of(contribution, entry)?;
            if !listed.insert(id.as_str().to_owned()) {
                return contribution.fail(format!(
                    "section '{}' lists '{id}' twice",
                    self.rule.section
                ));
            }
            match self.positions.get(id.as_str()) {
                Some(&position) => self.replace(contribution, &id, entry, position)?,
                None => self.append(contribution, &id, entry)?,
            }
        }
        Ok(())
    }

    fn add_engine_numbered(&self, ids: &mut BTreeSet<String>) {
        if !self.rule.numbered {
            return;
        }
        for (id, &position) in &self.positions {
            if !self.original[position] {
                ids.insert(id.clone());
            }
        }
    }

    fn id_of(&self, contribution: &Contribution, entry: &Value) -> Result<ContentId> {
        let key = entry.get(self.rule.entry_key).and_then(scalar_text);
        let Some(key) = key else {
            return contribution.fail(format!(
                "an entry of section '{}' needs the key '{}'",
                self.rule.section, self.rule.entry_key
            ));
        };
        ContentId::parse(&key).or_else(|error| {
            contribution.fail(format!(
                "'{key}' in section '{}' is not a content id: {error}",
                self.rule.section
            ))
        })
    }

    fn given_index(
        &self,
        contribution: &Contribution,
        id: &ContentId,
        entry: &Value,
    ) -> Result<Option<i64>> {
        match entry.get(INDEX_KEY) {
            None => Ok(None),
            Some(Value::Number(number)) if number.as_i64().is_some() => Ok(number.as_i64()),
            Some(_) => contribution.fail(format!(
                "'{id}' in section '{}' has an index that is not a whole number",
                self.rule.section
            )),
        }
    }

    fn check_namespace(&self, contribution: &Contribution, id: &ContentId) -> Result<()> {
        if !self.rule.owns_ids {
            return Ok(());
        }
        if !id.is_namespaced() && !contribution.has_base_rights() {
            return contribution.fail(format!(
                "'{id}' in section '{}' adds an entry without a namespace; pack '{}' must name it '{}:<name>'",
                self.rule.section, contribution.pack_id, contribution.pack_id
            ));
        }
        if id.is_namespaced() && id.pack_namespace() != contribution.pack_id {
            return contribution.fail(format!(
                "'{id}' in section '{}' uses another pack's namespace; pack '{}' adds its own",
                self.rule.section, contribution.pack_id
            ));
        }
        Ok(())
    }

    fn append(&mut self, contribution: &Contribution, id: &ContentId, entry: &Value) -> Result<()> {
        self.check_namespace(contribution, id)?;
        let given = self.given_index(contribution, id, entry)?;
        if given.is_some() && !contribution.has_base_rights() {
            return contribution.fail(format!(
                "'{id}' in section '{}' is new: only the base gives an index",
                self.rule.section
            ));
        }
        self.positions
            .insert(id.as_str().to_owned(), self.entries.len());
        self.original.push(given.is_some());
        if !self.rule.numbered {
            self.entries.push(entry.clone());
            self.indices.push(given);
            return Ok(());
        }
        let index = given.unwrap_or(self.next_index);
        if let Some(owner) = self.owners.get(&index) {
            return contribution.fail(format!(
                "'{id}' in section '{}' takes index {index}, which '{owner}' has",
                self.rule.section
            ));
        }
        self.owners.insert(index, id.as_str().to_owned());
        self.next_index = self.next_index.max(index + 1);
        self.entries.push(if given.is_some() {
            entry.clone()
        } else {
            with_index(entry, self.rule.entry_key, index)
        });
        self.indices.push(Some(index));
        Ok(())
    }

    fn replace(
        &mut self,
        contribution: &Contribution,
        id: &ContentId,
        entry: &Value,
        position: usize,
    ) -> Result<()> {
        let given = self.given_index(contribution, id, entry)?;
        let kept = self.indices[position];
        if given.is_some() && !self.original[position] && !contribution.has_base_rights() {
            return contribution.fail(format!(
                "'{id}' in section '{}' is new: only the base gives an index",
                self.rule.section
            ));
        }
        if let (Some(given), Some(kept)) = (given, kept)
            && given != kept
        {
            return contribution.fail(format!(
                "'{id}' in section '{}' changes its index from {kept} to {given}",
                self.rule.section
            ));
        }
        self.entries[position] = match (given, kept) {
            (None, Some(kept)) => with_index(entry, self.rule.entry_key, kept),
            _ => entry.clone(),
        };
        Ok(())
    }
}

fn scalar_text(value: &Value) -> Option<String> {
    match value {
        Value::String(text) => Some(text.clone()),
        Value::Number(number) => Some(number.to_string()),
        Value::Bool(flag) => Some(flag.to_string()),
        _ => None,
    }
}

/// The entry with `index` right after its key.
fn with_index(entry: &Value, entry_key: &str, index: i64) -> Value {
    let Value::Mapping(fields) = entry else {
        return entry.clone();
    };
    let mut numbered = Mapping::new();
    for (key, value) in fields {
        numbered.insert(key.clone(), value.clone());
        if key.as_str() == Some(entry_key) {
            numbered.insert(Value::from(INDEX_KEY), Value::from(index));
        }
    }
    Value::Mapping(numbered)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn contribution(pack_id: &str, layer: Layer, origin: PackOrigin, yaml: &str) -> Contribution {
        Contribution {
            pack_id: pack_id.to_owned(),
            layer,
            origin,
            file: format!("{pack_id}/data/maps.yaml"),
            document: serde_yaml_ng::from_str(yaml).unwrap(),
        }
    }

    fn base(yaml: &str) -> Contribution {
        contribution("base", Layer::Base, PackOrigin::Engine, yaml)
    }

    fn ids_and_indices(table: &ComposedTable) -> Vec<(String, i64)> {
        table.value["maps"]
            .as_sequence()
            .unwrap()
            .iter()
            .map(|entry| {
                (
                    entry["id"].as_str().unwrap().to_owned(),
                    entry["index"].as_i64().unwrap(),
                )
            })
            .collect()
    }

    const BASE: &str = "maps:\n- {id: area_a, index: 0, name: 1}\n- {id: area_b, index: 4, name: 2}\n\
                        doors:\n- {index: 0}\nmap_names: [one]\n";

    #[test]
    fn replaces_in_place_and_appends_with_the_next_index() {
        let modded = contribution(
            "my_mod",
            Layer::Mods,
            PackOrigin::Mod,
            "maps:\n- {id: area_a, name: 9}\n- {id: 'my_mod:meadow', name: 3}\nmap_names: [two]\n",
        );
        let table = compose_table(&[base(BASE), modded], &MAP_COMPOSITION).unwrap();
        assert_eq!(
            ids_and_indices(&table),
            vec![
                ("area_a".to_owned(), 0),
                ("area_b".to_owned(), 4),
                ("my_mod:meadow".to_owned(), 5)
            ]
        );
        assert_eq!(table.value["maps"][0]["name"].as_i64(), Some(9));
        // Other sections are replaced whole; untouched ones stay.
        assert_eq!(table.value["map_names"][0].as_str(), Some("two"));
        assert_eq!(table.value["doors"][0]["index"].as_i64(), Some(0));
        assert_eq!(
            table.engine_numbered,
            BTreeSet::from(["my_mod:meadow".to_owned()])
        );
        // The engine's index goes right after the entry's key.
        let keys: Vec<_> = table.value["maps"][2]
            .as_mapping()
            .unwrap()
            .keys()
            .map(|key| key.as_str().unwrap())
            .collect();
        assert_eq!(keys, vec!["id", "index", "name"]);
    }

    #[test]
    fn a_mod_follows_the_namespace_and_index_rules() {
        let compose = |yaml: &str| {
            compose_table(
                &[
                    base(BASE),
                    contribution("my_mod", Layer::Mods, PackOrigin::Mod, yaml),
                ],
                &MAP_COMPOSITION,
            )
        };
        assert!(compose("maps:\n- {id: meadow}\n").is_err(), "no namespace");
        assert!(
            compose("maps:\n- {id: 'other:meadow'}\n").is_err(),
            "foreign"
        );
        assert!(
            compose("maps:\n- {id: 'my_mod:meadow', index: 9}\n").is_err(),
            "own index"
        );
        assert!(
            compose("maps:\n- {id: area_a, index: 3}\n").is_err(),
            "changed index"
        );
        assert!(
            compose("maps:\n- {id: area_a, index: 0}\n").is_ok(),
            "same index"
        );
        assert!(compose("maps: 1\n").is_err(), "not a list");
        assert!(
            compose("maps:\n- {id: area_a}\n- {id: area_a}\n").is_err(),
            "twice"
        );
        // A pack that only declares the base layer has none of the base's rights.
        let fake_base = contribution(
            "fake",
            Layer::Base,
            PackOrigin::Mod,
            "maps:\n- {id: meadow}\n",
        );
        assert!(compose_table(&[base(BASE), fake_base], &MAP_COMPOSITION).is_err());
    }

    #[test]
    fn two_base_entries_cannot_share_an_index() {
        let error = compose_table(
            &[base(
                "maps:\n- {id: area_a, index: 0}\n- {id: area_b, index: 0}\n",
            )],
            &MAP_COMPOSITION,
        )
        .unwrap_err();
        assert!(error.to_string().contains("takes index 0"), "{error}");
    }
}
