//! The section editor: a window with the code of the section open (or of a routine of its file)
//! as a list of statements in words to select, change, move and delete, the fields of the
//! statement selected, a palette of commands to add, and the Lua itself in a tab. Every change
//! goes to the model as `Action::EditStory` (one undo step), into the target pack's draft of the
//! script.

use dwe_story::commands::{self, Flow, OperandKind};
use dwe_story::describe::Names;
use dwe_story::edit::{self, SimpleCondition, StmtPath};
use dwe_story::parse::{parse_chunk, parse_expr};
use dwe_story::write::{block_text, expr_text, is_link};
use dwe_story::{BinOp, Block, Event, Expr, Stmt, StmtKind};
use eframe::egui;
use mapper_core::{Action, Mapper, OpenSection, StoryEdit, text};

use crate::app::MapperApp;
use crate::inspector::grouped;

/// How many map ids the map picker lists at once.
const MOST_LISTED_MAPS: usize = 200;
/// The longest dialogue line a statement's line shows.
const LINE_PREVIEW: usize = 60;
/// The indent of a nested block in the list, in pixels.
const INDENT: f32 = 16.0;
/// The highest trigger or variable number the fields offer.
const MOST_STORY_NUMBER: i64 = u16::MAX as i64;

/// The editor's tabs.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
enum Tab {
    #[default]
    Structure,
    Lua,
}

/// A text field of the statement selected.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Field {
    Operand(usize),
    Condition(usize),
}

/// What the section editor keeps between frames.
#[derive(Debug, Default)]
pub struct SectionUi {
    tab: Tab,
    palette_filter: String,
    /// Whether the palette inserts into the selected statement's block.
    inside: bool,
    /// The Lua tab's text, and the code it was loaded from.
    lua: String,
    lua_base: String,
    /// A field being typed: the statement, the field and the text so far.
    typing: Option<(StmtPath, Field, String)>,
    /// The dialogue line being typed: its key and text.
    line: Option<(String, String)>,
    /// The text that narrows the map picker.
    map_filter: String,
}

impl SectionUi {
    /// The text a field shows: what is being typed in it, or its value.
    fn typed(&self, path: &StmtPath, field: Field, current: &str) -> String {
        match &self.typing {
            Some((known, typed_field, typed)) if known == path && *typed_field == field => {
                typed.clone()
            }
            _ => current.to_owned(),
        }
    }
}

/// What the editor's parts read.
struct Context<'a> {
    mapper: &'a Mapper,
    names: Names<'a>,
    open: &'a OpenSection,
}

/// The editor's window, while a section is open.
pub fn show(app: &mut MapperApp, ctx: &egui::Context) {
    let Some(open) = app.mapper.section.clone() else {
        return;
    };
    let mut actions = Vec::new();
    let mut test = None;
    let mut window_open = true;
    let title = format!(
        "{} · {}",
        text::SECTION_EDITOR,
        text::section_name(open.script, open.section)
    );
    let connected = app.live.is_connected();
    let mut window = egui::Window::new(title)
        .id(egui::Id::unique("section_editor"))
        .open(&mut window_open)
        .default_pos([250.0, 110.0])
        .default_size([620.0, 700.0]);
    // Above the timeline while it shows.
    if app.mapper.timeline.shown
        && let Some(canvas) = app.canvas_rect
    {
        let screen = ctx.content_rect();
        window = window.constrain_to(egui::Rect::from_min_max(
            screen.min,
            egui::pos2(screen.max.x, canvas.bottom()),
        ));
    }
    window.show(ctx, |ui| {
        let mapper = &app.mapper;
        let Some(names) = crate::events::names(mapper) else {
            return;
        };
        let context = Context {
            mapper,
            names,
            open: &open,
        };
        let state = &mut app.section_ui;
        header(&context, ui, connected, &mut actions, &mut test);
        ui.horizontal(|ui| {
            for (tab, label) in [
                (Tab::Structure, text::TAB_STRUCTURE),
                (Tab::Lua, text::TAB_LUA),
            ] {
                if ui.selectable_label(state.tab == tab, label).clicked() {
                    state.tab = tab;
                }
            }
        });
        ui.separator();
        match state.tab {
            Tab::Structure => structure(&context, state, ui, &mut actions),
            Tab::Lua => lua(&context, state, ui, &mut actions),
        }
    });
    if !window_open {
        actions.push(Action::CloseSection);
    }
    for action in actions {
        app.apply(action);
    }
    if let Some(event) = test {
        crate::probe::ask(app, &event);
    }
}

