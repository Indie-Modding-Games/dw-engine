//! The Rust map camera against the engine's: tests/data/camera_vectors.expected is what
//! packages/engine/render/src/map_camera.cpp printed for tests/data/camera_vectors.txt
//! (scripts/mapper.sh camera-vectors writes it again). Both use 32-bit floats; the tolerances
//! only absorb the order of their operations. With `DWE_CAMERA_VECTORS=<path>` set, the pair
//! `<path>.txt` / `<path>.expected` is checked as well: a local run over the cameras of the
//! player's own maps, which never go in the repository.

use glam::{IVec2, IVec3, Vec2, Vec3};

use dwe_map::{MapCamera, MapCameraDesc};

const INPUT: &str = include_str!("data/camera_vectors.txt");
const EXPECTED: &str = include_str!("data/camera_vectors.expected");

/// Background pixels.
const PIXEL_TOLERANCE: f32 = 0.01;
/// World units, for points picked far from the camera.
const WORLD_TOLERANCE: f32 = 0.1;

fn numbers(words: &[&str]) -> Vec<f32> {
    words.iter().map(|word| word.parse().unwrap()).collect()
}

fn close(a: Vec3, b: Vec3, tolerance: f32) -> bool {
    (a - b).abs().max_element() <= tolerance
}

#[test]
fn matches_the_engine_camera() {
    assert_eq!(check(INPUT, EXPECTED), 31);
}

#[test]
fn matches_the_engine_camera_on_local_vectors() {
    let Some(path) = std::env::var_os("DWE_CAMERA_VECTORS") else {
        return;
    };
    let path = std::path::PathBuf::from(path);
    let read = |extension: &str| std::fs::read_to_string(path.with_extension(extension)).unwrap();
    let checked = check(&read("txt"), &read("expected"));
    eprintln!("{checked} local vectors checked");
}

/// Runs the commands of an input and compares each with its expected line; returns how many.
fn check(input: &str, expected: &str) -> usize {
    let commands = input
        .lines()
        .map(str::trim)
        .filter(|line| !line.is_empty() && !line.starts_with('#'));
    let mut expected = expected.lines();
    let mut camera: Option<MapCamera> = None;
    let mut checked = 0;
    for command in commands {
        let words: Vec<&str> = command.split_whitespace().collect();
        let result = expected.next().expect("one result per command");
        let results: Vec<&str> = result.split_whitespace().collect();
        match words[0] {
            "map" => {
                let n = numbers(&words[2..]);
                let int = |i: usize| n[i] as i32;
                let mut map = MapCamera::new(MapCameraDesc::from_original(
                    IVec3::new(int(0), int(1), int(2)),
                    IVec3::new(int(3), int(4), int(5)),
                    int(6),
                    IVec2::new(int(7), int(8)),
                ));
                map.set_viewport(Vec2::new(n[9], n[10]));
                map.follow(Vec3::new(n[11], n[12], n[13]));
                let want = numbers(&[results[3], results[4], results[6]]);
                let got = Vec3::new(map.scroll().x, map.scroll().y, map.scale());
                assert!(
                    close(got, Vec3::from_slice(&want), PIXEL_TOLERANCE),
                    "{command}: {got} != {result}"
                );
                camera = Some(map);
            }
            "project" => {
                let map = camera.as_ref().unwrap();
                let n = numbers(&words[1..]);
                let point = map.project_to_background(Vec3::new(n[0], n[1], n[2]));
                match (point, results[1]) {
                    (None, "none") => {}
                    (Some(point), _) => {
                        let want = numbers(&results[1..]);
                        let pixel = Vec2::new(want[0], want[1]);
                        assert!(
                            (point.pixel - pixel).abs().max_element() <= PIXEL_TOLERANCE,
                            "{command}: {} != {result}",
                            point.pixel
                        );
                        assert!(
                            (point.view_depth - want[2]).abs() <= want[2] * 1e-5,
                            "{command}: depth {} != {result}",
                            point.view_depth
                        );
                    }
                    (None, _) => panic!("{command}: none != {result}"),
                }
            }
            "pick" => {
                let map = camera.as_ref().unwrap();
                let n = numbers(&words[1..]);
                let world = map.pick_ground(Vec2::new(n[0], n[1]), n[2]);
                match (world, results[1]) {
                    (None, "none") => {}
                    (Some(world), _) => {
                        let want = Vec3::from_slice(&numbers(&results[1..]));
                        assert!(
                            close(world, want, WORLD_TOLERANCE),
                            "{command}: {world} != {result}"
                        );
                    }
                    (None, _) => panic!("{command}: none != {result}"),
                }
            }
            other => panic!("unknown command {other}"),
        }
        checked += 1;
    }
    assert!(expected.next().is_none(), "more results than commands");
    checked
}
