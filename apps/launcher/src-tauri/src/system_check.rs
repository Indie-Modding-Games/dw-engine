//! What this computer needs to install and play, checked before it is needed: free space in the
//! data folder, and a GPU the renderer can use (`dwe-game --check-system`, rhi/system_check.hpp).
//! Nothing is installed on the player's computer: the game package brings its own Vulkan loader
//! and, on Linux, Mesa's software Vulkan driver (lib/lavapipe, scripts/package_linux.sh). When no
//! GPU can run the game the launcher starts it on that driver and says so; only when even that
//! cannot work does the page show what to do (the driver page of the GPU's maker).

use serde::Serialize;
use std::ffi::OsString;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

use crate::log_line;
use crate::store::Game;

/// Reading the disc writes the content next to a partial copy, may unpack an archive and
/// downloads the game first: about 3 GB at the peak.
pub const INSTALL_BYTES: u64 = 3 * 1024 * 1024 * 1024;
/// Playing writes saves, logs and reports.
pub const PLAY_BYTES: u64 = 200 * 1024 * 1024;
/// The probe makes a Vulkan instance and quits; a driver that hangs longer counts as none.
pub const PROBE_TIMEOUT: Duration = Duration::from_secs(20);
pub const PROBE_FLAG: &str = "--check-system";
/// The software driver of a Linux package, from the game's folder.
pub const SOFTWARE_DRIVER: &str = "lib/lavapipe";
const SOFTWARE_MANIFEST: &str = "lvp_icd.json";

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "kebab-case")]
pub enum ProbeResult {
    Ok,
    NoVulkan,
    NoDevice,
    Unsuitable,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Gpu {
    pub name: String,
    pub vendor_id: u32,
    /// discrete, integrated, virtual, software or other.
    pub kind: String,
    pub api: String,
    pub driver: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Probe {
    pub result: ProbeResult,
    pub detail: String,
    pub devices: u32,
    pub gpu: Option<Gpu>,
}

/// Reads what `dwe-game --check-system` printed; None when it is not that (a game older than
/// the flag says "bad argument", a game that could not start prints nothing).
pub fn parse(text: &str) -> Option<Probe> {
    let mut lines = text.lines().map(str::trim_end);
    if lines.next()? != "dwe-check-system 1" {
        return None;
    }
    let mut values = std::collections::HashMap::new();
    for line in lines {
        if let Some((key, value)) = line.split_once('=') {
            values.insert(key, value);
        }
    }
    let result = match *values.get("result")? {
        "ok" => ProbeResult::Ok,
        "no-vulkan" => ProbeResult::NoVulkan,
        "no-device" => ProbeResult::NoDevice,
        "unsuitable" => ProbeResult::Unsuitable,
        _ => return None,
    };
    let text = |key: &str| values.get(key).map(|v| v.to_string()).unwrap_or_default();
    let gpu = values.get("gpu").map(|name| Gpu {
        name: name.to_string(),
        vendor_id: values
            .get("vendor")
            .and_then(|v| u32::from_str_radix(v.trim_start_matches("0x"), 16).ok())
            .unwrap_or(0),
        kind: text("kind"),
        api: text("api"),
        driver: text("driver"),
    });
    Some(Probe { result, detail: text("detail"), devices: text("devices").parse().unwrap_or(0), gpu })
}

/// How the game will draw.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "kebab-case")]
pub enum Graphics {
    /// A GPU the renderer can use.
    Hardware,
    /// Only the computer's own software driver: it works, slowly.
    Software,
    /// No GPU can run it: the package's software driver (slow, but it starts).
    Fallback,
    /// Nothing can run it: the page says what to do.
    Blocked,
    /// No answer (a game older than the probe, or no game yet): play as before.
    Unknown,
}

/// The decision from the probe with the computer's drivers and, when that found nothing, the
/// probe with the package's software driver (None: there is none, or it gave no answer).
pub fn decide(system: Option<&Probe>, fallback: Option<&Probe>) -> Graphics {
    match system {
        None => Graphics::Unknown,
        Some(probe) if probe.result == ProbeResult::Ok => {
            if probe.gpu.as_ref().is_some_and(|g| g.kind == "software") {
                Graphics::Software
            } else {
                Graphics::Hardware
            }
        }
        Some(_) => match fallback {
            Some(probe) if probe.result == ProbeResult::Ok => Graphics::Fallback,
            _ => Graphics::Blocked,
        },
    }
}

/// The GPU makers whose driver page the page offers.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "kebab-case")]
pub enum Vendor {
    Nvidia,
    Amd,
    Intel,
    Other,
}

pub fn vendor(id: u32) -> Vendor {
    match id {
        0x10de => Vendor::Nvidia,
        0x1002 | 0x1022 => Vendor::Amd,
        0x8086 => Vendor::Intel,
        _ => Vendor::Other,
    }
}

/// The maker's own page for its drivers (only these are opened, never a page an add-on or a
/// release names).
pub fn driver_page(vendor: Vendor) -> Option<&'static str> {
    match vendor {
        Vendor::Nvidia => Some("https://www.nvidia.com/Download/index.aspx"),
        Vendor::Amd => Some("https://www.amd.com/en/support/download/drivers.html"),
        Vendor::Intel => Some("https://www.intel.com/content/www/us/en/support/detect.html"),
        Vendor::Other => None,
    }
}

/// Windows' own updates, which also bring GPU drivers.
pub const WINDOWS_UPDATE: &str = "ms-settings:windowsupdate";

/// The package's software driver manifest, when this game has one (Linux packages).
pub fn software_manifest(game: &Game) -> Option<PathBuf> {
    let manifest = game.dir.join(SOFTWARE_DRIVER).join(SOFTWARE_MANIFEST);
    manifest.is_file().then_some(manifest)
}

/// The environment that makes the game use only the package's software driver: the loader reads
/// VK_DRIVER_FILES (VK_ICD_FILENAMES before 1.3.207), and the driver finds the libraries it
/// brings through LD_LIBRARY_PATH.
pub fn software_env(manifest: &Path) -> Vec<(OsString, OsString)> {
    let folder = manifest.parent().unwrap_or(manifest);
    let mut libraries = OsString::from(folder.as_os_str());
    if let Some(previous) = std::env::var_os("LD_LIBRARY_PATH").filter(|p| !p.is_empty()) {
        libraries.push(":");
        libraries.push(previous);
    }
    vec![
        ("VK_DRIVER_FILES".into(), manifest.as_os_str().to_owned()),
        ("VK_ICD_FILENAMES".into(), manifest.as_os_str().to_owned()),
        ("LD_LIBRARY_PATH".into(), libraries),
    ]
}

/// XDG_DATA_DIRS without the AppImage's own folders (`appdir`): the AppImage's start script puts
/// its folder first for the launcher's toolkit, and the Vulkan loader looks for the computer's
/// drivers in these folders. None: leave it unset, so the loader uses its defaults.
pub fn data_dirs_without(dirs: &str, appdir: &str) -> Option<String> {
    let kept: Vec<&str> = dirs.split(':').filter(|d| !d.is_empty() && !d.starts_with(appdir)).collect();
    (!kept.is_empty()).then(|| kept.join(":"))
}

/// Gives the game (and its probe) the computer's own driver folders when the launcher runs from
/// an AppImage.
pub fn outside_appimage(command: &mut Command) {
    let Some(appdir) = std::env::var("APPDIR").ok().filter(|d| !d.is_empty()) else {
        return;
    };
    match std::env::var("XDG_DATA_DIRS").ok().and_then(|dirs| data_dirs_without(&dirs, &appdir)) {
        Some(dirs) => command.env("XDG_DATA_DIRS", dirs),
        None => command.env_remove("XDG_DATA_DIRS"),
    };
}

/// Runs the probe of a game with an extra environment; None when it gives no answer in time.
pub fn run_probe(program: &Path, env: &[(OsString, OsString)], timeout: Duration) -> Option<Probe> {
    let mut command = Command::new(program);
    command.arg(PROBE_FLAG);
    outside_appimage(&mut command);
    command.envs(env.iter().map(|(k, v)| (k, v)));
    if let Some(dir) = program.parent() {
        command.current_dir(dir);
    }
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        const CREATE_NO_WINDOW: u32 = 0x0800_0000;
        command.creation_flags(CREATE_NO_WINDOW);
    }
    let mut child = match command.stdin(Stdio::null()).stdout(Stdio::piped()).stderr(Stdio::null()).spawn() {
        Ok(child) => child,
        Err(error) => {
            log_line!("the system check did not start: {error}");
            return None;
        }
    };
    let start = Instant::now();
    loop {
        match child.try_wait() {
            Ok(Some(_)) => break,
            Ok(None) if start.elapsed() < timeout => std::thread::sleep(Duration::from_millis(50)),
            _ => {
                let _ = child.kill();
                let _ = child.wait();
                log_line!("the system check gave no answer in {}s", timeout.as_secs());
                return None;
            }
        }
    }
    let mut output = String::new();
    child.stdout.take()?.read_to_string(&mut output).ok()?;
    parse(&output)
}