/// The events of the map shown that run the open section.
fn events_of<'a>(context: &Context<'a>) -> Vec<&'a Event> {
    context
        .mapper
        .events()
        .iter()
        .filter(|event| {
            event.script == context.open.script && event.section == context.open.section
        })
        .collect()
}

/// Who gives the section, what runs it, and the buttons for the whole section.
fn header(
    context: &Context<'_>,
    ui: &mut egui::Ui,
    connected: bool,
    actions: &mut Vec<Action>,
    test: &mut Option<Event>,
) {
    let mapper = context.mapper;
    let origin = mapper.open_origin();
    let events = events_of(context);
    let kinds: Vec<String> = events
        .iter()
        .map(|event| text::event_kind(event.kind))
        .collect();
    if mapper.current_target().is_none() {
        ui.colored_label(egui::Color32::YELLOW, text::SECTION_NO_TARGET);
    }
    ui.horizontal_wrapped(|ui| {
        ui.strong(text::origin(origin.as_deref()));
        if !kinds.is_empty() {
            ui.label(format!("· {}", kinds.join(", ")));
        }
    });
    ui.horizontal_wrapped(|ui| {
        if let Some(routine) = &context.open.routine {
            ui.monospace(format!("{} {routine}", text::ROUTINE));
            if ui.button(text::BACK_TO_SECTION).clicked() {
                actions.push(Action::OpenRoutine(None));
            }
        }
        if origin.is_some()
            && ui
                .button(text::REVERT_SECTION)
                .on_hover_text(text::REVERT_SECTION_HINT)
                .clicked()
        {
            actions.push(Action::EditStory(StoryEdit::Revert));
        }
        let shown = mapper.timeline.shown;
        if ui
            .selectable_label(shown, text::TIMELINE)
            .on_hover_text(text::TIMELINE_HINT)
            .clicked()
        {
            actions.push(Action::ShowTimeline(!shown));
        }
        if let Some(event) = events.first()
            && ui
                .add_enabled(connected, egui::Button::new(text::TEST_TITLE))
                .on_disabled_hover_text(text::TEST_EVENT_OFFLINE)
                .clicked()
        {
            *test = Some((*event).clone());
        }
    });
}

/// A dialogue line, short, in quotes; the key when there is no line.
fn quoted(line: Option<&str>, key: &str) -> String {
    let Some(line) = line else {
        return key.to_owned();
    };
    let line = line.replace('\n', " ");
    let short: String = line.chars().take(LINE_PREVIEW).collect();
    let more = if line.chars().count() > LINE_PREVIEW {
        "…"
    } else {
        ""
    };
    format!("«{short}{more}»")
}

/// A statement in one line: as the descriptions say it, but a text box shows its line as edited.
fn stmt_line(context: &Context<'_>, stmt: &Stmt) -> String {
    if let StmtKind::Call(call) = &stmt.kind
        && let Some(("show_textbox", [Expr::Str(key)])) = call.story_call_name()
    {
        let label = commands::command("show_textbox").map_or("show_textbox", |c| c.label);
        return format!("{label}: {}", quoted(context.mapper.text_of(key), key));
    }
    context.names.stmt(stmt)
}

/// The routine a statement runs (a call, a `return` into it or a call that may end the script).
fn routine_of(stmt: &Stmt) -> Option<&str> {
    if let Some(name) = is_link(&stmt.kind) {
        return Some(name);
    }
    match &stmt.kind {
        StmtKind::Call(call) => call.routine_call_name(),
        StmtKind::Return(values) => match values.as_slice() {
            [call] => call.routine_call_name(),
            _ => None,
        },
        _ => None,
    }
}

