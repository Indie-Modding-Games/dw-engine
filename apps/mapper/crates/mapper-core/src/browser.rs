//! The map browser: the maps of the table that have data, grouped by the name the game shows when
//! they load (their zone) or by the prefix of their id.

use dwe_map::MapTable;

use crate::text;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum GroupBy {
    /// The loading name of `data/maps`.
    #[default]
    LoadingName,
    /// The letters before the digits of the id (`area` of `area01`), or a pack's namespace.
    Prefix,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MapItem {
    pub id: String,
    pub index: i32,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MapGroup {
    pub title: String,
    pub maps: Vec<MapItem>,
}

/// The groups of the maps that `has_data` accepts and the filter matches (by id or group title,
/// ignoring case), sorted by title and then by id.
pub fn group_maps(
    table: &MapTable,
    has_data: impl Fn(&str) -> bool,
    group_by: GroupBy,
    filter: &str,
) -> Vec<MapGroup> {
    let filter = filter.trim().to_lowercase();
    let mut groups: Vec<MapGroup> = Vec::new();
    for entry in &table.maps {
        if !has_data(&entry.id) {
            continue;
        }
        let title = match group_by {
            GroupBy::LoadingName => match table.display_name(entry) {
                "" => text::UNNAMED_GROUP.to_owned(),
                name => name.to_owned(),
            },
            GroupBy::Prefix => prefix_of(&entry.id).to_owned(),
        };
        if !filter.is_empty()
            && !entry.id.contains(&filter)
            && !title.to_lowercase().contains(&filter)
        {
            continue;
        }
        let item = MapItem {
            id: entry.id.clone(),
            index: entry.index,
        };
        match groups.iter_mut().find(|group| group.title == title) {
            Some(group) => group.maps.push(item),
            None => groups.push(MapGroup {
                title,
                maps: vec![item],
            }),
        }
    }
    groups.sort_by_key(|group| group.title.to_lowercase());
    for group in &mut groups {
        group.maps.sort_by(|a, b| a.id.cmp(&b.id));
    }
    groups
}

/// The letters before the first digit of an id, or the namespace of a pack's id.
pub fn prefix_of(id: &str) -> &str {
    if let Some((namespace, _)) = id.split_once(':') {
        return namespace;
    }
    let end = id.find(|c: char| c.is_ascii_digit()).unwrap_or(id.len());
    if end == 0 { id } else { &id[..end] }
}

#[cfg(test)]
mod tests {
    use std::collections::BTreeSet;

    use super::*;

    fn table() -> MapTable {
        let value = serde_yaml_ng::from_str(
            "maps:\n\
             - {id: town02, index: 0, loading_name: 1}\n\
             - {id: town01, index: 1, loading_name: 1}\n\
             - {id: cave01, index: 2, loading_name: 0}\n\
             - {id: cave02, index: 3, loading_name: 9}\n\
             - {id: empty, index: 4, loading_name: 1}\n\
             - {id: 'my_mod:meadow', index: 5, loading_name: 0}\n\
             map_names: [' Deep Cave ', ' Town ']\n",
        )
        .unwrap();
        MapTable::from_value(value, &BTreeSet::new()).unwrap()
    }

    fn titles(groups: &[MapGroup]) -> Vec<(&str, Vec<&str>)> {
        groups
            .iter()
            .map(|group| {
                (
                    group.title.as_str(),
                    group.maps.iter().map(|map| map.id.as_str()).collect(),
                )
            })
            .collect()
    }

    #[test]
    fn groups_by_loading_name_and_prefix_with_a_filter() {
        let table = table();
        let has_data = |id: &str| id != "empty";
        assert_eq!(
            titles(&group_maps(&table, has_data, GroupBy::LoadingName, "")),
            vec![
                (text::UNNAMED_GROUP, vec!["cave02"]),
                ("Deep Cave", vec!["cave01", "my_mod:meadow"]),
                ("Town", vec!["town01", "town02"]),
            ]
        );
        assert_eq!(
            titles(&group_maps(&table, has_data, GroupBy::Prefix, "")),
            vec![
                ("cave", vec!["cave01", "cave02"]),
                ("my_mod", vec!["my_mod:meadow"]),
                ("town", vec!["town01", "town02"]),
            ]
        );
        assert_eq!(
            titles(&group_maps(&table, has_data, GroupBy::LoadingName, "TOWN")),
            vec![("Town", vec!["town01", "town02"])]
        );
        assert_eq!(
            titles(&group_maps(&table, has_data, GroupBy::Prefix, "01")),
            vec![("cave", vec!["cave01"]), ("town", vec!["town01"])]
        );
    }
}
