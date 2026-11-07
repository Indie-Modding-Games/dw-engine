//! The map editor without a window: an MCP server over stdio for external tools
//! (docs/guides/mapper-mcp.md). It opens the content as the editor does, keeps one [`Mapper`]
//! and answers JSON-RPC 2.0 requests, one per line: `initialize`, `tools/list` and `tools/call`.
//! Every edit goes through the editor's [`mapper_core::Action`]s and saves through its savers.

mod tools;

use std::io::{BufRead, Write};
use std::path::PathBuf;

use dwe_pack::GameFolders;
use mapper_core::{Action, Mapper, Message};
use serde_json::{Value, json};

/// The protocol version the server speaks.
pub const PROTOCOL_VERSION: &str = "2025-06-18";

pub const USAGE: &str = "\
Usage: dwe-mapper mcp --content <dir> [options]

Serves the map editor to external tools over stdio (MCP, one JSON-RPC message per line).

  --content <dir>      the content folder: the base pack and the packs next to it
  --team-packs <dir>   the team's packs (content/packs of the source tree)
  --mods <dir>         a mods folder or a single pack (repeatable)
  --target <pack>      where saves go: the id or folder of a writable pack, or a new folder in a
                       mods folder, which becomes a new mod pack
  --live <endpoint>    connects to a running dwe-game: unix:<socket> or tcp:<port>
  -h, --help           this help";

/// What the command line gives the server.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ServerOptions {
    pub content: Option<PathBuf>,
    pub team_packs: Option<PathBuf>,
    pub mods: Vec<PathBuf>,
    pub target: Option<String>,
    pub live: Option<String>,
    pub help: bool,
}

impl ServerOptions {
    pub fn parse(arguments: impl IntoIterator<Item = String>) -> Result<Self, String> {
        let mut options = Self::default();
        let mut arguments = arguments.into_iter();
        while let Some(argument) = arguments.next() {
            if argument == "-h" || argument == "--help" {
                options.help = true;
                continue;
            }
            let mut value = || {
                arguments
                    .next()
                    .ok_or_else(|| format!("{argument} needs a value"))
            };
            match argument.as_str() {
                "--content" => options.content = Some(value()?.into()),
                "--team-packs" => options.team_packs = Some(value()?.into()),
                "--mods" => options.mods.push(value()?.into()),
                "--target" => options.target = Some(value()?),
                "--live" => options.live = Some(value()?),
                other => return Err(format!("unknown option '{other}'")),
            }
        }
        if options.content.is_none() && !options.help {
            return Err("--content is needed".to_owned());
        }
        Ok(options)
    }
}

/// The server: the editor's model, the game it may be linked to, and what it was started with.
pub struct Server {
    pub(crate) mapper: Mapper,
    pub(crate) live: Option<mapper_live::LiveClient>,
}

impl Server {
    /// Opens the content and picks the target; a content that does not open is an error.
    pub fn open(options: &ServerOptions) -> Result<Self, String> {
        let content = options.content.clone().ok_or("--content is needed")?;
        let mut mapper = Mapper::new();
        mapper.apply(Action::OpenContent(GameFolders {
            content,
            team_packs: options.team_packs.clone(),
            mods: options.mods.clone(),
        }));
        if mapper.content.is_none() {
            return Err(errors(&mut mapper).join("; "));
        }
        mapper.messages.clear();
        let mut server = Self { mapper, live: None };
        if let Some(target) = &options.target {
            server.choose_target(target)?;
        }
        if let Some(endpoint) = &options.live {
            server.connect(endpoint)?;
        }
        Ok(server)
    }

    /// Makes a pack the target: a writable pack by id or folder, or a new mod pack for a folder
    /// that does not exist yet inside a mods folder.
    pub(crate) fn choose_target(&mut self, target: &str) -> Result<(), String> {
        let path = PathBuf::from(target);
        let found = self
            .mapper
            .targets()
            .into_iter()
            .find(|candidate| candidate.id == target || candidate.root == path);
        if let Some(found) = found {
            self.mapper.apply(Action::SetTarget(Some(found.root)));
            return Ok(());
        }
        let (Some(folder), Some(id)) = (path.parent(), path.file_name()) else {
            return Err(format!("'{target}' is not a pack the editor may write"));
        };
        if path.exists() || !folder.is_dir() {
            return Err(format!("'{target}' is not a pack the editor may write"));
        }
        let id = id.to_string_lossy().into_owned();
        self.mapper.apply(Action::CreateModPack {
            folder: folder.to_path_buf(),
            id,
        });
        let problems = errors(&mut self.mapper);
        if self.mapper.current_target().is_none() {
            return Err(format!(
                "'{target}' could not become the target: {}",
                problems.join("; ")
            ));
        }
        Ok(())
    }

    pub(crate) fn connect(&mut self, endpoint: &str) -> Result<(), String> {
        let endpoint = mapper_live::Endpoint::parse(endpoint).map_err(|error| error.to_string())?;
        let client =
            mapper_live::LiveClient::connect(&endpoint).map_err(|error| error.to_string())?;
        self.live = Some(client);
        Ok(())
    }