/// A line of the statement list.
#[derive(Clone, Debug, PartialEq)]
struct Row {
    path: StmtPath,
    depth: usize,
    text: String,
    kind: RowKind,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum RowKind {
    Stmt,
    /// An `elseif` or `else` line (it selects its `if`).
    Arm,
    /// The place of an empty block, to insert into.
    Empty,
}

/// The lines of a block and of the blocks inside it, in order.
fn collect(
    context: &Context<'_>,
    block: &Block,
    parent: Option<(&StmtPath, usize)>,
    depth: usize,
    out: &mut Vec<Row>,
) {
    let path_of = |index| match parent {
        None => StmtPath::top(index),
        Some((path, child)) => path.child(child, index),
    };
    if block.is_empty() && parent.is_some() {
        out.push(Row {
            path: path_of(0),
            depth,
            text: text::EMPTY_BLOCK.to_owned(),
            kind: RowKind::Empty,
        });
        return;
    }
    for (index, stmt) in block.iter().enumerate() {
        let path = path_of(index);
        out.push(Row {
            path: path.clone(),
            depth,
            text: stmt_line(context, stmt),
            kind: RowKind::Stmt,
        });
        // A routine call that may end the script reads as one line.
        if is_link(&stmt.kind).is_some() {
            continue;
        }
        match &stmt.kind {
            StmtKind::If { arms, otherwise } => {
                for (arm, (condition, body)) in arms.iter().enumerate() {
                    if arm > 0 {
                        out.push(Row {
                            path: path.clone(),
                            depth,
                            text: format!(
                                "{} {}",
                                text::ARM_ELSEIF,
                                context.names.condition(condition)
                            ),
                            kind: RowKind::Arm,
                        });
                    }
                    collect(context, body, Some((&path, arm)), depth + 1, out);
                }
                if let Some(body) = otherwise {
                    out.push(Row {
                        path: path.clone(),
                        depth,
                        text: text::ARM_ELSE.to_owned(),
                        kind: RowKind::Arm,
                    });
                    collect(context, body, Some((&path, arms.len())), depth + 1, out);
                }
            }
            StmtKind::While { body, .. } | StmtKind::Do(body) | StmtKind::Function { body, .. } => {
                collect(context, body, Some((&path, 0)), depth + 1, out);
            }
            _ => {}
        }
    }
}

/// Where the palette inserts: after the statement selected (into its block when asked and it has
/// one), in the empty block selected, or before the section's last top-level `return`.
fn insertion_point(block: &Block, selected: Option<&StmtPath>, inside: bool) -> StmtPath {
    let Some(path) = selected else {
        let end = block
            .iter()
            .rposition(|stmt| matches!(stmt.kind, StmtKind::Return(_)))
            .unwrap_or(block.len());
        return StmtPath::top(end);
    };
    match edit::stmt_at(block, path) {
        Some(stmt) if inside && !edit::children(stmt).is_empty() => path.child(0, 0),
        Some(_) => StmtPath {
            blocks: path.blocks.clone(),
            index: path.index + 1,
        },
        None => path.clone(),
    }
}

/// The structure tab: the buttons for the statement selected, the list, its fields and the
/// palette.
fn structure(
    context: &Context<'_>,
    state: &mut SectionUi,
    ui: &mut egui::Ui,
    actions: &mut Vec<Action>,
) {
    let empty = Block::new();
    let code = context.mapper.open_code();
    if code.is_none() {
        ui.colored_label(crate::events::colors::MISSING, text::NO_SECTION_CODE);
    }
    let block = code.unwrap_or(&empty);
    let selected = context.open.selected.as_ref();
    let stmt = selected.and_then(|path| edit::stmt_at(block, path));
    ui.horizontal_wrapped(|ui| {
        let has = stmt.is_some();
        for (label, later) in [(text::UP, false), (text::DOWN, true)] {
            if ui.add_enabled(has, egui::Button::new(label)).clicked()
                && let Some(at) = selected
            {
                actions.push(Action::EditStory(StoryEdit::Move {
                    at: at.clone(),
                    later,
                }));
            }
        }
        if ui
            .add_enabled(has, egui::Button::new(text::DELETE_STMT))
            .clicked()
            && let Some(at) = selected
        {
            actions.push(Action::EditStory(StoryEdit::Remove(at.clone())));
        }
        if let Some(routine) = stmt.and_then(routine_of)
            && ui
                .button(format!("{} {routine}", text::OPEN_ROUTINE))
                .clicked()
        {
            actions.push(Action::OpenRoutine(Some(routine.to_owned())));
        }
    });
    let mut rows = Vec::new();
    collect(context, block, None, 0, &mut rows);
    egui::ScrollArea::vertical()
        .id_salt("section_lines")
        .max_height(290.0)
        .auto_shrink([false, true])
        .show(ui, |ui| {
            ui.style_mut().wrap_mode = Some(egui::TextWrapMode::Truncate);
            for row in &rows {
                ui.horizontal(|ui| {
                    ui.add_space(row.depth as f32 * INDENT);
                    let chosen = row.kind != RowKind::Arm && selected == Some(&row.path);
                    let label = match row.kind {
                        RowKind::Stmt => egui::RichText::new(&row.text),
                        RowKind::Arm => egui::RichText::new(&row.text).italics(),
                        RowKind::Empty => egui::RichText::new(&row.text).weak(),
                    };
                    if ui.selectable_label(chosen, label).clicked() {
                        actions.push(Action::SelectStmt(Some(row.path.clone())));
                    }
                });
            }
        });
    ui.separator();
    match (selected, stmt) {
        (Some(path), Some(stmt)) => fields(context, state, ui, path, stmt, actions),
        _ => {
            ui.weak(text::SELECT_A_LINE);
        }
    }
    ui.separator();
    palette(state, ui, block, selected, stmt, actions);
}

/// The fields of the statement selected: an `if`'s or a loop's conditions, or a command's
/// operands.
fn fields(
    context: &Context<'_>,
    state: &mut SectionUi,
    ui: &mut egui::Ui,
    path: &StmtPath,
    stmt: &Stmt,
    actions: &mut Vec<Action>,
) {
    match &stmt.kind {
        StmtKind::If { arms, .. } if is_link(&stmt.kind).is_none() => {
            for (arm, (condition, _)) in arms.iter().enumerate() {
                condition_field(context, state, ui, path, arm, condition, actions);
            }
        }
        StmtKind::While { condition, .. } if *condition != Expr::Bool(true) => {
            condition_field(context, state, ui, path, 0, condition, actions);
        }
        _ => operands(context, state, ui, path, stmt, actions),
    }
}

/// A condition: as Lua to type, in words, and quick fields when it is simple.
fn condition_field(
    context: &Context<'_>,
    state: &mut SectionUi,
    ui: &mut egui::Ui,
    path: &StmtPath,
    arm: usize,
    condition: &Expr,
    actions: &mut Vec<Action>,
) {
    let field = Field::Condition(arm);
    ui.strong(text::arm_label(arm));
    let current = expr_text(condition);
    let mut typed = state.typed(path, field, &current);
    let response = ui.add(
        egui::TextEdit::singleline(&mut typed)
            .code_editor()
            .hint_text(text::EXPR_HINT)
            .desired_width(f32::INFINITY),
    );
    if response.changed() {
        state.typing = Some((path.clone(), field, typed.clone()));
    }
    let parsed = parse_expr(&typed);
    if let Err(error) = &parsed {
        ui.colored_label(egui::Color32::LIGHT_RED, text::lua_error(error));
    }
    if response.lost_focus() {
        if let Ok(condition) = parsed
            && typed != current
        {
            actions.push(Action::EditStory(StoryEdit::SetCondition {
                at: path.clone(),
                arm,
                condition,
            }));
        }
        state.typing = None;
    }
    ui.weak(context.names.condition(condition));
    quick_condition(context, ui, path, arm, condition, actions);
    ui.add_space(4.0);
}

/// The comparisons the quick fields offer.
const COMPARISONS: [BinOp; 6] = [
    BinOp::Eq,
    BinOp::Ne,
    BinOp::Lt,
    BinOp::Le,
    BinOp::Gt,
    BinOp::Ge,
];

/// A trigger on or off, or a variable compared with a number, without typing Lua.
fn quick_condition(
    context: &Context<'_>,
    ui: &mut egui::Ui,
    path: &StmtPath,
    arm: usize,
    condition: &Expr,
    actions: &mut Vec<Action>,
) {
    let simple = SimpleCondition::of(condition);
    let mut choice = simple;
    let number = match simple {
        Some(SimpleCondition::Trigger { number, .. } | SimpleCondition::Var { number, .. }) => {
            number
        }
        None => 0,
    };
    let kind_label = |choice: Option<SimpleCondition>| match choice {
        Some(SimpleCondition::Trigger { on: true, .. }) => text::QUICK_TRIGGER_ON,
        Some(SimpleCondition::Trigger { on: false, .. }) => text::QUICK_TRIGGER_OFF,
        Some(SimpleCondition::Var { .. }) => text::QUICK_VAR,
        None => text::QUICK_OTHER,
    };
    ui.horizontal_wrapped(|ui| {
        egui::ComboBox::from_id_salt(("quick_kind", arm))
            .selected_text(kind_label(simple))
            .show_ui(ui, |ui| {
                for option in [
                    SimpleCondition::Trigger { number, on: true },
                    SimpleCondition::Trigger { number, on: false },
                    SimpleCondition::Var {
                        number,
                        op: BinOp::Eq,
                        value: 0,
                    },
                ] {
                    let same = kind_label(Some(option)) == kind_label(simple);
                    if ui
                        .selectable_label(same, kind_label(Some(option)))
                        .clicked()
                        && !same
                    {
                        choice = Some(option);
                    }
                }
            });
        match &mut choice {
            Some(SimpleCondition::Trigger { number, .. }) => {
                let response = ui.add(egui::DragValue::new(number).range(0..=MOST_STORY_NUMBER));
                grouped(&response, actions);
                ui.weak(context.names.trigger(*number));
            }
            Some(SimpleCondition::Var { number, op, value }) => {
                let response = ui.add(egui::DragValue::new(number).range(0..=MOST_STORY_NUMBER));
                grouped(&response, actions);
                egui::ComboBox::from_id_salt(("quick_op", arm))
                    .selected_text(op.symbol())
                    .width(48.0)
                    .show_ui(ui, |ui| {
                        for known in COMPARISONS {
                            ui.selectable_value(op, known, known.symbol());
                        }
                    });
                let response = ui.add(egui::DragValue::new(value));
                grouped(&response, actions);
                ui.weak(context.names.var(*number));
            }
            None => {}
        }
    });
    if choice != simple
        && let Some(choice) = choice
    {
        actions.push(Action::EditStory(StoryEdit::SetCondition {
            at: path.clone(),
            arm,
            condition: choice.expr(),
        }));
    }
}

/// A dialogue line to give a statement of its own: the new key and the text.
type NewLine = Option<(String, String)>;

/// The operands of a command, each by its kind.
fn operands(
    context: &Context<'_>,
    state: &mut SectionUi,
    ui: &mut egui::Ui,
    path: &StmtPath,
    stmt: &Stmt,
    actions: &mut Vec<Action>,
) {
    let mut edited = stmt.clone();
    let Some((name, arguments)) = edit::call_of_mut(&mut edited) else {
        ui.weak(text::NOTHING_TO_EDIT);
        return;
    };
    let command = commands::command(name);
    let title = command.map_or_else(
        || name.to_owned(),
        |command| format!("{} ({})", command.label, command.name),
    );
    ui.strong(format!("{} · {title}", text::OPERANDS));
    if arguments.is_empty() {
        ui.weak(text::NOTHING_TO_EDIT);
        return;
    }
    let mut changed = false;
    let mut new_line: NewLine = None;
    egui::Grid::new("operands").num_columns(2).show(ui, |ui| {
        for (index, argument) in arguments.iter_mut().enumerate() {
            let operand = command.and_then(|command| command.operands.get(index));
            let (label, kind) = operand.map_or(("?", OperandKind::Number), |operand| {
                (operand.name, operand.kind)
            });
            ui.monospace(label);
            let mut field = OperandField {
                context,
                state: &mut *state,
                path,
                index,
                actions: &mut *actions,
                new_line: &mut new_line,
            };
            changed |= field.show(ui, kind, argument);
            ui.end_row();
        }
    });
    if !changed {
        return;
    }
    let replace = Action::EditStory(StoryEdit::Replace {
        at: path.clone(),
        stmt: edited,
    });
    match new_line {
        Some((key, line)) => {
            actions.push(Action::BeginEdit);
            actions.push(Action::SetText { key, text: line });
            actions.push(replace);
            actions.push(Action::EndEdit);
        }
        None => actions.push(replace),
    }
}

/// One operand's field.
struct OperandField<'a, 'b> {
    context: &'a Context<'b>,
    state: &'a mut SectionUi,
    path: &'a StmtPath,
    index: usize,
    actions: &'a mut Vec<Action>,
    new_line: &'a mut NewLine,
}

impl OperandField<'_, '_> {
    /// Shows the field; true when the operand changed.
    fn show(&mut self, ui: &mut egui::Ui, kind: OperandKind, expr: &mut Expr) -> bool {
        match (kind, expr.as_number()) {
            (OperandKind::Entity, _) => self.entity(ui, expr),
            (OperandKind::Text, _) if matches!(expr, Expr::Str(_)) => self.text_line(ui, expr),
            (OperandKind::Map, Some(index)) => self.map(ui, index, expr),
            (_, Some(number)) => self.number(ui, kind, number, expr),
            _ => self.lua(ui, expr),
        }
    }

