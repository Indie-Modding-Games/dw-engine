//! `dwe-mapper-mcp`: the map editor's MCP server on its own (see the `mcp` mode of dwe-mapper).

fn main() -> std::process::ExitCode {
    mapper_mcp::run(std::env::args().skip(1), |_| None)
}