/// What the game's GPU check found, with the probe the decision rests on.
#[derive(Debug, Clone, Serialize)]
pub struct GraphicsCheck {
    pub graphics: Graphics,
    /// The GPU to name: the one the game uses, or the one whose driver needs an update.
    pub gpu: Option<Gpu>,
    pub vendor: Vendor,
    /// Why the computer's drivers were not enough (the probe's result), when they were not.
    pub reason: Option<ProbeResult>,
    pub detail: String,
    /// The extra environment the game starts with (the software driver's), not for the page.
    #[serde(skip)]
    pub env: Vec<(OsString, OsString)>,
}

impl GraphicsCheck {
    /// No answer: the game starts as before.
    pub fn unknown() -> Self {
        Self { graphics: Graphics::Unknown, gpu: None, vendor: Vendor::Other, reason: None, detail: String::new(), env: Vec::new() }
    }
}

/// Checks the game's GPU: first with the computer's drivers, then, when they cannot run it, with
/// the package's software driver.
pub fn check_graphics(game: &Game) -> GraphicsCheck {
    let program = game.game_program();
    let system = run_probe(&program, &[], PROBE_TIMEOUT);
    let failed = system.as_ref().is_some_and(|p| p.result != ProbeResult::Ok);
    let software = if failed { software_manifest(game) } else { None };
    let env = software.as_deref().map(software_env).unwrap_or_default();
    let fallback = software.as_ref().and_then(|_| run_probe(&program, &env, PROBE_TIMEOUT));
    let graphics = decide(system.as_ref(), fallback.as_ref());
    let gpu = system.as_ref().and_then(|p| p.gpu.clone());
    let check = GraphicsCheck {
        graphics,
        vendor: vendor(gpu.as_ref().map_or(0, |g| g.vendor_id)),
        gpu,
        reason: system.as_ref().filter(|p| p.result != ProbeResult::Ok).map(|p| p.result),
        detail: system.as_ref().map(|p| p.detail.clone()).unwrap_or_default(),
        env: if graphics == Graphics::Fallback { env } else { Vec::new() },
    };
    log_line!(
        "system check: {:?} ({}{})",
        check.graphics,
        check.gpu.as_ref().map_or("no GPU".to_string(), |g| format!("{}, {}", g.name, g.driver)),
        check.reason.map_or(String::new(), |r| format!(", {r:?}: {}", check.detail)),
    );
    check
}