    fn entity(&mut self, ui: &mut egui::Ui, expr: &mut Expr) -> bool {
        let mut changed = false;
        let choices = [
            (text::ENTITY_PLAYER.to_owned(), Expr::name("PLAYER")),
            (text::ENTITY_PARTNER.to_owned(), Expr::name("PARTNER")),
        ]
        .into_iter()
        .chain(
            (0..mapper_core::story::MOST_NPCS as i64)
                .map(|slot| (format!("{} {slot}", text::ENTITY_NPC), Expr::Number(slot))),
        );
        egui::ComboBox::from_id_salt(("entity", self.index))
            .selected_text(self.context.names.entity(expr))
            .show_ui(ui, |ui| {
                for (label, value) in choices {
                    if ui.selectable_label(*expr == value, label).clicked() && *expr != value {
                        *expr = value;
                        changed = true;
                    }
                }
            });
        changed
    }

    fn number(
        &mut self,
        ui: &mut egui::Ui,
        kind: OperandKind,
        number: i64,
        expr: &mut Expr,
    ) -> bool {
        let mut value = number;
        let changed = ui
            .horizontal(|ui| {
                let drag = egui::DragValue::new(&mut value);
                let drag = match kind {
                    OperandKind::Exit => drag.range(0..=9),
                    OperandKind::Section => drag
                        .range(0..=255)
                        .custom_formatter(|value, _| format!("{:#04x}", value as i64))
                        .custom_parser(|text| {
                            let text = text.trim();
                            match text.strip_prefix("0x") {
                                Some(hex) => i64::from_str_radix(hex, 16).ok().map(|n| n as f64),
                                None => text.parse().ok(),
                            }
                        }),
                    OperandKind::Trigger | OperandKind::Var => drag.range(0..=MOST_STORY_NUMBER),
                    _ => drag,
                };
                let response = ui.add(drag);
                grouped(&response, self.actions);
                let names = &self.context.names;
                match kind {
                    OperandKind::Trigger => {
                        ui.weak(names.trigger(value));
                    }
                    OperandKind::Var => {
                        ui.weak(names.var(value));
                    }
                    OperandKind::Section if value == dwe_story::events::NO_SECTION => {
                        ui.weak(text::SECTION_NONE);
                    }
                    _ => {}
                }
                response.changed()
            })
            .inner;
        if changed {
            *expr = Expr::Number(value);
        }
        changed
    }

