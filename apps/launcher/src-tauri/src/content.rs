//! The player's content: dwx identify and dwx extract --progress into <data>/content.partial,
//! then the packs move into <data>/content with an install.yaml record, exactly as the game's own
//! first-run screen does (apps/content/src/disc_install.cpp), so the game mounts the result with no
//! further question.

use std::fs;
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc;
use std::time::Duration;

use crate::failure::{code, Failure, Result};
use crate::log_line;

pub const CONTENT_FOLDER: &str = "content";
pub const PARTIAL_FOLDER: &str = "content.partial";
pub const BASE_PACK: &str = "base";
pub const INSTALL_RECORD: &str = "install.yaml";
const RECORD_VERSION_KEY: &str = "extractor_version";
const RECORD_IMAGE_KEY: &str = "disc_image";

#[derive(Debug, Clone, PartialEq)]
pub struct InstallRecord {
    pub extractor_version: i32,
    pub image: PathBuf,
}

pub fn content_folder(data: &Path) -> PathBuf {
    data.join(CONTENT_FOLDER)
}

pub fn has_base_pack(content: &Path) -> bool {
    content.join(BASE_PACK).join("pack.yaml").is_file()
}

fn unquote(text: &str) -> String {
    let text = text.trim();
    if let Some(inner) = text.strip_prefix('"').and_then(|t| t.strip_suffix('"')) {
        let mut out = String::new();
        let mut chars = inner.chars();
        while let Some(c) = chars.next() {
            if c == '\\' {
                match chars.next() {
                    Some('n') => out.push('\n'),
                    Some('t') => out.push('\t'),
                    Some(other) => out.push(other),
                    None => {}
                }
            } else {
                out.push(c);
            }
        }
        out
    } else if let Some(inner) = text.strip_prefix('\'').and_then(|t| t.strip_suffix('\'')) {
        inner.replace("''", "'")
    } else {
        text.to_string()
    }
}

fn quote(text: &str) -> String {
    let mut out = String::from("\"");
    for c in text.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\t' => out.push_str("\\t"),
            other => out.push(other),
        }
    }
    out.push('"');
    out
}

/// The record of a content folder (the two keys the game writes); nothing without one.
pub fn read_record(content: &Path) -> Option<InstallRecord> {
    let text = fs::read_to_string(content.join(INSTALL_RECORD)).ok()?;
    let mut version = None;
    let mut image = PathBuf::new();
    for line in text.lines() {
        if line.trim_start().starts_with('#') {
            continue;
        }
        let Some((key, value)) = line.split_once(':') else { continue };
        match key.trim() {
            RECORD_VERSION_KEY => version = unquote(value).parse().ok(),
            RECORD_IMAGE_KEY => image = PathBuf::from(unquote(value)),
            _ => {}
        }
    }
    Some(InstallRecord { extractor_version: version?, image })
}

pub fn write_record(content: &Path, record: &InstallRecord) -> Result<()> {
    let text = format!(
        "# Written by the launcher when it read the player's disc; another extractor version reads it again.\n{RECORD_VERSION_KEY}: {}\n{RECORD_IMAGE_KEY}: {}\n",
        record.extractor_version,
        quote(&record.image.to_string_lossy())
    );
    crate::store::write_atomically(&content.join(INSTALL_RECORD), text.as_bytes())
}

