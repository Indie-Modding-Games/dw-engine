//! The command line.

use std::path::{Path, PathBuf};

use dwe_pack::GameFolders;
use mapper_core::{GroupBy, Layer};

pub const USAGE: &str = "\
Usage: dwe-mapper [options]

  --content <dir>      the content folder: the base pack and the packs next to it, as the game
  --team-packs <dir>   the team's packs (content/packs of the source tree); found on its own
                       when the program runs inside the repository
  --mods <dir>         a mods folder (repeatable)
  --map <id>           the map to show first
  --exit <n>           with --map: centre on where the player arrives through that exit
  --variant <name>     the background: day, dusk, night...
  --layers <list>      the layers shown, by comma: walls,triggers,exits,npcs,waypoints,doors,
                       toilet,grid (default: all but grid)
  --group-by <kind>    the map list's groups: name or prefix
  --filter <text>      filters the map list
  --live <endpoint>    connects to a running dwe-game: unix:<socket> or tcp:<port>
  --size <w>x<h>       the window's size (default 1600x900)
  --shot <file.png>    saves a picture of the window once it is drawn, then quits
  -h, --help           this help";

#[derive(Clone, Debug, PartialEq)]
pub struct Options {
    pub content: Option<PathBuf>,
    pub team_packs: Option<PathBuf>,
    pub mods: Vec<PathBuf>,
    pub map: Option<String>,
    pub exit: Option<i32>,
    pub variant: Option<String>,
    pub layers: Option<Vec<Layer>>,
    pub group_by: Option<GroupBy>,
    pub filter: Option<String>,
    pub live: Option<String>,
    pub size: [f32; 2],
    pub shot: Option<PathBuf>,
    pub help: bool,
}

impl Default for Options {
    fn default() -> Self {
        Self {
            content: None,
            team_packs: None,
            mods: Vec::new(),
            map: None,
            exit: None,
            variant: None,
            layers: None,
            group_by: None,
            filter: None,
            live: None,
            size: [1600.0, 900.0],
            shot: None,
            help: false,
        }
    }
}

impl Options {
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
                "--map" => options.map = Some(value()?),
                "--exit" => {
                    let text = value()?;
                    options.exit = Some(
                        text.parse()
                            .map_err(|_| format!("--exit {text} is not a number"))?,
                    );
                }
                "--variant" => options.variant = Some(value()?),
                "--layers" => {
                    let list = value()?;
                    let layers = list
                        .split(',')
                        .filter(|name| !name.is_empty())
                        .map(|name| {
                            Layer::named(name).ok_or_else(|| format!("unknown layer '{name}'"))
                        })
                        .collect::<Result<Vec<_>, _>>()?;
                    options.layers = Some(layers);
                }
                "--group-by" => {
                    options.group_by = Some(match value()?.as_str() {
                        "name" => GroupBy::LoadingName,
                        "prefix" => GroupBy::Prefix,
                        other => {
                            return Err(format!("--group-by takes name or prefix, not '{other}'"));
                        }
                    });
                }
                "--filter" => options.filter = Some(value()?),
                "--live" => options.live = Some(value()?),
                "--size" => {
                    let text = value()?;
                    let parsed = text
                        .split_once('x')
                        .and_then(|(w, h)| Some([w.parse().ok()?, h.parse().ok()?]))
                        .filter(|[w, h]: &[f32; 2]| *w >= 320.0 && *h >= 240.0);
                    options.size = parsed
                        .ok_or_else(|| format!("--size {text} is not <w>x<h>, at least 320x240"))?;
                }
                "--shot" => options.shot = Some(value()?.into()),
                other => return Err(format!("unknown option '{other}'")),
            }
        }
        Ok(options)
    }

    /// The folders to mount, or None without --content.
    pub fn folders(&self) -> Option<GameFolders> {
        let content = self.content.clone()?;
        Some(GameFolders {
            team_packs: self
                .team_packs
                .clone()
                .or_else(|| find_team_packs(&content)),
            content,
            mods: self.mods.clone(),
        })
    }
}

/// The source tree's `content/packs`, looked for upwards from the working folder and from the
/// program's own folder, as the game finds the team's packs when it runs from a build.
pub fn find_team_packs(content: &Path) -> Option<PathBuf> {
    let starts = [
        std::env::current_dir().ok(),
        std::env::current_exe()
            .ok()
            .and_then(|exe| exe.parent().map(Path::to_path_buf)),
        Some(content.to_path_buf()),
    ];
    starts.into_iter().flatten().find_map(|start| {
        start
            .ancestors()
            .map(|folder| folder.join("content/packs"))
            .find(|packs| packs.join("game/pack.yaml").is_file())
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse(line: &str) -> Result<Options, String> {
        Options::parse(line.split_whitespace().map(str::to_owned))
    }

    #[test]
    fn reads_every_option() {
        let options = parse(
            "--content c --mods m1 --mods m2 --map area_a --exit 2 --variant night \
             --layers walls,exits --group-by prefix --filter ar --live tcp:7000 --size 800x600 --shot s.png",
        )
        .unwrap();
        assert_eq!(options.content, Some(PathBuf::from("c")));
        assert_eq!(options.mods.len(), 2);
        assert_eq!(options.exit, Some(2));
        assert_eq!(options.layers, Some(vec![Layer::Walls, Layer::Exits]));
        assert_eq!(options.group_by, Some(GroupBy::Prefix));
        assert_eq!(options.size, [800.0, 600.0]);
        assert_eq!(options.shot, Some(PathBuf::from("s.png")));
        assert!(parse("--layers walls,clouds").is_err());
        assert!(parse("--size 10x10").is_err());
        assert!(parse("--map").is_err());
        assert!(parse("--what").is_err());
        assert!(parse("-h").unwrap().help);
    }
}
