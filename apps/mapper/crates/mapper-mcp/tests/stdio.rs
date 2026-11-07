//! Drives the MCP server over stdio, the way an external tool does: a synthetic content folder
//! (neutral ids), the handshake, the tool list, a read, a dry-run edit, a save into a new mod pack
//! and the saved edit read back by a second server.

use std::io::{BufRead, BufReader, Write};
use std::path::Path;
use std::process::{Child, ChildStdin, ChildStdout, Command, Stdio};

use serde_json::{Value, json};

fn write(path: &Path, text: &str) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(path, text).unwrap();
}

/// A map with walls on row 0 and exit 0's two cells on row 50, leading to `target`'s exit 0.
fn map_yaml(target_index: i32, target: &str) -> String {
    let mut rows = vec![".".repeat(100); 100];
    rows[0] = "#".repeat(100);
    rows[50] = format!("{}00{}", ".".repeat(10), ".".repeat(88));
    let rows: String = rows.iter().map(|row| format!("  - \"{row}\"\n")).collect();
    format!(
        "camera:\n  viewpoint: {{x: 0, y: -3000, z: -4000}}\n  reference: {{x: 0, y: 0, z: 0}}\n  projection: 1000\n\
         tiles:\n  width: 3\n  height: 2\n  rows: [\"0 1 2\", \"3 4 5\"]\n\
         warps:\n- exit: 0\n  spawn: {{x: 0, y: 0, z: -1050}}\n  rotation: 0\n  target_map: {target_index}\n  target_map_id: \"{target}\"\n  target_exit: 0\n\
         npcs: []\n\
         collision:\n  width: 100\n  height: 100\n  encoding: \"legend\"\n  legend:\n\
         \x20 - {{cell: \".\", value: 0}}\n  - {{cell: \"#\", value: 128}}\n  - {{cell: \"0\", value: 110}}\n\
         \x20 rows:\n{rows}"
    )
}

fn content(root: &Path) {
    let base = root.join("content/base");
    write(
        &base.join("pack.yaml"),
        "id: base\nversion: 1.0.0\nlayer: base\n",
    );
    write(
        &base.join("data/maps.yaml"),
        "maps:\n\
         - {id: area_a, index: 0, filename: AREA_A, doors: null, toilet: null, loading_name: 0}\n\
         - {id: area_b, index: 1, filename: AREA_B, doors: null, toilet: null, loading_name: 0}\n\
         map_names: [' Plains ']\n",
    );
    write(
        &base.join("field/warps.yaml"),
        "start:\n  map: area_a\n  exit: 0\n",
    );
    write(&base.join("map/area_a/data.yaml"), &map_yaml(1, "area_b"));
    write(&base.join("map/area_b/data.yaml"), &map_yaml(0, "area_a"));
    for map in ["area_a", "area_b"] {
        write(&base.join(format!("map/{map}/background_day.png")), "day");
    }
    std::fs::create_dir_all(root.join("mods")).unwrap();
}

struct Session {
    child: Child,
    input: ChildStdin,
    output: BufReader<ChildStdout>,
    next: u64,
}

