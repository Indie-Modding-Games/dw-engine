//! The timeline of a section: what runs when, how long the moves last, what waits for what, and
//! the code "play from here" sends.

use std::collections::BTreeMap;

use dwe_story::ast::{Block, StmtKind};
use dwe_story::edit::StmtPath;
use dwe_story::parse::parse_block;
use dwe_story::sequence::{
    self, ANIMATION_FRAMES, Hold, PARTNER, PLAYER, Pose, Sequence, Stage, TEXT_FRAMES, Track,
};
use glam::Vec3;

/// A scene: a guard, a walk the camera pans after, a line, a wait for the walker, a turn, a
/// delay, an NPC's two walks (the second takes over the first), a routine and a map change.
const SCENE: &str = r#"
if story.trigger(5) then
  return ENDED
end
story.set_trigger(7)
story.entity_walk_to(PLAYER, 200, -100, 0, 0)
story.move_camera_to(30, 200, -100)
story.show_textbox("story_900.text.0000")
story.wait_for_entity(PLAYER)
story.entity_look_at_entity(PLAYER, 12)
story.delay(15)
story.entity_walk_to(12, 0, 0, 1, 0)
story.delay(2)
story.entity_walk_to(12, 300, 0, 0, 0)
routines.talk()
return story.warp_to(3, 1, 255)
"#;

const TALK: &str = r#"
story.play_animation(12, 3)
story.play_sound(1, 2)
"#;

fn stage() -> Stage {
    let mut poses = BTreeMap::new();
    poses.insert(PLAYER, Pose::at(Vec3::ZERO));
    poses.insert(PARTNER, Pose::at(Vec3::new(-100.0, 0.0, 0.0)));
    poses.insert(12, Pose::at(Vec3::new(400.0, 0.0, 0.0)));
    Stage {
        poses,
        camera: Vec3::ZERO,
        ..Stage::default()
    }
}

fn derive(body: &Block, follow: Option<&StmtPath>) -> Sequence {
    let talk = parse_block(TALK).unwrap();
    let routines = lookup(&talk);
    Sequence::derive(body, &routines, &stage(), follow)
}

/// The routines of the scene's file: `talk`.
fn lookup<'b>(talk: &'b Block) -> impl Fn(&str) -> Option<&'b Block> + 'b {
    move |name| (name == "talk").then_some(talk)
}

fn cue<'a>(sequence: &'a Sequence, command: &str, nth: usize) -> &'a sequence::Cue {
    sequence
        .cues
        .iter()
        .filter(|cue| cue.command == command)
        .nth(nth)
        .unwrap_or_else(|| panic!("no {command} #{nth}"))
}

#[test]
fn a_scene_runs_its_moves_beside_the_section_and_waits_where_the_interpreter_waits() {
    let body = parse_block(SCENE).unwrap();
    let scene = derive(&body, None);
    // The guard only ends the section: the timeline falls through it.
    assert_eq!(cue(&scene, "set_trigger", 0).start, 0);
    assert_eq!(cue(&scene, "set_trigger", 0).track, Track::Story);
    // 200 x 100 units at the player's walk, 20 a frame: 12 frames, not estimated.
    let walk = cue(&scene, "entity_walk_to", 0);
    assert_eq!((walk.start, walk.frames), (0, 12));
    assert_eq!(walk.track, Track::Actor(PLAYER));
    assert!(!walk.estimated);
    let (from, to) = walk.way.unwrap();
    assert_eq!(from, Vec3::ZERO);
    assert_eq!(to, Vec3::new(200.0, 0.0, 100.0));
    // The pan starts with the walk and lasts its frames operand.
    let pan = cue(&scene, "move_camera_to", 0);
    assert_eq!((pan.start, pan.frames, pan.track), (0, 30, Track::Camera));
    // The text waits for the player, an estimated time.
    let text = cue(&scene, "show_textbox", 0);
    assert_eq!((text.start, text.hold), (0, Hold::Player));
    assert!(text.estimated);
    // The walk ended during the text: the wait for the player takes nothing.
    let wait = cue(&scene, "wait_for_entity", 0);
    assert_eq!((wait.start, wait.frames), (TEXT_FRAMES, 0));
    // The turn to the NPC (at +x from (200, 100)) and the delay start together.
    let look = cue(&scene, "entity_look_at_entity", 0);
    assert_eq!(look.start, TEXT_FRAMES);
    assert!(look.facing.is_some());
    let delay = cue(&scene, "delay", 0);
    assert_eq!((delay.start, delay.frames), (TEXT_FRAMES, 15));
    // The NPC runs (animation 1, estimated: an NPC's speed is the player's) and is taken over
    // two frames later by a walk from where it got to.
    let run = cue(&scene, "entity_walk_to", 1);
    assert_eq!(run.start, TEXT_FRAMES + 15);
    assert_eq!(run.frames, 2);
    assert!(run.cut && run.estimated);
    let then = cue(&scene, "entity_walk_to", 2);
    assert_eq!(then.start, TEXT_FRAMES + 17);
    assert_eq!(then.way.unwrap().0, Vec3::new(300.0, 0.0, 0.0));
    // The routine's commands show under the call; the animation is estimated.
    let animation = cue(&scene, "play_animation", 0);
    assert_eq!(animation.path, StmtPath::top(11));
    assert_eq!(animation.routine.as_deref(), Some("talk"));
    assert_eq!(
        (animation.frames, animation.track),
        (ANIMATION_FRAMES, Track::Actor(12))
    );
    assert_eq!(cue(&scene, "play_sound", 0).track, Track::Audio);
    // The map change ends it.
    assert_eq!(cue(&scene, "warp_to", 0).hold, Hold::Ends);
    assert!(scene.ends);
    assert_eq!(
        scene.tracks(),
        vec![
            Track::Actor(PLAYER),
            Track::Actor(12),
            Track::Camera,
            Track::Text,
            Track::Audio,
            Track::Wait,
            Track::Story
        ]
    );
}

