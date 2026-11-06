//! "Probar evento": runs an event of the map shown in the connected game. The editor works out
//! the story writes that get past the section's early ends (`Mapper::test_plan`), asks first,
//! then reads the story, writes, reloads the map, puts the player where the event runs and
//! presses confirm when it needs it; a moment later it reads the story again and keeps what
//! changed for the story panel. Nothing waits: each frame looks at what the link said.

use std::time::{Duration, Instant};

use dwe_story::Event;
use dwe_story::needs::Need;
use eframe::egui;
use mapper_core::{Message, TestPlan, text};
use mapper_live::{Command, StoryWrite};

use crate::app::MapperApp;

/// How long the event runs before the story is read again.
const RUN_FOR: Duration = Duration::from_millis(1500);
/// How long a story read may take before the test gives up on it.
const READ_WAIT: Duration = Duration::from_secs(5);
/// How long the game may take to load the map again (a debug build takes seconds).
const RELOAD_WAIT: Duration = Duration::from_secs(60);
/// How long after the reload confirm is pressed, at the soonest: the map's entry section runs
/// first (it spawns the NPCs), and the press waits for it to end.
const PRESS_AFTER: Duration = Duration::from_millis(600);
/// How long the press waits for a story script to end before it goes anyway.
const BUSY_WAIT: Duration = Duration::from_secs(10);
/// The confirm press: the key and how many ticks it is held.
const PRESS_KEY: &str = "space";
const PRESS_TICKS: u64 = 6;

/// The test of an event, step by step.
#[derive(Clone, Debug, Default)]
enum Stage {
    #[default]
    Idle,
    /// The question, with what the test will do.
    Asking { plan: TestPlan },
    /// Waiting for the story as it is before the test.
    ReadingBefore {
        plan: TestPlan,
        reads: u64,
        since: Instant,
    },
    /// The story is written and the map loads again; then the player goes to the event.
    Reloading {
        before: Snapshot,
        plan: TestPlan,
        reloads: u64,
        failures: u64,
        since: Instant,
    },
    /// The player is at the event; confirm is pressed from `at`, once no script runs.
    Pressing { before: Snapshot, at: Instant },
    /// The event runs.
    Running { before: Snapshot, until: Instant },
    /// Waiting for the story after the test.
    ReadingAfter {
        before: Snapshot,
        reads: u64,
        since: Instant,
    },
}

/// The story's triggers on and its variables not 0.
#[derive(Clone, Debug, Default, PartialEq)]
struct Snapshot {
    triggers: Vec<u32>,
    variables: Vec<(u32, i64)>,
}

/// What a test changed in the story.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct StoryDiff {
    /// The triggers that changed, with whether they are on now.
    pub triggers: Vec<(u32, bool)>,
    /// The variables that changed: before and after.
    pub variables: Vec<(u32, i64, i64)>,
    /// The event tested, said.
    pub event: String,
}

impl StoryDiff {
    fn between(before: &Snapshot, after: &Snapshot) -> Self {
        let mut triggers: Vec<(u32, bool)> = after
            .triggers
            .iter()
            .filter(|trigger| !before.triggers.contains(trigger))
            .map(|&trigger| (trigger, true))
            .chain(
                before
                    .triggers
                    .iter()
                    .filter(|trigger| !after.triggers.contains(trigger))
                    .map(|&trigger| (trigger, false)),
            )
            .collect();
        triggers.sort_unstable();
        let value = |list: &[(u32, i64)], number: u32| {
            list.iter()
                .find(|(known, _)| *known == number)
                .map_or(0, |(_, value)| *value)
        };
        let mut numbers: Vec<u32> = before
            .variables
            .iter()
            .chain(&after.variables)
            .map(|(number, _)| *number)
            .collect();
        numbers.sort_unstable();
        numbers.dedup();
        let variables = numbers
            .into_iter()
            .map(|number| {
                (
                    number,
                    value(&before.variables, number),
                    value(&after.variables, number),
                )
            })
            .filter(|(_, before, after)| before != after)
            .collect();
        Self {
            triggers,
            variables,
            event: String::new(),
        }
    }

    pub fn is_empty(&self) -> bool {
        self.triggers.is_empty() && self.variables.is_empty()
    }
}

/// The test of an event and the last one's result.
#[derive(Debug, Default)]
pub struct Probe {
    stage: Stage,
    /// The event of the test running or asked about, said.
    event: String,
    pub diff: Option<StoryDiff>,
}

impl Probe {
    /// Whether a test is running.
    pub fn is_running(&self) -> bool {
        !matches!(self.stage, Stage::Idle | Stage::Asking { .. })
    }
}

/// Asks before testing an event of the map shown.
pub fn ask(app: &mut MapperApp, event: &Event) {
    let plan = app.mapper.test_plan(event);
    app.probe.event = format!(
        "{} · {}",
        text::event_kind(event.kind),
        text::section_name(event.script, event.section)
    );
    app.probe.stage = Stage::Asking { plan };
}