/// Whether the installed content must be read again for a game whose dwx has this extractor
/// version: it has a base pack, but no record or one of another version.
pub fn is_outdated(content: &Path, extractor_version: Option<i32>) -> bool {
    if !has_base_pack(content) {
        return false;
    }
    match (read_record(content), extractor_version) {
        (None, _) => true,
        (Some(record), Some(version)) => record.extractor_version != version,
        (Some(_), None) => false,
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Progress {
    pub percent: u8,
    pub step: String,
}

/// A line of dwx extract --progress: "progress <percent> <step>".
pub fn parse_progress(line: &str) -> Option<Progress> {
    let rest = line.strip_prefix("progress ")?;
    let (percent, step) = rest.split_once(' ')?;
    let percent: u8 = percent.parse().ok()?;
    let step = step.trim();
    (percent <= 100 && !step.is_empty()).then(|| Progress { percent, step: step.to_string() })
}

/// What dwx said last about a failure: its last line starting with "dwx: ", without that.
pub fn dwx_error(lines: &[String]) -> String {
    lines.iter().rev().find_map(|l| l.strip_prefix("dwx: ")).unwrap_or_default().to_string()
}

#[derive(Debug, Clone, PartialEq)]
pub enum Identity {
    Supported(String),
    OtherVersion(String),
    Unknown,
}

/// dwx identify's answer from its exit code and output.
pub fn read_identity(exit_code: Option<i32>, lines: &[String]) -> Result<Identity> {
    let said = lines
        .iter()
        .rev()
        .find(|l| l.starts_with("supported ") || l.starts_with("other-version ") || l.starts_with("unknown "));
    let version = said.and_then(|l| l.split_once(' ')).map(|(_, v)| v.trim().to_string()).unwrap_or_default();
    match exit_code {
        Some(0) => Ok(Identity::Supported(version)),
        Some(1) => Ok(Identity::Unknown),
        Some(3) => Ok(Identity::OtherVersion(version)),
        _ => Err(Failure::new(code::CANNOT_READ, dwx_error(lines))),
    }
}

/// Runs a program, handing every line it writes (either stream) to on_line, until it ends or
/// cancel is set (then it is killed). Returns its exit code and lines.
pub fn run_lines(
    mut command: Command,
    cancel: &AtomicBool,
    mut on_line: impl FnMut(&str),
) -> Result<(Option<i32>, Vec<String>)> {
    command.stdin(Stdio::null()).stdout(Stdio::piped()).stderr(Stdio::piped());
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        const CREATE_NO_WINDOW: u32 = 0x0800_0000;
        command.creation_flags(CREATE_NO_WINDOW);
    }
    let mut child = command.spawn().map_err(|e| Failure::new(code::NO_GAME, e.to_string()))?;
    let (sender, receiver) = mpsc::channel::<String>();
    let mut readers = Vec::new();
    for stream in [child.stdout.take().map(|s| Box::new(s) as Box<dyn std::io::Read + Send>),
                   child.stderr.take().map(|s| Box::new(s) as Box<dyn std::io::Read + Send>)]
        .into_iter()
        .flatten()
    {
        let sender = sender.clone();
        readers.push(std::thread::spawn(move || {
            for line in BufReader::new(stream).lines().map_while(std::result::Result::ok) {
                if sender.send(line).is_err() {
                    break;
                }
            }
        }));
    }
    drop(sender);
    let mut lines = Vec::new();
    loop {
        match receiver.recv_timeout(Duration::from_millis(100)) {
            Ok(line) => {
                on_line(&line);
                lines.push(line);
                continue;
            }
            Err(mpsc::RecvTimeoutError::Disconnected) => break,
            Err(mpsc::RecvTimeoutError::Timeout) => {}
        }
        if cancel.load(Ordering::SeqCst) {
            let _ = child.kill();
            let _ = child.wait();
            return Err(Failure::new(code::CANCELLED, ""));
        }
    }
    let status = child.wait()?;
    for reader in readers {
        let _ = reader.join();
    }
    if cancel.load(Ordering::SeqCst) {
        return Err(Failure::new(code::CANCELLED, ""));
    }
    Ok((status.code(), lines))
}

pub fn identify(dwx: &Path, image: &Path, cancel: &AtomicBool) -> Result<Identity> {
    log_line!("identifying {}", image.display());
    let mut command = Command::new(dwx);
    command.arg("identify").arg(image);
    let (exit, lines) = run_lines(command, cancel, |_| {})?;
    read_identity(exit, &lines)
}

/// Extracts the disc into the data folder's content: dwx writes the partial folder, then each
/// pack it wrote replaces the installed one of the same name and the record is written. Anything
/// else in content/ stays.
pub fn extract(
    dwx: &Path,
    image: &Path,
    recorded_image: &Path,
    extractor_version: Option<i32>,
    data: &Path,
    cancel: &AtomicBool,
    mut on_progress: impl FnMut(Progress),
) -> Result<()> {
    let content = content_folder(data);
    let partial = data.join(PARTIAL_FOLDER);
    let _ = fs::remove_dir_all(&partial);
    fs::create_dir_all(&partial)?;
    log_line!("extracting {} into {}", image.display(), partial.display());
    let mut command = Command::new(dwx);
    command.arg("extract").arg("--out").arg(partial.join(BASE_PACK)).arg("--progress").arg(image);
    let outcome = run_lines(command, cancel, |line| {
        if let Some(progress) = parse_progress(line) {
            on_progress(progress);
        }
    });
    let finish = || -> Result<()> {
        let (exit, lines) = outcome?;
        if exit != Some(0) {
            return Err(Failure::new(code::EXTRACT_FAILED, dwx_error(&lines)));
        }
        fs::create_dir_all(&content)?;
        for entry in fs::read_dir(&partial)?.flatten() {
            let target = content.join(entry.file_name());
            let _ = fs::remove_dir_all(&target);
            fs::rename(entry.path(), &target)
                .map_err(|e| Failure::new(code::EXTRACT_FAILED, format!("{}: {e}", target.display())))?;
        }
        if !has_base_pack(&content) {
            return Err(Failure::new(code::EXTRACT_FAILED, dwx_error(&lines)));
        }
        // dwx writes the version it was built with; a package always says which one that is.
        let version = extractor_version.unwrap_or_default();
        let image = std::path::absolute(recorded_image).unwrap_or_else(|_| recorded_image.to_path_buf());
        write_record(&content, &InstallRecord { extractor_version: version, image })
    };
    let result = finish();
    let _ = fs::remove_dir_all(&partial);
    match &result {
        Ok(()) => log_line!("the disc is installed in {}", content.display()),
        Err(failure) => log_line!("the disc was not installed: {failure}"),
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn records_round_trip_with_the_games_format() {
        let dir = tempfile::tempdir().unwrap();
        let record = InstallRecord { extractor_version: 12, image: PathBuf::from("/discs/My \"Game\" (USA).cue") };
        write_record(dir.path(), &record).unwrap();
        assert_eq!(read_record(dir.path()), Some(record));
        // As the game writes it (core::emitYaml, quoted scalar).
        fs::write(dir.path().join(INSTALL_RECORD), "# Written by the game\nextractor_version: 9\ndisc_image: \"/a/b c.cue\"\n").unwrap();
        assert_eq!(read_record(dir.path()).unwrap().image, PathBuf::from("/a/b c.cue"));
        assert_eq!(read_record(dir.path()).unwrap().extractor_version, 9);
    }

    #[test]
    fn outdated_content_is_seen() {
        let dir = tempfile::tempdir().unwrap();
        assert!(!is_outdated(dir.path(), Some(12)));
        fs::create_dir_all(dir.path().join("base")).unwrap();
        fs::write(dir.path().join("base/pack.yaml"), "id: base\n").unwrap();
        assert!(is_outdated(dir.path(), Some(12)));
        write_record(dir.path(), &InstallRecord { extractor_version: 12, image: "/d.cue".into() }).unwrap();
        assert!(!is_outdated(dir.path(), Some(12)));
        assert!(is_outdated(dir.path(), Some(13)));
        assert!(!is_outdated(dir.path(), None));
    }

    #[test]
    fn progress_lines_and_identify_answers_are_read() {
        assert_eq!(parse_progress("progress 42 extract"), Some(Progress { percent: 42, step: "extract".into() }));
        assert_eq!(parse_progress("progress 101 x"), None);
        assert_eq!(parse_progress("progress 5"), None);
        assert_eq!(parse_progress("dwx: no"), None);
        let lines = |l: &[&str]| l.iter().map(|s| s.to_string()).collect::<Vec<_>>();
        assert_eq!(read_identity(Some(0), &lines(&["supported us"])).unwrap(), Identity::Supported("us".into()));
        assert_eq!(read_identity(Some(3), &lines(&["other-version eu"])).unwrap(), Identity::OtherVersion("eu".into()));
        assert_eq!(read_identity(Some(1), &lines(&["unknown -"])).unwrap(), Identity::Unknown);
        let failure = read_identity(Some(2), &lines(&["dwx: cannot open the image"])).unwrap_err();
        assert_eq!(failure, Failure::new(code::CANNOT_READ, "cannot open the image"));
    }

    #[cfg(unix)]
    #[test]
    fn a_fake_dwx_extracts_into_place() {
        use std::os::unix::fs::PermissionsExt;
        let dir = tempfile::tempdir().unwrap();
        let dwx = dir.path().join("dwx");
        // Writes <out>/pack.yaml and a ui_original pack beside it, like the real one.
        fs::write(&dwx, "#!/bin/sh\nout=\"$3\"\necho 'progress 10 read' >&2\nmkdir -p \"$out\" \"$out/../ui_original\"\necho 'id: base' > \"$out/pack.yaml\"\necho 'progress 100 done' >&2\n").unwrap();
        fs::set_permissions(&dwx, fs::Permissions::from_mode(0o755)).unwrap();
        let data = dir.path().join("data");
        fs::create_dir_all(data.join("content/base")).unwrap();
        fs::write(data.join("content/base/old.txt"), "old").unwrap();
        fs::write(data.join("content/keep.txt"), "mine").unwrap();
        let image = dir.path().join("disc.cue");
        fs::write(&image, "").unwrap();
        let mut seen = Vec::new();
        extract(&dwx, &image, &image, Some(12), &data, &AtomicBool::new(false), |p| seen.push(p.percent)).unwrap();
        assert_eq!(seen, vec![10, 100]);
        let content = data.join("content");
        assert!(has_base_pack(&content));
        assert!(content.join("ui_original").is_dir());
        assert!(!content.join("base/old.txt").exists());
        assert!(content.join("keep.txt").exists());
        assert!(!data.join(PARTIAL_FOLDER).exists());
        assert_eq!(read_record(&content).unwrap().extractor_version, 12);

        fs::write(&dwx, "#!/bin/sh\necho 'dwx: the disc is scratched' >&2\nexit 1\n").unwrap();
        let failure = extract(&dwx, &image, &image, Some(12), &data, &AtomicBool::new(false), |_| {}).unwrap_err();
        assert_eq!(failure, Failure::new(code::EXTRACT_FAILED, "the disc is scratched"));
        // A failed extraction leaves the installed content as it was.
        assert!(has_base_pack(&content));
    }
}