    fn map(&mut self, ui: &mut egui::Ui, index: i64, expr: &mut Expr) -> bool {
        let mut changed = false;
        let table = self.context.names.maps;
        egui::ComboBox::from_id_salt(("map", self.index))
            .selected_text(self.context.names.map(index))
            .height(320.0)
            .show_ui(ui, |ui| {
                ui.add(
                    egui::TextEdit::singleline(&mut self.state.map_filter)
                        .hint_text(text::FILTER_HINT),
                );
                let filter = self.state.map_filter.trim().to_lowercase();
                let maps = table
                    .maps
                    .iter()
                    .filter(|entry| entry.original_index && entry.id.contains(&filter))
                    .take(MOST_LISTED_MAPS);
                for entry in maps {
                    let chosen = i64::from(entry.index) == index;
                    if ui
                        .selectable_label(chosen, format!("{} ({})", entry.id, entry.index))
                        .clicked()
                        && !chosen
                    {
                        *expr = Expr::Number(i64::from(entry.index));
                        changed = true;
                    }
                }
            });
        changed
    }

    /// A text operand: its key, and its line to change in place or as a new line of its own.
    fn text_line(&mut self, ui: &mut egui::Ui, expr: &mut Expr) -> bool {
        let Expr::Str(key) = expr else { return false };
        let key = key.clone();
        let mapper = self.context.mapper;
        let mut changed = false;
        ui.vertical(|ui| {
            let field = Field::Operand(self.index);
            let mut typed = self.state.typed(self.path, field, &key);
            let response = ui.add(
                egui::TextEdit::singleline(&mut typed)
                    .code_editor()
                    .hint_text(text::TEXT_KEY)
                    .desired_width(f32::INFINITY),
            );
            if response.changed() {
                self.state.typing = Some((self.path.clone(), field, typed.clone()));
            }
            if response.lost_focus() {
                if typed != key {
                    *expr = Expr::Str(typed);
                    changed = true;
                }
                self.state.typing = None;
            }
            let current = mapper.text_of(&key).unwrap_or_default().to_owned();
            let mut line = match &self.state.line {
                Some((known, line)) if *known == key => line.clone(),
                _ => current.clone(),
            };
            ui.label(text::TEXT_LINE);
            let response = ui
                .add(
                    egui::TextEdit::multiline(&mut line)
                        .desired_rows(3)
                        .desired_width(f32::INFINITY),
                )
                .on_hover_text(text::TEXT_LINE_HINT);
            if response.changed() {
                self.state.line = Some((key.clone(), line.clone()));
            }
            if response.lost_focus() {
                if line != current {
                    self.actions.push(Action::SetText {
                        key: key.clone(),
                        text: line.clone(),
                    });
                }
                self.state.line = None;
            }
            if ui
                .button(text::NEW_LINE)
                .on_hover_text(text::NEW_LINE_HINT)
                .clicked()
            {
                let fresh = mapper.free_text_key(self.context.open.script);
                *expr = Expr::Str(fresh.clone());
                *self.new_line = Some((fresh, line));
                self.state.line = None;
                changed = true;
            }
        });
        changed
    }