    /// Answers one JSON-RPC message; None for a notification.
    pub fn handle(&mut self, message: &Value) -> Option<Value> {
        let id = message.get("id").cloned();
        let method = message.get("method").and_then(Value::as_str).unwrap_or("");
        let params = message.get("params").cloned().unwrap_or(Value::Null);
        let answer = match method {
            "initialize" => Ok(json!({
                "protocolVersion": params
                    .get("protocolVersion")
                    .and_then(Value::as_str)
                    .unwrap_or(PROTOCOL_VERSION),
                "capabilities": {"tools": {"listChanged": false}},
                "serverInfo": {"name": "dwe-mapper", "version": env!("CARGO_PKG_VERSION")},
                "instructions": tools::INSTRUCTIONS,
            })),
            "ping" => Ok(json!({})),
            "tools/list" => Ok(json!({"tools": tools::list()})),
            "tools/call" => {
                let name = params.get("name").and_then(Value::as_str);
                let arguments = params
                    .get("arguments")
                    .cloned()
                    .unwrap_or_else(|| json!({}));
                match name {
                    Some(name) if tools::exists(name) => Ok(self.call(name, &arguments)),
                    Some(name) => Err((-32602, format!("unknown tool '{name}'"))),
                    None => Err((-32602, "tools/call needs a name".to_owned())),
                }
            }
            _ if id.is_none() => return None,
            other => Err((-32601, format!("unknown method '{other}'"))),
        };
        let id = id?;
        Some(match answer {
            Ok(result) => json!({"jsonrpc": "2.0", "id": id, "result": result}),
            Err((code, message)) => {
                json!({"jsonrpc": "2.0", "id": id, "error": {"code": code, "message": message}})
            }
        })
    }

    /// Runs a tool: its answer as text and as structured content, or the error.
    fn call(&mut self, name: &str, arguments: &Value) -> Value {
        self.mapper.messages.clear();
        let result = tools::call(self, name, arguments);
        let messages: Vec<Value> = self
            .mapper
            .messages
            .drain(..)
            .map(|message| match message {
                Message::Info(text) => json!({"info": text}),
                Message::Error(text) => json!({"error": text}),
            })
            .collect();
        match result {
            Ok(mut value) => {
                if !messages.is_empty()
                    && let Some(object) = value.as_object_mut()
                {
                    object.insert("messages".to_owned(), Value::Array(messages));
                }
                json!({
                    "content": [{"type": "text", "text": pretty(&value)}],
                    "structuredContent": value,
                    "isError": false,
                })
            }
            Err(error) => {
                let mut text = error;
                for message in &messages {
                    if let Some(extra) = message.get("error").and_then(Value::as_str) {
                        text.push_str("; ");
                        text.push_str(extra);
                    }
                }
                json!({"content": [{"type": "text", "text": text}], "isError": true})
            }
        }
    }
}

fn pretty(value: &Value) -> String {
    serde_json::to_string_pretty(value).unwrap_or_default()
}

/// The errors the editor said, taken out of its messages.
pub(crate) fn errors(mapper: &mut Mapper) -> Vec<String> {
    let mut errors = Vec::new();
    mapper.messages.retain(|message| match message {
        Message::Error(error) => {
            errors.push(error.clone());
            false
        }
        Message::Info(_) => true,
    });
    errors
}

/// Serves messages from `input` until it ends, one answer per line on `output`.
pub fn serve(
    server: &mut Server,
    input: impl BufRead,
    mut output: impl Write,
) -> std::io::Result<()> {
    for line in input.lines() {
        let line = line?;
        if line.trim().is_empty() {
            continue;
        }
        let answer = match serde_json::from_str::<Value>(&line) {
            Ok(message) => server.handle(&message),
            Err(error) => Some(json!({
                "jsonrpc": "2.0",
                "id": null,
                "error": {"code": -32700, "message": error.to_string()},
            })),
        };
        if let Some(answer) = answer {
            writeln!(output, "{answer}")?;
            output.flush()?;
        }
    }
    Ok(())
}

/// `dwe-mapper mcp`: parses the options, opens the content and serves stdin and stdout.
pub fn run(
    arguments: impl IntoIterator<Item = String>,
    team_packs: impl FnOnce(&std::path::Path) -> Option<PathBuf>,
) -> std::process::ExitCode {
    let mut options = match ServerOptions::parse(arguments) {
        Ok(options) => options,
        Err(message) => {
            eprintln!("dwe-mapper mcp: {message}\n\n{USAGE}");
            return std::process::ExitCode::from(2);
        }
    };
    if options.help {
        println!("{USAGE}");
        return std::process::ExitCode::SUCCESS;
    }
    if options.team_packs.is_none()
        && let Some(content) = &options.content
    {
        options.team_packs = team_packs(content);
    }
    let mut server = match Server::open(&options) {
        Ok(server) => server,
        Err(message) => {
            eprintln!("dwe-mapper mcp: {message}");
            return std::process::ExitCode::FAILURE;
        }
    };
    let stdin = std::io::stdin();
    match serve(&mut server, stdin.lock(), std::io::stdout().lock()) {
        Ok(()) => std::process::ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("dwe-mapper mcp: {error}");
            std::process::ExitCode::FAILURE
        }
    }
}