#[test]
fn the_scrubber_puts_everyone_where_the_moves_have_them() {
    let body = parse_block(SCENE).unwrap();
    let scene = derive(&body, None);
    let player = |frame: f32| scene.pose_at(Track::Actor(PLAYER), frame).unwrap();
    assert_eq!(player(0.0).position, Vec3::ZERO);
    assert!((player(6.0).position - Vec3::new(100.0, 0.0, 50.0)).length() < 1e-3);
    assert_eq!(player(20.0).position, Vec3::new(200.0, 0.0, 100.0));
    // Walking +x and +z faces between the two.
    assert!((player(6.0).yaw - (200.0f32).atan2(100.0)).abs() < 1e-4);
    let camera = scene.pose_at(Track::Camera, 15.0).unwrap();
    assert!((camera.position - Vec3::new(100.0, 0.0, 50.0)).length() < 1e-3);
    // The partner never moves: it stays where it stood.
    assert_eq!(
        scene
            .pose_at(Track::Actor(PARTNER), 100.0)
            .unwrap()
            .position,
        Vec3::new(-100.0, 0.0, 0.0)
    );
    assert_eq!(scene.path_at(TEXT_FRAMES + 1), Some(&StmtPath::top(7)));
}

#[test]
fn an_if_follows_the_arm_asked_for() {
    let body = parse_block(
        "if story.var(1) == 0 then\n  story.delay(5)\nelse\n  story.delay(9)\n  story.delay(1)\nend\n",
    )
    .unwrap();
    let longest = derive(&body, None);
    assert_eq!(longest.length, 10);
    let first = StmtPath::top(0).child(0, 0);
    let asked = derive(&body, Some(&first));
    assert_eq!(asked.length, 5);
    assert_eq!(asked.cues[0].path, first);
}

#[test]
fn lines_of_a_running_section_find_its_cue() {
    let body = parse_block(SCENE).unwrap();
    let scene = derive(&body, None);
    // `story.delay(15)` is on line 11 of the code; the innermost line wins.
    let at = scene.cue_at_lines(&[11, 3]).unwrap();
    assert_eq!(at.command, "delay");
    assert!(scene.cue_at_lines(&[999]).is_none());
}

#[test]
fn play_from_here_places_everyone_keeps_the_state_and_runs_the_rest() {
    let body = parse_block(SCENE).unwrap();
    let scene = derive(&body, None);
    let at = StmtPath::top(8);
    let (code, origins) = sequence::play_from(&body, &at, &scene);
    assert_eq!(code.len(), origins.len());
    let written: Vec<String> = code
        .iter()
        .map(|stmt| {
            dwe_story::write::block_text(&vec![stmt.clone()])
                .trim()
                .to_owned()
        })
        .collect();
    // The player is placed where the walk left it, facing the NPC, before the rest runs.
    assert_eq!(
        written[0],
        "story.entity_move_to(story.PLAYER, 200, -100, 1, 0)"
    );
    assert!(
        written
            .iter()
            .any(|line| line == "story.move_camera_to(1, 200, -100)")
    );
    assert!(
        written
            .iter()
            .any(|line| line.starts_with("story.entity_set_rotation(story.PLAYER"))
    );
    // The guard and the trigger stay; the walk, the pan and the text before it go.
    let kept: Vec<_> = origins.iter().flatten().collect();
    assert_eq!(kept[0], &StmtPath::top(0));
    assert_eq!(kept[1], &StmtPath::top(1));
    assert_eq!(kept[2], &at);
    assert!(!written.iter().any(|line| line.contains("show_textbox")));
    assert!(matches!(code.last().unwrap().kind, StmtKind::Return(_)));
}

#[test]
fn play_from_inside_an_if_runs_the_rest_of_its_arm_and_what_follows() {
    let body = parse_block(
        "story.set_var(3, 1)\nif story.var(1) == 0 then\n  story.delay(5)\n  story.set_trigger(2)\nend\nstory.delay(7)\n",
    )
    .unwrap();
    let at = StmtPath::top(1).child(0, 1);
    let scene = derive(&body, Some(&at));
    let (code, origins) = sequence::play_from(&body, &at, &scene);
    let origins: Vec<_> = origins.into_iter().flatten().collect();
    assert_eq!(origins, vec![StmtPath::top(0), at, StmtPath::top(2)]);
    assert_eq!(code.len(), 3);
}