/// A test override of the free space (bytes), like store::GAME_DIR_ENV: shows the space screens
/// without filling a disk.
pub const FREE_BYTES_ENV: &str = "DWE_LAUNCHER_FREE_BYTES";

/// Free space where the data folder is (or would be): the nearest folder that exists.
pub fn free_bytes(folder: &Path) -> Option<u64> {
    if let Some(bytes) = std::env::var(FREE_BYTES_ENV).ok().and_then(|v| v.parse().ok()) {
        return Some(bytes);
    }
    let existing = folder.ancestors().find(|p| p.is_dir())?;
    free_bytes_at(existing)
}

#[cfg(unix)]
fn free_bytes_at(folder: &Path) -> Option<u64> {
    use std::os::unix::ffi::OsStrExt;
    let path = std::ffi::CString::new(folder.as_os_str().as_bytes()).ok()?;
    let mut stats: libc::statvfs = unsafe { std::mem::zeroed() };
    // SAFETY: a valid C string and a statvfs the call fills in.
    if unsafe { libc::statvfs(path.as_ptr(), &mut stats) } != 0 {
        return None;
    }
    Some((stats.f_bavail as u64).saturating_mul(stats.f_frsize as u64))
}

#[cfg(windows)]
fn free_bytes_at(folder: &Path) -> Option<u64> {
    use std::os::windows::ffi::OsStrExt;
    let wide: Vec<u16> = folder.as_os_str().encode_wide().chain(std::iter::once(0)).collect();
    let mut available = 0u64;
    // SAFETY: a nul-terminated path and a u64 the call fills in; the other outputs may be null.
    let done = unsafe {
        windows_sys::Win32::Storage::FileSystem::GetDiskFreeSpaceExW(
            wide.as_ptr(),
            &mut available,
            std::ptr::null_mut(),
            std::ptr::null_mut(),
        )
    };
    (done != 0).then_some(available)
}