/// The question: what the test writes, what it cannot, and where the player goes.
pub fn show(app: &mut MapperApp, ctx: &egui::Context) {
    let Stage::Asking { plan, .. } = &app.probe.stage else {
        return;
    };
    let names = crate::events::names(&app.mapper);
    let mut open = true;
    let mut run = false;
    let mut cancel = false;
    egui::Window::new(text::TEST_TITLE)
        .open(&mut open)
        .collapsible(false)
        .resizable(false)
        .show(ctx, |ui| {
            ui.strong(&app.probe.event);
            ui.add_space(4.0);
            if app.mapper.is_dirty() {
                ui.colored_label(egui::Color32::YELLOW, text::TEST_UNSAVED);
            }
            ui.label(text::TEST_WRITES);
            if plan.to_selected {
                ui.weak(text::TEST_TO_SELECTED);
            }
            if plan.writes.is_empty() {
                ui.weak(text::TEST_NO_WRITES);
            }
            for need in &plan.writes {
                let line = match (*need, &names) {
                    (Need::Trigger { number, on }, Some(names)) => {
                        text::need_trigger(&names.trigger(number), on)
                    }
                    (Need::Var { number, value }, Some(names)) => {
                        text::need_var(&names.var(number), value)
                    }
                    (need, None) => format!("{need:?}"),
                };
                if story_write(*need).is_some() {
                    ui.monospace(format!("• {line}"));
                } else {
                    ui.colored_label(
                        egui::Color32::YELLOW,
                        format!("• {line} ({})", text::TEST_SKIPPED),
                    );
                }
            }
            if !plan.others.is_empty() {
                ui.add_space(4.0);
                ui.colored_label(egui::Color32::YELLOW, text::TEST_OTHERS);
                for other in &plan.others {
                    ui.monospace(format!("• {other}"));
                }
            }
            ui.add_space(4.0);
            match &plan.place {
                Some(place) => {
                    ui.label(text::TEST_PLACE);
                    ui.monospace(place);
                }
                None => {
                    ui.weak(text::TEST_NO_PLACE);
                }
            }
            if plan.press {
                ui.label(text::TEST_PRESS);
            }
            ui.add_space(6.0);
            ui.horizontal(|ui| {
                let connected = app.live.is_connected();
                if ui
                    .add_enabled(connected, egui::Button::new(text::TEST_RUN))
                    .on_disabled_hover_text(text::TEST_EVENT_OFFLINE)
                    .clicked()
                {
                    run = true;
                }
                if ui.button(text::TEST_CANCEL).clicked() {
                    cancel = true;
                }
            });
        });
    if run {
        start(app);
    } else if cancel || !open {
        app.probe.stage = Stage::Idle;
    }
}

/// A need as the link's write, when its number and value fit.
fn story_write(need: Need) -> Option<StoryWrite> {
    match need {
        Need::Trigger { number, on } => Some(StoryWrite::Trigger {
            number: u32::try_from(number).ok()?,
            on,
        }),
        Need::Var { number, value } => Some(StoryWrite::Variable {
            number: u32::try_from(number).ok()?,
            value: u8::try_from(value).ok()?,
        }),
    }
}

/// The first step: read the story as it is.
fn start(app: &mut MapperApp) {
    let Stage::Asking { plan, .. } = std::mem::take(&mut app.probe.stage) else {
        return;
    };
    // The game refuses story writes while a script runs.
    if app.live.view.as_ref().is_some_and(|view| view.busy) {
        app.mapper
            .messages
            .push(Message::Error(text::TEST_BUSY.to_owned()));
        return;
    }
    app.live.send(Command::ReadStory);
    app.probe.stage = Stage::ReadingBefore {
        plan,
        reads: app.live.story_reads,
        since: Instant::now(),
    };
    app.mapper
        .messages
        .push(Message::Info(text::TEST_RUNNING.to_owned()));
}

fn snapshot(app: &MapperApp) -> Snapshot {
    app.live
        .story
        .as_ref()
        .map_or_else(Snapshot::default, |story| Snapshot {
            triggers: story.triggers.clone(),
            variables: story.variables.clone(),
        })
}

