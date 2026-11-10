//! The player's content: dwx identify and dwx extract --progress into <data>/content.partial, then
//! dwx cook-pak --progress cooks the base pack into base.pak there, and the packs move into
//! <data>/content with an install.yaml record, exactly as the game's own first-run screen does
//! (packages/content/src/disc_install.cpp), so the game mounts the result with no further question.
//! An install from before base.pak keeps a loose base folder: convert() cooks it into base.pak
//! without reading the disc again (docs/adr/0017-cocinado-y-archivos-pak.md).

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
/// The base pack as an install leaves it, cooked into one archive; a base folder hides it.
pub const BASE_ARCHIVE: &str = "base.pak";
/// Where convert() cooks a loose base pack; outside content/, since it holds a pack with the same
/// id, and kept after a stop so the next try finds dwx's work.
pub const CONVERT_FOLDER: &str = "content.convert";
const COOK_PAK: &str = "cook-pak";
/// dwx cook-pak cooks in <archive>.cook beside the archive.
const COOKED_SUFFIX: &str = ".cook";
/// A replaced pack is renamed to this in the work folder, in one step, before it is removed.
const RETIRED_SUFFIX: &str = ".retired";
/// The share of the install's percent that the extraction takes; the cook into base.pak, most of
/// the time, takes the rest.
const EXTRACT_SHARE: u8 = 20;
const PACK_EXTENSION: &str = "pak";pub const INSTALL_RECORD: &str = "install.yaml";
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

/// Whether the content has the base pack: the archive an install leaves, or a loose folder.
pub fn has_base_pack(content: &Path) -> bool {
    content.join(BASE_ARCHIVE).is_file() || has_loose_base(content)
}