/// Free space of the data folder against what installing and playing need.
#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
pub struct Space {
    pub folder: String,
    /// None when the system did not say (then nothing is refused).
    pub free: Option<u64>,
    pub install_needed: u64,
    pub play_needed: u64,
    pub install_ok: bool,
    pub play_ok: bool,
}

pub fn enough(free: Option<u64>, needed: u64) -> bool {
    free.is_none_or(|free| free >= needed)
}

pub fn space(folder: &Path, free: Option<u64>) -> Space {
    Space {
        folder: folder.display().to_string(),
        free,
        install_needed: INSTALL_BYTES,
        play_needed: PLAY_BYTES,
        install_ok: enough(free, INSTALL_BYTES),
        play_ok: enough(free, PLAY_BYTES),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const NVIDIA: &str = "dwe-check-system 1\nresult=ok\ndetail=\ndevices=2\ngpu=NVIDIA GeForce RTX 2060\n\
                          vendor=0x10de\nkind=discrete\napi=1.4.329\ndriver=NVIDIA 595.99.02\n";

    fn probe(text: &str) -> Probe {
        parse(text).expect("a probe")
    }

    #[test]
    fn the_probe_output_is_read() {
        let p = probe(NVIDIA);
        assert_eq!(p.result, ProbeResult::Ok);
        assert_eq!(p.devices, 2);
        let gpu = p.gpu.unwrap();
        assert_eq!((gpu.name.as_str(), gpu.vendor_id, gpu.kind.as_str()), ("NVIDIA GeForce RTX 2060", 0x10de, "discrete"));
        assert_eq!((gpu.api.as_str(), gpu.driver.as_str()), ("1.4.329", "NVIDIA 595.99.02"));

        let none = probe("dwe-check-system 1\r\nresult=no-vulkan\r\ndetail=windowing_extensions_not_present\r\ndevices=0\r\n");
        assert_eq!(none.result, ProbeResult::NoVulkan);
        assert_eq!(none.detail, "windowing_extensions_not_present");
        assert!(none.gpu.is_none());

        let old = probe("dwe-check-system 1\nresult=unsuitable\ndetail=x=y\ndevices=1\ngpu=Old\nvendor=0x8086\nkind=integrated\napi=1.2.0\ndriver=0x1\n");
        assert_eq!(old.detail, "x=y");
        assert_eq!(old.gpu.unwrap().vendor_id, 0x8086);
    }

    #[test]
    fn anything_else_is_no_answer() {
        assert!(parse("").is_none());
        assert!(parse("bad argument '--check-system'; usage: dwe-game ...").is_none());
        assert!(parse("dwe-check-system 2\nresult=ok\n").is_none());
        assert!(parse("dwe-check-system 1\ndevices=1\n").is_none());
        assert!(parse("dwe-check-system 1\nresult=maybe\n").is_none());
    }

    #[test]
    fn the_decision_falls_back_to_the_software_driver() {
        let gpu = probe(NVIDIA);
        let software = probe("dwe-check-system 1\nresult=ok\ndetail=\ndevices=1\ngpu=llvmpipe\nvendor=0x10005\nkind=software\napi=1.4.318\ndriver=Mesa\n");
        let none = probe("dwe-check-system 1\nresult=no-device\ndetail=\ndevices=0\n");
        assert_eq!(decide(Some(&gpu), None), Graphics::Hardware);
        assert_eq!(decide(Some(&software), None), Graphics::Software);
        assert_eq!(decide(Some(&none), Some(&software)), Graphics::Fallback);
        assert_eq!(decide(Some(&none), None), Graphics::Blocked);
        assert_eq!(decide(Some(&none), Some(&none)), Graphics::Blocked);
        assert_eq!(decide(None, None), Graphics::Unknown);
    }

    #[test]
    fn each_maker_has_its_own_driver_page() {
        assert_eq!(vendor(0x10de), Vendor::Nvidia);
        assert_eq!(vendor(0x1002), Vendor::Amd);
        assert_eq!(vendor(0x8086), Vendor::Intel);
        assert_eq!(vendor(0x10005), Vendor::Other);
        assert_eq!(vendor(0), Vendor::Other);
        for maker in [Vendor::Nvidia, Vendor::Amd, Vendor::Intel] {
            let page = tauri::Url::parse(driver_page(maker).unwrap()).unwrap();
            assert_eq!(page.scheme(), "https");
        }
        assert!(driver_page(Vendor::Nvidia).unwrap().contains("nvidia.com"));
        assert!(driver_page(Vendor::Amd).unwrap().contains("amd.com"));
        assert!(driver_page(Vendor::Intel).unwrap().contains("intel.com"));
        assert_eq!(driver_page(Vendor::Other), None);
    }

    #[test]
    fn the_appimage_folders_are_left_out_of_the_driver_search() {
        assert_eq!(data_dirs_without("/tmp/.mount_x/usr/share:/usr/share:/usr/local/share", "/tmp/.mount_x").as_deref(), Some("/usr/share:/usr/local/share"));
        assert_eq!(data_dirs_without("/tmp/.mount_x/usr/share:", "/tmp/.mount_x"), None);
        assert_eq!(data_dirs_without("/usr/share", "/tmp/.mount_x").as_deref(), Some("/usr/share"));
    }

    #[test]
    fn free_space_decides_install_and_play() {
        assert!(enough(None, INSTALL_BYTES));
        assert!(enough(Some(INSTALL_BYTES), INSTALL_BYTES));
        assert!(!enough(Some(INSTALL_BYTES - 1), INSTALL_BYTES));
        let tight = space(Path::new("/d"), Some(PLAY_BYTES + 1));
        assert!(tight.play_ok && !tight.install_ok);
        let full = space(Path::new("/d"), Some(1));
        assert!(!full.play_ok && !full.install_ok);
    }

    #[test]
    fn free_space_is_read_from_the_nearest_folder() {
        let dir = tempfile::tempdir().unwrap();
        let free = free_bytes(&dir.path().join("not/yet/there")).expect("free space");
        assert!(free > 0);
    }

    #[test]
    fn the_software_driver_is_found_in_the_package() {
        let dir = tempfile::tempdir().unwrap();
        let game = Game { dir: dir.path().into(), version: "1.0.0".into(), commit: String::new(), extractor_version: None };
        assert!(software_manifest(&game).is_none());
        let folder = dir.path().join(SOFTWARE_DRIVER);
        std::fs::create_dir_all(&folder).unwrap();
        std::fs::write(folder.join(SOFTWARE_MANIFEST), "{}").unwrap();
        let manifest = software_manifest(&game).unwrap();
        let env = software_env(&manifest);
        assert_eq!(env[0], ("VK_DRIVER_FILES".into(), manifest.clone().into_os_string()));
        assert_eq!(env[1].1, manifest.into_os_string());
        assert!(env[2].1.to_string_lossy().starts_with(&*folder.to_string_lossy()));
    }

    #[cfg(unix)]
    #[test]
    fn a_probe_that_hangs_or_says_nothing_is_no_answer() {
        use std::os::unix::fs::PermissionsExt;
        let dir = tempfile::tempdir().unwrap();
        let write = |name: &str, body: &str| {
            let path = dir.path().join(name);
            std::fs::write(&path, format!("#!/bin/sh\n{body}\n")).unwrap();
            std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o755)).unwrap();
            path
        };
        let answers = write("answers", &format!("[ \"$1\" = --check-system ] && printf '{}' \"$VK_DRIVER_FILES\"", "dwe-check-system 1\\nresult=ok\\ndetail=%s\\ndevices=1\\n"));
        let env = vec![("VK_DRIVER_FILES".into(), "/x.json".into())];
        assert_eq!(run_probe(&answers, &env, PROBE_TIMEOUT).unwrap().detail, "/x.json");
        let silent = write("silent", "echo \"bad argument '$1'\" >&2; exit 1");
        assert!(run_probe(&silent, &[], PROBE_TIMEOUT).is_none());
        let hangs = write("hangs", "exec sleep 30");
        let start = Instant::now();
        assert!(run_probe(&hangs, &[], Duration::from_millis(300)).is_none());
        assert!(start.elapsed() < Duration::from_secs(5));
        assert!(run_probe(&dir.path().join("missing"), &[], PROBE_TIMEOUT).is_none());
    }
}