/// Each frame: moves the test on when the link has answered (or gives up when it does not).
pub fn tick(app: &mut MapperApp) {
    if !app.probe.is_running() {
        return;
    }
    if !app.live.is_connected() {
        app.probe.stage = Stage::Idle;
        return;
    }
    let now = Instant::now();
    let stage = std::mem::take(&mut app.probe.stage);
    app.probe.stage = match stage {
        Stage::ReadingBefore { plan, reads, since } => {
            if app.live.story_reads > reads {
                let before = snapshot(app);
                let (reloads, failures) = (app.live.reloads, app.live.failures);
                write_place_and_reload(app, &plan);
                Stage::Reloading {
                    before,
                    plan,
                    reloads,
                    failures,
                    since: now,
                }
            } else if now - since > READ_WAIT {
                Stage::Idle
            } else {
                Stage::ReadingBefore { plan, reads, since }
            }
        }
        Stage::Reloading {
            before,
            plan,
            reloads,
            failures,
            since,
        } => {
            if app.live.failures > failures {
                // The game refused a write or the reload: its error is in the messages.
                Stage::Idle
            } else if app.live.reloads > reloads {
                if plan.press {
                    Stage::Pressing {
                        before,
                        at: now + PRESS_AFTER,
                    }
                } else {
                    Stage::Running {
                        before,
                        until: now + RUN_FOR,
                    }
                }
            } else if now - since > RELOAD_WAIT {
                app.mapper
                    .messages
                    .push(Message::Error(text::TEST_NO_RELOAD.to_owned()));
                Stage::Idle
            } else {
                Stage::Reloading {
                    before,
                    plan,
                    reloads,
                    failures,
                    since,
                }
            }
        }
        Stage::Pressing { before, at }
            if now >= at
                && (now >= at + BUSY_WAIT || !app.live.view.as_ref().is_some_and(|v| v.busy)) =>
        {
            app.live.send(Command::Press {
                keys: vec![PRESS_KEY.to_owned()],
                ticks: PRESS_TICKS,
            });
            Stage::Running {
                before,
                until: now + RUN_FOR,
            }
        }
        Stage::Running { before, until } if now >= until => {
            app.live.send(Command::ReadStory);
            Stage::ReadingAfter {
                before,
                reads: app.live.story_reads,
                since: now,
            }
        }
        Stage::ReadingAfter {
            before,
            reads,
            since,
        } => {
            if app.live.story_reads > reads {
                let mut diff = StoryDiff::between(&before, &snapshot(app));
                diff.event = app.probe.event.clone();
                app.probe.diff = Some(diff);
                Stage::Idle
            } else if now - since > READ_WAIT {
                Stage::Idle
            } else {
                Stage::ReadingAfter {
                    before,
                    reads,
                    since,
                }
            }
        }
        other => other,
    };
}

/// Writes the story, puts the player at the event and reloads the map, which reads the saved
/// story and texts again and enters where the player stands (a place after the reload would be
/// undone by the map's entry).
fn write_place_and_reload(app: &MapperApp, plan: &TestPlan) {
    let live = &app.live;
    for write in plan.writes.iter().filter_map(|&need| story_write(need)) {
        live.send(Command::StorySet(write));
    }
    if let Some(place) = &plan.place {
        live.send(Command::Place(place.clone()));
    }
    live.reload();
    if live.view.as_ref().is_some_and(|view| view.paused) {
        live.send(Command::Resume);
    }
}

/// The last test's changes, for the story panel.
pub fn diff_lines(app: &MapperApp, ui: &mut egui::Ui) {
    if app.probe.is_running() {
        ui.colored_label(egui::Color32::YELLOW, text::TEST_RUNNING);
    }
    let Some(diff) = &app.probe.diff else {
        return;
    };
    let names = &app.mapper.story_names;
    ui.add_space(4.0);
    ui.strong(text::TEST_DIFF);
    ui.weak(&diff.event);
    if diff.is_empty() {
        ui.weak(text::TEST_DIFF_NONE);
    }
    for &(trigger, on) in &diff.triggers {
        let name = text::named(trigger, names.trigger(trigger));
        let color = if on {
            egui::Color32::LIGHT_GREEN
        } else {
            egui::Color32::LIGHT_RED
        };
        ui.label(
            egui::RichText::new(text::trigger_change(&name, on))
                .monospace()
                .color(color),
        );
    }
    for &(variable, before, after) in &diff.variables {
        let name = text::named(variable, names.variable(variable));
        ui.monospace(text::var_change(&name, before, after));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_diff_lists_what_changed() {
        let before = Snapshot {
            triggers: vec![1, 5],
            variables: vec![(2, 3), (4, 1)],
        };
        let after = Snapshot {
            triggers: vec![5, 9],
            variables: vec![(2, 3), (7, 2)],
        };
        let diff = StoryDiff::between(&before, &after);
        assert_eq!(diff.triggers, vec![(1, false), (9, true)]);
        assert_eq!(diff.variables, vec![(4, 1, 0), (7, 0, 2)]);
        assert!(StoryDiff::between(&before, &before).is_empty());
    }

    #[test]
    fn writes_that_do_not_fit_are_left_out() {
        assert_eq!(
            story_write(Need::Var {
                number: 3,
                value: 300
            }),
            None
        );
        assert_eq!(
            story_write(Need::Trigger {
                number: 4,
                on: true
            }),
            Some(StoryWrite::Trigger {
                number: 4,
                on: true
            })
        );
    }
}