    /// An operand that is not a plain value, as Lua.
    fn lua(&mut self, ui: &mut egui::Ui, expr: &mut Expr) -> bool {
        let field = Field::Operand(self.index);
        let current = expr_text(expr);
        let mut typed = self.state.typed(self.path, field, &current);
        let mut changed = false;
        ui.vertical(|ui| {
            let response = ui.add(
                egui::TextEdit::singleline(&mut typed)
                    .code_editor()
                    .hint_text(text::EXPR_HINT),
            );
            if response.changed() {
                self.state.typing = Some((self.path.clone(), field, typed.clone()));
            }
            let parsed = parse_expr(&typed);
            if let Err(error) = &parsed {
                ui.colored_label(egui::Color32::LIGHT_RED, text::lua_error(error));
            }
            if response.lost_focus() {
                if let Ok(value) = parsed
                    && typed != current
                {
                    *expr = value;
                    changed = true;
                }
                self.state.typing = None;
            }
        });
        changed
    }
}

/// The commands to add, by what they do to the flow, and the blocks.
fn palette(
    state: &mut SectionUi,
    ui: &mut egui::Ui,
    block: &Block,
    selected: Option<&StmtPath>,
    stmt: Option<&Stmt>,
    actions: &mut Vec<Action>,
) {
    egui::CollapsingHeader::new(text::PALETTE)
        .id_salt("palette")
        .default_open(true)
        .show(ui, |ui| {
            let has_block = stmt.is_some_and(|stmt| !edit::children(stmt).is_empty());
            ui.horizontal_wrapped(|ui| {
                ui.add(
                    egui::TextEdit::singleline(&mut state.palette_filter)
                        .hint_text(text::PALETTE_FILTER_HINT)
                        .desired_width(160.0),
                );
                ui.add_enabled(
                    has_block,
                    egui::Checkbox::new(&mut state.inside, text::INSERT_INSIDE),
                );
            });
            ui.weak(text::INSERT_HINT);
            let at = insertion_point(block, selected, state.inside && has_block);
            let mut insert = |stmt: Stmt| {
                actions.push(Action::EditStory(StoryEdit::Insert {
                    at: at.clone(),
                    stmt,
                }));
            };
            let filter = state.palette_filter.trim().to_lowercase();
            egui::ScrollArea::vertical()
                .id_salt("palette_list")
                .max_height(180.0)
                .show(ui, |ui| {
                    for (title, flow) in [
                        (text::PALETTE_PLAIN, Flow::Plain),
                        (text::PALETTE_ENDS, Flow::Ends),
                    ] {
                        ui.label(egui::RichText::new(title).strong());
                        ui.horizontal_wrapped(|ui| {
                            let commands = commands::COMMANDS.iter().filter(|command| {
                                command.palette
                                    && command.flow == flow
                                    && (filter.is_empty()
                                        || command.label.to_lowercase().contains(&filter)
                                        || command.name.contains(&filter))
                            });
                            for command in commands {
                                if ui
                                    .button(command.label)
                                    .on_hover_text(command.name)
                                    .clicked()
                                {
                                    insert(edit::new_command(command));
                                }
                            }
                        });
                    }
                    ui.label(egui::RichText::new(text::PALETTE_BLOCKS).strong());
                    ui.horizontal_wrapped(|ui| {
                        if ui.button(text::NEW_IF).clicked() {
                            let condition = Expr::story_call("trigger", vec![Expr::Number(0)]);
                            insert(edit::new_if(condition));
                        }
                        if ui.button(text::NEW_END).clicked() {
                            insert(edit::end_script());
                        }
                    });
                });
        });
}

/// The Lua tab: the code shown as text to edit, checked as it is typed.
fn lua(context: &Context<'_>, state: &mut SectionUi, ui: &mut egui::Ui, actions: &mut Vec<Action>) {
    let code = context
        .mapper
        .open_code()
        .map(block_text)
        .unwrap_or_default();
    if code != state.lua_base {
        state.lua = code.clone();
        state.lua_base = code;
    }
    let parsed = parse_chunk(&state.lua);
    ui.horizontal_wrapped(|ui| {
        let ready = parsed.is_ok() && state.lua != state.lua_base;
        if ui
            .add_enabled(ready, egui::Button::new(text::APPLY))
            .on_hover_text(text::LUA_APPLY_HINT)
            .clicked()
        {
            actions.push(Action::EditStory(StoryEdit::SetCode(state.lua.clone())));
        }
        match &parsed {
            Ok(_) => ui.colored_label(egui::Color32::LIGHT_GREEN, text::LUA_OK),
            Err(error) => ui.colored_label(egui::Color32::LIGHT_RED, text::lua_error(error)),
        };
    });
    egui::ScrollArea::vertical()
        .id_salt("section_lua")
        .show(ui, |ui| {
            ui.add(
                egui::TextEdit::multiline(&mut state.lua)
                    .code_editor()
                    .desired_rows(26)
                    .desired_width(f32::INFINITY),
            );
        });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn inserts_after_the_selection_or_before_the_end() {
        let block = parse_chunk(
            "if story.trigger(1) then\n  story.delay(1)\nend\nstory.delay(2)\nreturn ENDED\n",
        )
        .unwrap();
        assert_eq!(insertion_point(&block, None, false), StmtPath::top(2));
        let first = StmtPath::top(0);
        assert_eq!(
            insertion_point(&block, Some(&first), false),
            StmtPath::top(1)
        );
        assert_eq!(
            insertion_point(&block, Some(&first), true),
            first.child(0, 0)
        );
        let empty = StmtPath::top(1).child(0, 0);
        assert_eq!(insertion_point(&block, Some(&empty), false), empty);
    }

    #[test]
    fn a_routine_call_names_its_routine() {
        let block = parse_chunk(
            "if routines.at_0010() == ENDED then return ENDED end\nreturn routines.at_0020()\nstory.delay(1)\n",
        )
        .unwrap();
        assert_eq!(routine_of(&block[0]), Some("at_0010"));
        assert_eq!(routine_of(&block[1]), Some("at_0020"));
        assert_eq!(routine_of(&block[2]), None);
        assert_eq!(quoted(Some("a\nb"), "k"), "«a b»");
        assert_eq!(quoted(None, "k"), "k");
    }
}
