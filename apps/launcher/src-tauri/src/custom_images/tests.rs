use super::png::tests::header;
use super::*;
use std::io::Write;

const DAY: &str = "map/mayo01/background_day.png";
const NIGHT: &str = "map/mayo01/background_night.png";
const OTHER_DAY: &str = "map/mayo02/background_day.png";
const ICONS: &str = "texture/etcdat/fi_info_palette12.png";

/// An invented PNG: a real header, then bytes that tell copies apart.
fn png_file(width: u32, height: u32, tag: &str) -> Vec<u8> {
    let mut bytes = header(width, height);
    bytes.extend_from_slice(tag.as_bytes());
    bytes
}

fn write(path: &Path, bytes: &[u8]) {
    fs::create_dir_all(path.parent().unwrap()).unwrap();
    fs::write(path, bytes).unwrap();
}

/// A data folder whose export has two maps of one zone, one of another, and one menu image.
fn prepared() -> (tempfile::TempDir, CustomImages, Catalogue) {
    let data = tempfile::tempdir().unwrap();
    let images = CustomImages::new(data.path());
    let originals = images.originals();
    for target in [DAY, NIGHT, OTHER_DAY] {
        write(&originals.join(target), &png_file(768, 640, target));
    }
    write(&originals.join("map/betl01/background_day.png"), &png_file(320, 240, "betl"));
    write(&originals.join(ICONS), &png_file(256, 64, ICONS));
    write(&originals.join(MAPS_FILE), b"mayo01\tBosque Nativo\nmayo02\tCama de Kunemon\nmayo03\tBosque Nativo\nbetl01\tArena\n");
    let catalogue = images.catalogue().unwrap();
    (data, images, catalogue)
}

fn problems(summary: &BulkSummary) -> BTreeMap<String, Problem> {
    summary.rejected.iter().map(|r| (r.name.clone(), r.problem.clone())).collect()
}

#[test]
fn names_match_from_the_first_map_or_texture_folder_in_any_case() {
    let (_data, _images, catalogue) = prepared();
    assert_eq!(catalogue.target_of("dwe-imagenes/map/mayo01/background_day.png").as_deref(), Some(DAY));
    assert_eq!(catalogue.target_of("Mis HD\\MAP\\MAYO01\\Background_Night.PNG").as_deref(), Some(NIGHT));
    assert_eq!(catalogue.target_of("texture/etcdat/fi_info_palette12.png").as_deref(), Some(ICONS));
    assert_eq!(catalogue.target_of("map/mayo01/background_noon.png"), None);
    assert_eq!(catalogue.target_of("background_day.png"), None);
    assert_eq!(catalogue.target_of("texture/etcdat/fi_info_palette99.png"), None);
}

#[test]
fn a_bulk_load_sorts_what_it_finds_and_applies_only_the_matches() {
    let (data, images, catalogue) = prepared();
    let source = data.path().join("mine");
    write(&source.join("hd").join(DAY), &png_file(1536, 1280, "2x day"));
    write(&source.join("hd").join(ICONS), &png_file(1024, 256, "4x icons"));
    write(&source.join("hd").join(OTHER_DAY), &png_file(768, 640, OTHER_DAY));
    write(&source.join("hd/map/mayo01/background_noon.png"), &png_file(768, 640, "?"));
    write(&source.join("hd/map/betl01/background_day.png"), &png_file(640, 240, "stretched"));
    write(&source.join("hd/LEEME.txt"), b"guide");
    let mut picked = Picked::open(&source).unwrap();

    let summary = images.inspect(&catalogue, &mut picked).unwrap();
    let matched: Vec<_> = summary.matched.iter().map(|m| (m.target.as_str(), m.scale)).collect();
    assert_eq!(matched, [(DAY, 2), (ICONS, 4)]);
    assert_eq!(summary.unchanged, 1);
    assert_eq!(summary.ignored, 1);
    let refused = problems(&summary);
    assert_eq!(refused["hd/map/mayo01/background_noon.png"], Problem::UnknownName);
    assert_eq!(
        refused["hd/map/betl01/background_day.png"],
        Problem::WrongSize { width: 640, height: 240, expected_width: 320, expected_height: 240 }
    );
    assert!(images.custom_targets().is_empty(), "inspecting writes nothing");

    let applied = images.apply(&catalogue, &mut picked).unwrap();
    assert!(applied.applied);
    assert_eq!(fs::read(images.pack().join(DAY)).unwrap(), png_file(1536, 1280, "2x day"));
    assert_eq!(fs::read_to_string(images.pack().join(MANIFEST_FILE)).unwrap(), MANIFEST);
    assert!(!images.pack().join(OTHER_DAY).exists(), "an unchanged image stays out");
    assert_eq!(images.active_pack(), Some(images.pack()));
    images.set_enabled(false).unwrap();
    assert_eq!(images.active_pack(), None);
}

#[test]
fn a_maps_backgrounds_keep_one_scale_and_a_size_beyond_eight_is_refused() {
    let (data, images, catalogue) = prepared();
    let source = data.path().join("mine");
    write(&source.join(DAY), &png_file(1536, 1280, "2x"));
    write(&source.join(NIGHT), &png_file(2304, 1920, "3x"));
    write(&source.join(OTHER_DAY), &png_file(768 * 9, 640 * 9, "9x"));
    let summary = images.inspect(&catalogue, &mut Picked::open(&source).unwrap()).unwrap();
    assert!(summary.matched.is_empty());
    let refused = problems(&summary);
    assert_eq!(refused[DAY], Problem::MixedScale { scale: 2, other: 3 });
    assert_eq!(refused[NIGHT], Problem::MixedScale { scale: 3, other: 2 });
    assert_eq!(refused[OTHER_DAY], Problem::TooLarge { scale: 9 });
}

