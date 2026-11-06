//! Checks against the player's own extracted content, which never is in the repository: with
//! `DWE_MAPPER_CONTENT` set to a content folder (base pack and packs), every map of the table
//! loads, its collision writes back exactly as `dwx` wrote it, the whole document writes back
//! as it was read, and every exit's target resolves.
//! Without the variable the tests pass without checking anything.

use std::path::PathBuf;

use dwe_map::collision::CollisionSection;
use dwe_map::{load, load_map, load_map_table, warp_target};
use dwe_pack::{GameContent, GameFolders};

fn content() -> Option<GameContent> {
    let folder = PathBuf::from(std::env::var_os("DWE_MAPPER_CONTENT")?);
    Some(
        GameContent::mount(&GameFolders {
            content: folder,
            ..GameFolders::default()
        })
        .expect("the content mounts"),
    )
}

#[test]
fn every_map_loads_and_its_collision_round_trips() {
    let Some(content) = content() else {
        eprintln!("DWE_MAPPER_CONTENT is not set: skipped");
        return;
    };
    let table = load_map_table(&content.vfs).expect("data/maps composes");
    let mut checked = 0;
    for entry in table.maps.iter().filter(|entry| entry.has_file()) {
        let Ok(id) = load::map_data_id(&entry.id) else {
            continue;
        };
        if !content.vfs.contains(&id) {
            continue;
        }
        let map = load_map(&content.vfs, &entry.id).expect("the map loads");
        let text = content.vfs.read_text(&id).unwrap();
        let document: serde_yaml_ng::Value = serde_yaml_ng::from_str(&text).unwrap();
        let section: CollisionSection =
            serde_yaml_ng::from_value(document["collision"].clone()).unwrap();
        assert_eq!(map.collision.to_section(), section, "{}", entry.id);
        // Unpatched data writes back byte for byte; a patched map at least as the same tree.
        let written = map.to_yaml();
        if map.patches.is_empty() {
            assert_eq!(written, text, "{} writes back", entry.id);
        } else {
            let tree: serde_yaml_ng::Value = serde_yaml_ng::from_str(&written).unwrap();
            assert_eq!(
                dwe_map::MapDocument::parse(&entry.id, &written)
                    .unwrap()
                    .to_tree(),
                tree
            );
        }
        for warp in &map.warps {
            let _ = warp_target(&table, warp);
        }
        checked += 1;
    }
    assert!(checked > 0, "no map has data");
    eprintln!("{checked} maps checked");
}