impl Session {
    fn start(root: &Path) -> Self {
        let mut child = Command::new(env!("CARGO_BIN_EXE_dwe-mapper-mcp"))
            .arg("--content")
            .arg(root.join("content"))
            .arg("--mods")
            .arg(root.join("mods"))
            .arg("--target")
            .arg(root.join("mods/mine"))
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::inherit())
            .spawn()
            .unwrap();
        let input = child.stdin.take().unwrap();
        let output = BufReader::new(child.stdout.take().unwrap());
        Self {
            child,
            input,
            output,
            next: 1,
        }
    }

    fn request(&mut self, method: &str, params: Value) -> Value {
        let id = self.next;
        self.next += 1;
        let message = json!({"jsonrpc": "2.0", "id": id, "method": method, "params": params});
        writeln!(self.input, "{message}").unwrap();
        self.input.flush().unwrap();
        let mut line = String::new();
        self.output.read_line(&mut line).unwrap();
        let answer: Value = serde_json::from_str(&line).unwrap();
        assert_eq!(answer["id"], json!(id), "{answer}");
        answer
    }

    fn notify(&mut self, method: &str) {
        writeln!(
            self.input,
            "{}",
            json!({"jsonrpc": "2.0", "method": method})
        )
        .unwrap();
    }

    /// Calls a tool and answers its structured result; panics on a tool error.
    fn call(&mut self, name: &str, arguments: Value) -> Value {
        let answer = self.request("tools/call", json!({"name": name, "arguments": arguments}));
        let result = &answer["result"];
        assert_eq!(result["isError"], json!(false), "{name}: {answer}");
        result["structuredContent"].clone()
    }

    /// Calls a tool that must fail and answers its error text.
    fn fails(&mut self, name: &str, arguments: Value) -> String {
        let answer = self.request("tools/call", json!({"name": name, "arguments": arguments}));
        assert_eq!(answer["result"]["isError"], json!(true), "{name}: {answer}");
        answer["result"]["content"][0]["text"]
            .as_str()
            .unwrap()
            .to_owned()
    }

    fn handshake(&mut self) -> Value {
        let answer = self.request(
            "initialize",
            json!({"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "test", "version": "0"}}),
        );
        self.notify("notifications/initialized");
        answer
    }

    fn close(mut self) {
        drop(self.input);
        let status = self.child.wait().unwrap();
        assert!(status.success(), "{status}");
    }
}

fn walls(info: &Value) -> i64 {
    info["collision"]["wall"].as_i64().unwrap_or(0)
}

#[test]
fn a_session_reads_edits_with_a_dry_run_saves_and_reads_back() {
    let dir = tempfile::tempdir().unwrap();
    content(dir.path());
    let mut session = Session::start(dir.path());

    let hello = session.handshake();
    assert_eq!(hello["result"]["protocolVersion"], "2025-06-18");
    assert_eq!(hello["result"]["serverInfo"]["name"], "dwe-mapper");
    assert!(hello["result"]["capabilities"]["tools"].is_object());

    let tools = session.request("tools/list", json!({}));
    let tools = tools["result"]["tools"].as_array().unwrap().clone();
    let names: Vec<&str> = tools
        .iter()
        .map(|tool| tool["name"].as_str().unwrap())
        .collect();
    for name in [
        "list_maps",
        "map_info",
        "world_graph",
        "path",
        "section",
        "find",
        "validate",
        "set_exit",
        "set_collision_rect",
        "add_npc",
        "move_npc",
        "edit_section",
        "set_text",
        "new_map",
        "save",
        "live_connect",
        "live_status",
        "live_reload",
        "probar_evento",
        "play_from",
    ] {
        assert!(names.contains(&name), "{name} is missing from {names:?}");
    }
    assert!(
        tools
            .iter()
            .all(|tool| tool["inputSchema"]["type"] == "object")
    );

    let maps = session.call("list_maps", json!({}));
    assert_eq!(maps["count"], 2);
    assert_eq!(maps["maps"][0]["area"], "Plains");
    let way = session.call("path", json!({"from": "area_a", "to": "area_b"}));
    assert_eq!(way["steps"][0]["label"], "area_a:0 → area_b:0");

    let before = session.call("map_info", json!({"map": "area_a"}));
    assert_eq!(before["exits"][0]["to"], "area_b:0");
    let walls_before = walls(&before);
    assert_eq!(walls_before, 100);

    let rect = json!({"map": "area_a", "column0": 30, "row0": 30, "column1": 32, "row1": 31,
                      "value": 128, "dry_run": true});
    let dry = session.call("set_collision_rect", rect.clone());
    assert_eq!(dry["kept"], false);
    assert!(
        dry["patch"].as_str().unwrap().contains("collision"),
        "{dry}"
    );
    let unchanged = session.call("map_info", json!({"map": "area_a"}));
    assert_eq!(walls(&unchanged), walls_before);
    assert_eq!(unchanged["unsaved"], false);

    let mut real = rect;
    real["dry_run"] = json!(false);
    assert_eq!(session.call("set_collision_rect", real)["kept"], true);
    assert!(
        session
            .fails("map_info", json!({"map": "area_b"}))
            .contains("unsaved")
    );
    let saved = session.call("save", json!({}));
    assert_eq!(saved["saved_in"], "mine");
    assert!(dir.path().join("mods/mine/pack.yaml").is_file());

    let bad = session.request(
        "tools/call",
        json!({"name": "no_such_tool", "arguments": {}}),
    );
    assert_eq!(bad["error"]["code"], -32602);
    assert!(
        session
            .fails("map_info", json!({}))
            .contains("'map' is needed")
    );
    session.close();

    let mut again = Session::start(dir.path());
    again.handshake();
    let after = again.call("map_info", json!({"map": "area_a"}));
    assert_eq!(walls(&after), walls_before + 6);
    again.close();
}

#[test]
fn a_new_map_is_planned_by_a_dry_run_then_written_and_connected() {
    let dir = tempfile::tempdir().unwrap();
    content(dir.path());
    let mut session = Session::start(dir.path());
    session.handshake();
    let new = json!({"name": "cellar", "title": "Bodega", "area": 0, "copy_of": "area_b",
                     "connect_map": "area_a", "connect_exit": 0, "own_exit": 0, "dry_run": true});
    let plan = session.call("new_map", new.clone());
    assert_eq!(plan["id"], "mine:cellar");
    assert!(!dir.path().join("mods/mine/map/mine/cellar").exists());

    let mut real = new;
    real["dry_run"] = json!(false);
    assert_eq!(session.call("new_map", real)["id"], "mine:cellar");
    assert!(
        dir.path()
            .join("mods/mine/map/mine/cellar/data.yaml")
            .is_file()
    );
    let graph = session.call("world_graph", json!({"map": "mine:cellar"}));
    let labels: Vec<&str> = graph["edges"]
        .as_array()
        .unwrap()
        .iter()
        .map(|edge| edge["label"].as_str().unwrap())
        .collect();
    assert!(labels.contains(&"area_a:0 → mine:cellar:0"), "{labels:?}");
    assert!(labels.contains(&"mine:cellar:0 → area_a:0"), "{labels:?}");
    session.close();
}