#[test]
fn a_zip_loads_like_a_folder_and_a_name_given_twice_counts_once() {
    let (data, images, catalogue) = prepared();
    let archive = data.path().join("mine.zip");
    let mut zip = zip::ZipWriter::new(fs::File::create(&archive).unwrap());
    let options = zip::write::SimpleFileOptions::default();
    for (name, tag) in [("hd/map/mayo01/background_day.png", "a"), ("otra/map/mayo01/background_day.png", "b")] {
        zip.start_file(name, options).unwrap();
        zip.write_all(&png_file(1536, 1280, tag)).unwrap();
    }
    zip.finish().unwrap();
    let summary = images.apply(&catalogue, &mut Picked::open(&archive).unwrap()).unwrap();
    assert_eq!(summary.matched.len(), 1);
    assert_eq!(problems(&summary)["otra/map/mayo01/background_day.png"], Problem::Duplicate);
    assert_eq!(fs::read(images.pack().join(DAY)).unwrap(), png_file(1536, 1280, "a"));
    assert!(Picked::open(&data.path().join("nothing.rar")).is_err());
}

#[test]
fn one_image_is_assigned_checked_and_reset() {
    let (data, images, catalogue) = prepared();
    let file = data.path().join("day.png");
    write(&file, &png_file(1536, 1280, "2x"));
    assert_eq!(images.assign(&catalogue, DAY, &file).unwrap(), Ok(2));
    assert_eq!(images.custom_scale(&catalogue, DAY), Some(2));

    let night = data.path().join("night.png");
    write(&night, &png_file(3072, 2560, "4x"));
    assert_eq!(images.assign(&catalogue, NIGHT, &night).unwrap(), Err(Problem::MixedScale { scale: 4, other: 2 }));
    let replaced = data.path().join("day4.png");
    write(&replaced, &png_file(3072, 2560, "4x day"));
    assert_eq!(images.assign(&catalogue, DAY, &replaced).unwrap(), Ok(4), "replacing a map's only image may change its scale");

    let original = data.path().join("same.png");
    write(&original, &png_file(768, 640, NIGHT));
    assert_eq!(images.assign(&catalogue, NIGHT, &original).unwrap(), Err(Problem::Unchanged));
    write(&original, b"not a png");
    assert_eq!(images.assign(&catalogue, NIGHT, &original).unwrap(), Err(Problem::NotPng));
    assert_eq!(images.assign(&catalogue, "map/x/background_day.png", &file).unwrap(), Err(Problem::UnknownName));

    images.reset(&catalogue, DAY).unwrap();
    assert!(images.custom_targets().is_empty());
    assert!(images.reset(&catalogue, "../outside.png").is_err());
    assert!(images.thumbnail(&catalogue, "../../secret.png", false).is_err());
}

#[test]
fn the_overview_groups_maps_by_their_ids_letters_and_images_by_folder() {
    let (data, images, catalogue) = prepared();
    let file = data.path().join("night.png");
    write(&file, &png_file(1536, 1280, "2x"));
    assert_eq!(images.assign(&catalogue, NIGHT, &file).unwrap(), Ok(2));
    let overview = images.overview(&catalogue, true);
    assert_eq!(overview.custom_count, 1);
    let keys: Vec<_> = overview.groups.iter().map(|g| (g.kind, g.key.as_str(), g.name.as_str())).collect();
    assert_eq!(keys, [("maps", "betl", "Arena"), ("maps", "mayo", "Bosque Nativo"), ("images", "texture/etcdat", "")]);
    let mayo = &overview.groups[1].entries[0];
    assert_eq!((mayo.id.as_str(), mayo.name.as_str()), ("mayo01", "Bosque Nativo"));
    let variants: Vec<_> = mayo.files.iter().map(|f| (f.variant.as_str(), f.custom_scale)).collect();
    assert_eq!(variants, [("day", None), ("night", Some(2))]);
    let icons = &overview.groups[2].entries[0];
    assert_eq!((icons.id.as_str(), icons.files[0].variant.as_str()), ("texture/etcdat/fi_info", "12"));
}

#[test]
fn an_export_is_a_new_folder_with_the_originals_and_a_guide() {
    let (data, images, catalogue) = prepared();
    let first = images.export(&catalogue, data.path()).unwrap();
    let second = images.export(&catalogue, data.path()).unwrap();
    assert_eq!(first, data.path().join(EXPORT_FOLDER));
    assert_eq!(second, data.path().join(format!("{EXPORT_FOLDER}-2")));
    assert_eq!(fs::read(first.join(ICONS)).unwrap(), png_file(256, 64, ICONS));
    assert!(fs::read_to_string(first.join(README_FILE)).unwrap().contains("map/"));
    assert!(fs::read_to_string(first.join(EXPORTED_MAPS_FILE)).unwrap().contains("Bosque Nativo"));
    let reloaded = images.inspect(&catalogue, &mut Picked::open(&first).unwrap()).unwrap();
    assert_eq!((reloaded.matched.len(), reloaded.unchanged, reloaded.ignored), (0, 5, 2));
}

#[test]
fn the_export_counts_as_prepared_for_one_stamp_only() {
    let (_data, images, _catalogue) = prepared();
    assert!(!images.prepared("1.0|disc"));
    images.mark_prepared("1.0|disc").unwrap();
    assert!(images.prepared("1.0|disc"));
    assert!(!images.prepared("1.1|disc"));
}