/// Whether the base pack is a loose folder, as dwx extract writes it (an install from before
/// base.pak, or a game whose dwx cannot cook one).
pub fn has_loose_base(content: &Path) -> bool {
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

/// A line of dwx extract --progress or dwx cook-pak --progress: "progress <percent> <step>".
pub fn parse_progress(line: &str) -> Option<Progress> {
    let rest = line.strip_prefix("progress ")?;
    let (percent, step) = rest.split_once(' ')?;
    let percent: u8 = percent.parse().ok()?;
    let step = step.trim();
    (percent <= 100 && !step.is_empty()).then(|| Progress { percent, step: step.to_string() })
}

/// The most lines of dwx's output a failure message carries.
const ERROR_LINES: usize = 20;
/// The most lines of dwx's output the log keeps of a failure.
const LOGGED_LINES: usize = 60;

/// dwx's lines that are not progress.
fn said(lines: &[String]) -> Vec<&str> {
    lines.iter().map(String::as_str).filter(|l| parse_progress(l).is_none()).collect()
}

/// What dwx said last about a failure: its last line starting with "dwx: ", without that, and the
/// indented lines after it ("  <file>: <why>"; its report on the other stream may come between
/// them); with no such line, its last lines that are not progress (a program that crashed says
/// nothing of its own). At most ERROR_LINES lines.
pub fn dwx_error(lines: &[String]) -> String {
    let said = said(lines);
    let tail: Vec<&str> = match said.iter().rposition(|l| l.starts_with("dwx: ")) {
        Some(at) => {
            let first = said[at].strip_prefix("dwx: ").unwrap_or_default();
            std::iter::once(first)
                .chain(said[at + 1..].iter().copied().filter(|l| l.starts_with([' ', '\t'])))
                .collect()
        }
        None => {
            let shown: Vec<&str> = said.iter().copied().filter(|l| !l.trim().is_empty()).collect();
            shown[shown.len().saturating_sub(ERROR_LINES)..].to_vec()
        }
    };
    let mut text = tail.iter().take(ERROR_LINES).copied().collect::<Vec<_>>().join("\n");
    if tail.len() > ERROR_LINES {
        text.push_str(&format!("\n  … {} more", tail.len() - ERROR_LINES));
    }
    text
}

/// Writes how dwx ended and its last lines that are not progress to the log, for a failure.
fn log_dwx_output(exit: Option<i32>, lines: &[String]) {
    let said = said(lines);
    let tail = &said[said.len().saturating_sub(LOGGED_LINES)..];
    // A crash on Windows ends with an NTSTATUS code, read in hexadecimal (0xC0000005...).
    let exit = exit.map_or_else(|| "no exit code".to_string(), |code| format!("exit code {code} ({:#x})", code as u32));
    log_line!("dwx ended with {exit}; its last {} lines:\n{}", tail.len(), tail.join("\n"));
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
        _ => {
            log_dwx_output(exit_code, lines);
            Err(Failure::new(code::CANNOT_READ, dwx_error(lines)))
        }
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

/// Whether the game's dwx cooks a pack into an archive (dwx cook-pak). Its usage, which it prints
/// when run without a command, names every command; a game older than base.pak has none.
pub fn dwx_cooks_archives(dwx: &Path) -> bool {
    let needle = format!("dwx {COOK_PAK} ");
    run_lines(Command::new(dwx), &AtomicBool::new(false), |_| {})
        .is_ok_and(|(_, lines)| lines.iter().any(|line| line.trim_start().starts_with(&needle)))
}

/// Runs dwx cook-pak --progress on a pack folder into an archive, handing on its progress.
fn cook_pak(
    dwx: &Path,
    pack: &Path,
    archive: &Path,
    cancel: &AtomicBool,
    failure: &'static str,
    mut on_progress: impl FnMut(Progress),
) -> Result<()> {
    log_line!("cooking {} into {}", pack.display(), archive.display());
    let mut command = Command::new(dwx);
    command.arg(COOK_PAK).arg(pack).arg("-o").arg(archive).arg("--progress");
    let (exit, lines) = run_lines(command, cancel, |line| {
        if let Some(progress) = parse_progress(line) {
            on_progress(progress);
        }
    })?;
    if exit != Some(0) {
        log_dwx_output(exit, &lines);
        return Err(Failure::new(failure, dwx_error(&lines)));
    }
    Ok(())
}

fn with_suffix(path: &Path, suffix: &str) -> PathBuf {
    let mut name = path.as_os_str().to_owned();
    name.push(suffix);
    PathBuf::from(name)
}

/// The other form of a pack in a content folder: the archive of a folder, the folder of an archive.
fn other_form(pack: &Path) -> PathBuf {
    if pack.extension().is_some_and(|e| e == PACK_EXTENSION) {
        pack.with_extension("")
    } else {
        with_suffix(pack, &format!(".{PACK_EXTENSION}"))
    }
}

/// Moves a pack out of content/ into a work folder in one step, then removes it: the game never
/// sees half of it.
fn retire(pack: &Path, work: &Path) -> Result<()> {
    if !pack.exists() {
        return Ok(());
    }
    let name = pack.file_name().unwrap_or_default().to_string_lossy();
    let retired = work.join(format!("{name}{RETIRED_SUFFIX}"));
    let _ = fs::remove_dir_all(&retired);
    let _ = fs::remove_file(&retired);
    fs::rename(pack, &retired).map_err(|e| Failure::new(code::EXTRACT_FAILED, format!("{}: {e}", pack.display())))?;
    if retired.is_dir() {
        let _ = fs::remove_dir_all(&retired);
    } else {
        let _ = fs::remove_file(&retired);
    }
    Ok(())
}

/// Moves each pack of the partial folder (an archive, or a folder with its manifest and a name
/// without a dot: never dwx's work beside them) into content/. It replaces the installed pack of the
/// same name, which goes only once the new one is in place, and then the other form of it (the base
/// folder of an older install, when base.pak comes): until then the game mounts the old one whole.
fn install_packs(partial: &Path, content: &Path) -> Result<()> {
    fs::create_dir_all(content)?;
    let mut packs = Vec::new();
    for entry in fs::read_dir(partial)?.flatten() {
        let path = entry.path();
        let archive = path.extension().is_some_and(|e| e == PACK_EXTENSION) && path.is_file();
        let folder = path.extension().is_none() && path.join("pack.yaml").is_file();
        if archive || folder {
            packs.push(path);
        }
    }
    for pack in packs {
        let target = content.join(pack.file_name().unwrap_or_default());
        if target.is_dir() {
            retire(&target, partial)?;
        }
        fs::rename(&pack, &target).map_err(|e| Failure::new(code::EXTRACT_FAILED, format!("{}: {e}", target.display())))?;
        retire(&other_form(&target), partial)?;
    }
    Ok(())
}

/// Extracts the disc into the data folder's content: dwx writes the partial folder and cooks its
/// base pack into base.pak there (a dwx that cannot leaves it loose), then each pack replaces the
/// installed one of the same name and the record is written. Anything else in content/ stays.
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
    let cooks = dwx_cooks_archives(dwx);
    log_line!("extracting {} into {} (base.pak: {cooks})", image.display(), partial.display());
    let extract_share = if cooks { EXTRACT_SHARE } else { 100 };
    let mut command = Command::new(dwx);
    command.arg("extract").arg("--out").arg(partial.join(BASE_PACK)).arg("--progress").arg(image);
    let outcome = run_lines(command, cancel, |line| {
        if let Some(progress) = parse_progress(line) {
            on_progress(Progress { percent: scaled(progress.percent, 0, extract_share), step: progress.step });
        }
    });
    let finish = || -> Result<()> {
        let (exit, lines) = outcome?;
        if exit != Some(0) {
            log_dwx_output(exit, &lines);
            return Err(Failure::new(code::EXTRACT_FAILED, dwx_error(&lines)));
        }
        if cooks {
            let archive = partial.join(BASE_ARCHIVE);
            cook_pak(dwx, &partial.join(BASE_PACK), &archive, cancel, code::EXTRACT_FAILED, |progress| {
                on_progress(Progress { percent: scaled(progress.percent, EXTRACT_SHARE, 100), step: progress.step });
            })?;
            let _ = fs::remove_dir_all(partial.join(BASE_PACK));
            let _ = fs::remove_dir_all(with_suffix(&archive, COOKED_SUFFIX));
        }
        install_packs(&partial, &content)?;
        if !has_base_pack(&content) {
            log_dwx_output(exit, &lines);
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

/// A step's percent as a share of the whole job, from `from` to `to`.
fn scaled(percent: u8, from: u8, to: u8) -> u8 {
    from + (u16::from(percent.min(100)) * u16::from(to - from) / 100) as u8
}

/// What convert() did.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Conversion {
    /// The loose base pack is now base.pak.
    Converted,
    /// There was no loose base pack (or only one a developer keeps beside base.pak).
    NothingToDo,
    /// The game's dwx cannot cook an archive: the base pack stays loose, and the game plays it.
    Unsupported,
}

/// Cooks a loose base pack (an install from before base.pak) into content/base.pak, without
/// reading the disc again. dwx cook-pak works in CONVERT_FOLDER; once it checked the archive, the
/// archive moves into content/ and only then the loose folder goes (renamed away in one step), so
/// a stop at any point leaves content/ with a whole base pack: the loose one until the archive is
/// in place, then the archive. A failed or stopped conversion keeps the loose folder. With nothing
/// to convert, removes what an earlier conversion left.
pub fn convert(dwx: &Path, data: &Path, cancel: &AtomicBool, on_progress: impl FnMut(Progress)) -> Result<Conversion> {
    let content = content_folder(data);
    let work = data.join(CONVERT_FOLDER);
    let installed = content.join(BASE_ARCHIVE);
    if has_loose_base(&content) && installed.is_file() && work.is_dir() {
        // A conversion that stopped after the archive was in place: the loose folder still goes.
        retire(&content.join(BASE_PACK), &work).map_err(|f| Failure::new(code::CONVERT_FAILED, f.detail))?;
    }
    if !has_loose_base(&content) || installed.is_file() {
        let _ = fs::remove_dir_all(&work);
        return Ok(Conversion::NothingToDo);
    }
    if !dwx_cooks_archives(dwx) {
        log_line!("the game's dwx cannot cook base.pak: the base pack stays loose");
        return Ok(Conversion::Unsupported);
    }
    fs::create_dir_all(&work)?;
    let archive = work.join(BASE_ARCHIVE);
    cook_pak(dwx, &content.join(BASE_PACK), &archive, cancel, code::CONVERT_FAILED, on_progress)?;
    fs::rename(&archive, &installed).map_err(|e| Failure::new(code::CONVERT_FAILED, format!("{}: {e}", installed.display())))?;
    retire(&content.join(BASE_PACK), &work).map_err(|f| Failure::new(code::CONVERT_FAILED, f.detail))?;
    let _ = fs::remove_dir_all(&work);
    log_line!("the base pack is now {}", installed.display());
    Ok(Conversion::Converted)
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
    fn a_base_archive_is_a_base_pack() {
        let dir = tempfile::tempdir().unwrap();
        fs::write(dir.path().join(BASE_ARCHIVE), "x").unwrap();
        assert!(has_base_pack(dir.path()));
        assert!(!has_loose_base(dir.path()));
        assert!(is_outdated(dir.path(), Some(12)));
        assert_eq!(scaled(50, 20, 100), 60);
        assert_eq!(scaled(100, 0, 20), 20);
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

    #[test]
    fn dwx_errors_keep_their_problem_lines() {
        let lines = |l: &[&str]| l.iter().map(|s| s.to_string()).collect::<Vec<_>>();
        let output = lines(&[
            "progress 90 check",
            "dwx: warning: a.png opened only when read again alone: cannot open 'a.png'",
            "dwx: engine check failed: the engine cannot open 2 of its assets:",
            "  base/b.png: cannot open 'base/b.png': permission denied",
            "Disc: SLUS, 1 files",
            "  base/c.yaml: not UTF-8",
        ]);
        assert_eq!(
            dwx_error(&output),
            "engine check failed: the engine cannot open 2 of its assets:\n  base/b.png: cannot open 'base/b.png': permission denied\n  base/c.yaml: not UTF-8"
        );
        // A long list is cut, saying how much was left out.
        let mut long = vec!["dwx: the engine cannot open 30 of its assets:".to_string()];
        long.extend((0..30).map(|i| format!("  f{i}: bad")));
        let text = dwx_error(&long);
        assert_eq!(text.lines().count(), ERROR_LINES + 1);
        assert!(text.ends_with("… 11 more"));
        // A dwx that crashed says no "dwx: " line: its last lines that are not progress stand in.
        let crashed = lines(&["progress 10 read", "Disc: SLUS, 1 files", "", "progress 12 read", "Assertion failed: x"]);
        assert_eq!(dwx_error(&crashed), "Disc: SLUS, 1 files\nAssertion failed: x");
        assert_eq!(dwx_error(&lines(&["progress 1 read"])), "");
    }

    /// A stand-in for dwx: without a command it prints its usage (naming cook-pak when `cooks`);
    /// extract writes <out>/pack.yaml and a ui_original pack beside it, like the real one; cook-pak
    /// writes the archive (a fake one) after leaving its cooked folder, or fails with `cook_exit`.
    #[cfg(unix)]
    fn fake_dwx(dir: &Path, cooks: bool, cook_exit: i32) -> PathBuf {
        use std::os::unix::fs::PermissionsExt;
        let dwx = dir.join("dwx");
        let usage = if cooks { "  dwx cook-pak <pack folder> -o <file.pak> [--progress]" } else { "  dwx pack <pack folder> -o <file.pak>" };
        let script = format!(
            "#!/bin/sh\n\
             case \"$1\" in\n\
             extract) out=\"$3\"; echo 'progress 10 read' >&2; mkdir -p \"$out\" \"$out/../ui_original\"; \
               echo 'id: base' > \"$out/pack.yaml\"; echo 'id: ui_original' > \"$out/../ui_original/pack.yaml\"; \
               echo 'progress 100 done' >&2;;\n\
             cook-pak) [ -f \"$2/pack.yaml\" ] || exit 2; echo 'progress 50 cook' >&2; mkdir -p \"$4.cook\"; \
               echo 'id: base' > \"$4.cook/pack.yaml\"; \
               [ {cook_exit} -eq 0 ] || {{ echo 'dwx: the cook failed' >&2; exit {cook_exit}; }}; \
               echo 'archive' > \"$4\"; rm -rf \"$4.cook\"; echo 'progress 100 done' >&2;;\n\
             *) echo 'Usage:' >&2; echo '{usage}' >&2; exit 2;;\n\
             esac\n"
        );
        fs::write(&dwx, script).unwrap();
        fs::set_permissions(&dwx, fs::Permissions::from_mode(0o755)).unwrap();
        dwx
    }

    /// The names in a folder, sorted.
    fn names(folder: &Path) -> Vec<String> {
        let mut names: Vec<String> = fs::read_dir(folder).unwrap().flatten().map(|e| e.file_name().to_string_lossy().into_owned()).collect();
        names.sort();
        names
    }

    #[cfg(unix)]
    #[test]
    fn a_fresh_install_ends_with_base_pak_alone() {
        let dir = tempfile::tempdir().unwrap();
        let dwx = fake_dwx(dir.path(), true, 0);
        assert!(dwx_cooks_archives(&dwx));
        let data = dir.path().join("data");
        fs::create_dir_all(data.join("content")).unwrap();
        fs::write(data.join("content/keep.txt"), "mine").unwrap();
        let image = dir.path().join("disc.cue");
        fs::write(&image, "").unwrap();
        let mut seen = Vec::new();
        extract(&dwx, &image, &image, Some(12), &data, &AtomicBool::new(false), |p| seen.push((p.percent, p.step))).unwrap();
        // The extraction takes the first fifth of the bar, the cook the rest.
        let percents: Vec<u8> = seen.iter().map(|(p, _)| *p).collect();
        assert_eq!(percents, vec![2, 20, 60, 100]);
        assert_eq!(seen[2].1, "cook");
        let content = data.join("content");
        assert_eq!(names(&content), vec!["base.pak", "install.yaml", "keep.txt", "ui_original"]);
        assert!(has_base_pack(&content));
        assert!(!has_loose_base(&content));
        assert!(!data.join(PARTIAL_FOLDER).exists());
        assert_eq!(read_record(&content).unwrap().extractor_version, 12);

        // Reading the disc again replaces the archive; a failed cook leaves the install as it was.
        let failing = fake_dwx(dir.path(), true, 2);
        let failure = extract(&failing, &image, &image, Some(12), &data, &AtomicBool::new(false), |_| {}).unwrap_err();
        assert_eq!(failure, Failure::new(code::EXTRACT_FAILED, "the cook failed"));
        assert_eq!(names(&content), vec!["base.pak", "install.yaml", "keep.txt", "ui_original"]);
        assert!(!data.join(PARTIAL_FOLDER).exists());
    }

    #[cfg(unix)]
    #[test]
    fn a_fake_dwx_extracts_into_place() {
        // A game whose dwx cannot cook base.pak installs the loose base pack, as before.
        let dir = tempfile::tempdir().unwrap();
        let dwx = fake_dwx(dir.path(), false, 0);
        assert!(!dwx_cooks_archives(&dwx));
        let data = dir.path().join("data");
        fs::create_dir_all(data.join("content/base")).unwrap();
        fs::write(data.join("content/base/old.txt"), "old").unwrap();
        fs::write(data.join("content/base.pak"), "a newer game's").unwrap();
        fs::write(data.join("content/keep.txt"), "mine").unwrap();
        let image = dir.path().join("disc.cue");
        fs::write(&image, "").unwrap();
        let mut seen = Vec::new();
        extract(&dwx, &image, &image, Some(12), &data, &AtomicBool::new(false), |p| seen.push(p.percent)).unwrap();
        assert_eq!(seen, vec![10, 100]);
        let content = data.join("content");
        assert!(has_loose_base(&content));
        assert!(content.join("ui_original").is_dir());
        assert!(!content.join("base/old.txt").exists());
        assert!(!content.join(BASE_ARCHIVE).exists());
        assert!(content.join("keep.txt").exists());
        assert!(!data.join(PARTIAL_FOLDER).exists());
        assert_eq!(read_record(&content).unwrap().extractor_version, 12);

        fs::write(&dwx, "#!/bin/sh\necho 'dwx: the disc is scratched' >&2\nexit 1\n").unwrap();
        let failure = extract(&dwx, &image, &image, Some(12), &data, &AtomicBool::new(false), |_| {}).unwrap_err();
        assert_eq!(failure, Failure::new(code::EXTRACT_FAILED, "the disc is scratched"));
        // A failed extraction leaves the installed content as it was.
        assert!(has_base_pack(&content));
    }

    #[cfg(unix)]
    #[test]
    fn a_loose_install_converts_to_base_pak_and_a_stop_keeps_it_whole() {
        let dir = tempfile::tempdir().unwrap();
        let data = dir.path().join("data");
        let content = data.join("content");
        fs::create_dir_all(content.join("base/map")).unwrap();
        fs::write(content.join("base/pack.yaml"), "id: base\n").unwrap();
        fs::write(content.join("base/map/a.png"), "png").unwrap();
        write_record(&content, &InstallRecord { extractor_version: 12, image: "/d.cue".into() }).unwrap();
        let record = fs::read(content.join(INSTALL_RECORD)).unwrap();

        // A cook that fails, or a player who stops it, leaves the loose pack as it was.
        let failing = fake_dwx(dir.path(), true, 2);
        let failure = convert(&failing, &data, &AtomicBool::new(false), |_| {}).unwrap_err();
        assert_eq!(failure, Failure::new(code::CONVERT_FAILED, "the cook failed"));
        assert!(has_loose_base(&content));
        assert!(!content.join(BASE_ARCHIVE).exists());
        let stopped = convert(&failing, &data, &AtomicBool::new(true), |_| {}).unwrap_err();
        assert_eq!(stopped.code, code::CANCELLED);
        assert_eq!(names(&content), vec!["base", "install.yaml"]);

        // A game too old to cook one leaves it loose too.
        assert_eq!(convert(&fake_dwx(dir.path(), false, 0), &data, &AtomicBool::new(false), |_| {}).unwrap(), Conversion::Unsupported);
        assert!(has_loose_base(&content));

        let dwx = fake_dwx(dir.path(), true, 0);
        let mut seen = Vec::new();
        assert_eq!(convert(&dwx, &data, &AtomicBool::new(false), |p| seen.push(p.step)).unwrap(), Conversion::Converted);
        assert_eq!(seen, vec!["cook", "done"]);
        // Without reading the disc again: the record stays, only the base pack changed form.
        assert_eq!(names(&content), vec!["base.pak", "install.yaml"]);
        assert_eq!(fs::read(content.join(INSTALL_RECORD)).unwrap(), record);
        assert!(!data.join(CONVERT_FOLDER).exists());
        assert_eq!(convert(&dwx, &data, &AtomicBool::new(false), |_| {}).unwrap(), Conversion::NothingToDo);

        // A conversion that stopped once the archive was in place finishes on the next start.
        fs::create_dir_all(content.join("base")).unwrap();
        fs::write(content.join("base/pack.yaml"), "id: base\n").unwrap();
        fs::create_dir_all(data.join(CONVERT_FOLDER)).unwrap();
        assert_eq!(convert(&dwx, &data, &AtomicBool::new(false), |_| {}).unwrap(), Conversion::NothingToDo);
        assert_eq!(names(&content), vec!["base.pak", "install.yaml"]);
        assert!(!data.join(CONVERT_FOLDER).exists());
    }
}
