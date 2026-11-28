//! The game's graphics diagnosis (rhi/gpu_diagnosis.hpp; `diagnosis=<json>` from --check-system,
//! `dwe-diagnosis 1 <json>` in a failed start's log) as the page's steps and buttons, plus the
//! cases only the launcher sees (no Vulkan loader, a game older than the diagnosis).

use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::path::Path;

use crate::graphics_step::GraphicsStep;
use crate::system_check::{Graphics, Probe, ProbeResult};

pub const LOG_PREFIX: &str = "dwe-diagnosis 1 ";
/// Lets the game's notice say why it runs on the processor: `code|key=value|...`.
pub const REASON_ENV: &str = "DWE_GRAPHICS_REASON";

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default, rename_all = "camelCase")]
pub struct DiagnosedGpu {
    pub name: String,
    pub vendor: String,
    pub device: String,
    pub kind: String,
    pub api: String,
    pub driver: String,
    pub driver_version: String,
    #[serde(rename = "vramMiB")]
    pub vram_mib: u64,
    pub floor: String,
    pub missing: Vec<String>,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default)]
pub struct Adapter {
    pub name: String,
    pub vendor: String,
    pub device: String,
    pub driver: String,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default)]
pub struct Layer {
    pub name: String,
    pub manifest: String,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default)]
pub struct Reason {
    pub code: String,
    pub severity: String,
    pub fix: String,
    pub params: BTreeMap<String, String>,
    pub detail: String,
    pub gpu: Option<usize>,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default)]
pub struct Diagnosis {
    pub version: u32,
    pub os: String,
    pub gpus: Vec<DiagnosedGpu>,
    pub adapters: Vec<Adapter>,
    pub layers: Vec<Layer>,
    pub reasons: Vec<Reason>,
    pub chosen: Option<usize>,
    pub remote: bool,
    #[serde(rename = "virtual")]
    pub virtual_machine: bool,
    pub laptop: bool,
}

pub fn parse(json: &str) -> Option<Diagnosis> {
    serde_json::from_str(json.trim()).ok()
}

/// The last diagnosis a game wrote to its log.
pub fn from_log(text: &str) -> Option<Diagnosis> {
    text.lines().rev().find_map(|line| line.find(LOG_PREFIX).and_then(|at| parse(&line[at + LOG_PREFIX.len()..])))
}

/// The end of a game's log is enough: the line is written as the game gives up.
pub fn from_log_file(path: &Path) -> Option<Diagnosis> {
    use std::io::{Read, Seek, SeekFrom};
    let mut file = std::fs::File::open(path).ok()?;
    let length = file.metadata().ok()?.len();
    file.seek(SeekFrom::Start(length.saturating_sub(512 * 1024))).ok()?;
    let mut bytes = Vec::new();
    file.read_to_end(&mut bytes).ok()?;
    from_log(&String::from_utf8_lossy(&bytes))
}

fn reason(code: &str, severity: &str, fix: &str, params: &[(&str, &str)]) -> Reason {
    Reason {
        code: code.into(),
        severity: severity.into(),
        fix: fix.into(),
        params: params.iter().map(|(k, v)| (k.to_string(), v.to_string())).collect(),
        detail: String::new(),
        gpu: None,
    }
}

/// A probe that could not even start because the Vulkan loader is missing: the Linux loader's
/// "error while loading shared libraries: libvulkan.so.1", or Windows' STATUS_DLL_NOT_FOUND.
pub fn loader_missing(stderr: &str, exit_code: Option<i32>) -> bool {
    const STATUS_DLL_NOT_FOUND: i32 = 0xC000_0135_u32 as i32;
    let lower = stderr.to_lowercase();
    (lower.contains("libvulkan") && (lower.contains("cannot open shared object") || lower.contains("not found")))
        || lower.contains("vulkan-1.dll")
        || exit_code == Some(STATUS_DLL_NOT_FOUND)
}

pub fn no_loader(detail: &str) -> Diagnosis {
    let mut missing = reason("no-loader", "blocker", "driver", &[]);
    missing.detail = detail.into();
    Diagnosis { version: 1, reasons: vec![missing], ..Default::default() }
}

/// The reasons of a game older than the diagnosis, from what its probe said.
pub fn legacy(probe: &Probe) -> Vec<Reason> {
    let gpu = probe.gpu.as_ref();
    let name = gpu.map(|g| g.name.as_str()).unwrap_or_default();
    let api = gpu.map(|g| g.api.as_str()).unwrap_or_default();
    let vendor = match gpu.map(|g| crate::system_check::vendor(g.vendor_id)) {
        Some(crate::system_check::Vendor::Nvidia) => "nvidia",
        Some(crate::system_check::Vendor::Amd) => "amd",
        Some(crate::system_check::Vendor::Intel) => "intel",
        _ => "other",
    };
    let mut found = match probe.result {
        ProbeResult::Ok if probe.compatible => reason("compatible-path", "warning", "driver", &[("gpu", name), ("api", api), ("vendor", vendor)]),
        ProbeResult::Ok => return Vec::new(),
        ProbeResult::Unsuitable => reason("gpu-unsuitable", "blocker", "driver", &[("gpu", name), ("api", api), ("vendor", vendor)]),
        ProbeResult::NoVulkan | ProbeResult::NoDevice => reason("no-gpu", "blocker", "unknown", &[]),
    };
    found.detail = probe.detail.clone();
    vec![found]
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "kebab-case")]
pub enum Meanwhile {
    Card,
    Compatible,
    Processor,
    Nothing,
}

pub fn meanwhile(graphics: Graphics) -> Option<Meanwhile> {
    match graphics {
        Graphics::Hardware => Some(Meanwhile::Card),
        Graphics::Compatible => Some(Meanwhile::Compatible),
        Graphics::Software | Graphics::Fallback => Some(Meanwhile::Processor),
        Graphics::Blocked => Some(Meanwhile::Nothing),
        Graphics::Unknown => None,
    }
}

/// One reason as the page shows it: the codes of its sentences, steps and buttons.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Advice {
    pub code: String,
    pub severity: String,
    pub fix: String,
    pub params: BTreeMap<String, String>,
    /// Step codes, in order (`why.step.<code>` in the page's texts).
    pub steps: Vec<&'static str>,
    /// Pages the buttons open: nvidia, amd, intel, windows-update, windows-graphics.
    pub links: Vec<&'static str>,
    pub detail: String,
}

fn vendor_links(vendor: &str, os: &str) -> Vec<&'static str> {
    let page = match vendor {
        "nvidia" => Some("nvidia"),
        "amd" if os != "linux" => Some("amd"),
        "intel" if os != "linux" => Some("intel"),
        _ => None,
    };
    let mut links: Vec<&'static str> = page.into_iter().collect();
    if links.is_empty() && vendor == "other" && os != "linux" {
        links = vec!["nvidia", "amd", "intel"];
    }
    if os == "windows" {
        links.push("windows-update");
    }
    links
}

/// The driver step for a maker on this system: on Linux, AMD and Intel drivers come with the
/// system (Mesa), NVIDIA's from the distribution or NVIDIA.
fn driver_step(vendor: &str, os: &str) -> &'static str {
    match (os, vendor) {
        ("linux", "nvidia") => "linux-nvidia-driver",
        ("linux", _) => "linux-update-mesa",
        ("macos", _) => "macos-update",
        _ => "install-driver",
    }
}

pub fn advise(reason: &Reason, os: &str) -> Advice {
    let vendor = reason.params.get("vendor").map(String::as_str).unwrap_or("other");
    let driver = driver_step(vendor, os);
    let (steps, links): (Vec<&'static str>, Vec<&'static str>) = match reason.code.as_str() {
        "no-loader" if os == "linux" => (vec!["linux-vulkan-loader", driver], vec![]),
        "no-loader" => (vec![driver, "restart"], vendor_links("other", os)),
        "window-failed" if reason.params.get("display").is_some_and(|d| d == "none") => (vec!["linux-display"], vec![]),
        "window-failed" => (vec!["close-and-retry", "restart"], vec![]),
        "remote-session" => (vec!["play-locally", "remote-tool"], vec![]),
        "implicit-layers" => (vec!["close-overlays", "update-overlays", "restart"], vec![]),
        "basic-display-adapter" => (vec!["install-driver", "restart"], vendor_links(vendor, os)),
        "dedicated-gpu-unused" => {
            let dedicated = reason.params.get("dedicatedVendor").map(String::as_str).unwrap_or("other");
            match os {
                "windows" => {
                    let mut links = vec!["windows-graphics"];
                    links.extend(vendor_links(dedicated, os).into_iter().filter(|l| *l != "windows-update"));
                    (vec!["windows-graphics-settings", "vendor-panel", "install-driver-dedicated"], links)
                }
                "linux" => (vec![driver_step(dedicated, os), "linux-prime"], vendor_links(dedicated, os)),
                _ => (vec!["install-driver-dedicated"], vec![]),
            }
        }
        "driver-too-old" => (vec!["update-driver-min", "restart"], vendor_links(vendor, os)),
        "no-vendor-driver" => (vec![driver, "restart"], vendor_links(vendor, os)),
        "out-of-memory" => (vec!["close-programs", "lower-settings", "restart"], vec![]),
        "device-lost" => (vec!["update-driver", "clean-install", "no-overclock"], vendor_links(vendor, os)),
        "vulkan-too-old" | "missing-features" | "gpu-unsuitable" => match reason.fix.as_str() {
            "driver" => (vec!["update-driver", "restart"], vendor_links(vendor, os)),
            "hardware" => (vec!["hardware-limit"], vec![]),
            _ => (vec!["update-driver", "hardware-maybe"], vendor_links(vendor, os)),
        },
        "instance-failed" | "device-failed" => (vec!["clean-install", "close-overlays", "restart"], vendor_links(vendor, os)),
        "virtual-machine" => (vec!["vm-3d", "play-on-host"], vec![]),
        "compatible-path" if reason.fix == "driver" => (vec!["update-driver"], vendor_links(vendor, os)),
        "compatible-path" => (vec!["nothing-needed"], vec![]),
        "hardware-no-vulkan" => (vec!["hardware-limit"], vec![]),
        "low-vram" => (vec!["lower-settings"], vec![]),
        "integrated-gpu" => (vec!["integrated-tips"], vec![]),
        _ => (vec![driver, "restart"], vendor_links(vendor, os)),
    };
    Advice {
        code: reason.code.clone(),
        severity: reason.severity.clone(),
        fix: reason.fix.clone(),
        params: reason.params.clone(),
        steps,
        links,
        detail: reason.detail.clone(),
    }
}

/// Whether `shown` already tells the player what `other` would: the same fix for the same card,
/// or `other` is only what `shown` leads to.
fn covers(shown: &Reason, other: &Reason) -> bool {
    let same_gpu = shown.gpu.is_some() && shown.gpu == other.gpu;
    match other.code.as_str() {
        "vulkan-too-old" | "missing-features" | "compatible-path" => {
            same_gpu && matches!(shown.code.as_str(), "driver-too-old" | "vulkan-too-old" | "missing-features")
        }
        "no-gpu" => matches!(shown.code.as_str(), "no-loader" | "basic-display-adapter" | "no-vendor-driver" | "hardware-no-vulkan" | "dedicated-gpu-unused"),
        "integrated-gpu" => shown.code == "dedicated-gpu-unused",
        _ => false,
    }
}

/// The reasons to show, most actionable first (the game already sorted them), without the ones an
/// earlier reason already covers; the info-only ones stay out of a failure's dialog.
pub fn advise_all(reasons: &[Reason], os: &str, failure: bool) -> Vec<Advice> {
    let mut shown: Vec<&Reason> = Vec::new();
    for reason in reasons.iter().filter(|r| !failure || r.severity != "info") {
        if !shown.iter().any(|s| covers(s, reason)) {
            shown.push(reason);
        }
    }
    shown.into_iter().map(|r| advise(r, os)).collect()
}

/// The environment value of the first reason (REASON_ENV): the separators never appear in a value.
pub fn reason_env(advice: &[Advice]) -> Option<String> {
    let first = advice.first()?;
    let mut text = first.code.clone();
    for (key, value) in &first.params {
        let clean: String = value.chars().map(|c| if c == '|' || c == '\n' || c == '\r' { ' ' } else { c }).collect();
        text.push_str(&format!("|{key}={clean}"));
    }
    Some(text)
}

/// The text "Copy details" copies.
pub fn report(diagnosis: Option<&Diagnosis>, advice: &[Advice], log: &[String]) -> String {
    let mut lines = Vec::new();
    if let Some(d) = diagnosis {
        lines.push(format!("system: {}{}{}{}", d.os, if d.laptop { ", laptop" } else { "" }, if d.remote { ", remote session" } else { "" }, if d.virtual_machine { ", virtual machine" } else { "" }));
        for (i, gpu) in d.gpus.iter().enumerate() {
            let chosen = if d.chosen == Some(i) { " [used]" } else { "" };
            lines.push(format!(
                "gpu {i}{chosen}: {} ({}, {}:{}), driver {} ({}), Vulkan {}, {} MiB, floor {}{}",
                gpu.name,
                gpu.kind,
                gpu.vendor,
                gpu.device,
                gpu.driver_version,
                gpu.driver,
                gpu.api,
                gpu.vram_mib,
                gpu.floor,
                if gpu.missing.is_empty() { String::new() } else { format!(", missing {}", gpu.missing.join(", ")) },
            ));
        }
        for adapter in &d.adapters {
            lines.push(format!("adapter: {} ({}:{}){}", adapter.name, adapter.vendor, adapter.device, if adapter.driver.is_empty() { String::new() } else { format!(", {}", adapter.driver) }));
        }
        for layer in &d.layers {
            lines.push(format!("implicit layer: {} ({})", layer.name, layer.manifest));
        }
    }
    for a in advice {
        let params: Vec<String> = a.params.iter().map(|(k, v)| format!("{k}={v}")).collect();
        lines.push(format!("reason: {} ({}, fix {}) {}{}", a.code, a.severity, a.fix, params.join(" "), if a.detail.is_empty() { String::new() } else { format!(" — {}", a.detail) }));
    }
    if !log.is_empty() {
        lines.push("log:".into());
        lines.extend(log.iter().filter(|l| !l.contains(LOG_PREFIX)).cloned());
    }
    lines.join("\n")
}

/// The reasons a failed run shows: the ones the game wrote as it gave up, else, for a graphics
/// failure, the check's.
pub fn exit_advice(written: Option<&Diagnosis>, check: &[Advice], graphics_failure: bool, os: &str) -> Vec<Advice> {
    match written {
        Some(d) if !d.reasons.is_empty() => advise_all(&d.reasons, os, true),
        _ if graphics_failure => check.iter().filter(|a| a.severity != "info").cloned().collect(),
        _ => Vec::new(),
    }
}

/// A window that cannot open is not the graphics card's fault: stepping down would not help.
pub fn keeps_the_step(written: Option<&Diagnosis>) -> bool {
    written.and_then(|d| d.reasons.first()).is_some_and(|r| r.code == "window-failed" || r.code == "remote-session")
}

pub fn exit_meanwhile(next: Option<GraphicsStep>, graphics_failure: bool) -> Option<Meanwhile> {
    match next {
        Some(GraphicsStep::Card) | Some(GraphicsStep::Vulkan13) => Some(Meanwhile::Card),
        Some(GraphicsStep::Compatible) => Some(Meanwhile::Compatible),
        Some(GraphicsStep::Software) => Some(Meanwhile::Processor),
        None if graphics_failure => Some(Meanwhile::Nothing),
        None => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::system_check::Gpu;

    const GTX1060: &str = r#"{"adapters":[],"chosen":0,"gpus":[{"api":"1.2.175","device":"0x1c03","driver":"NVIDIA 457.30","driverVersion":"457.30","floor":"1.2","kind":"discrete","missing":["Vulkan 1.3"],"name":"NVIDIA GeForce GTX 1060 6GB","vendor":"0x10de","vramMiB":6144}],"laptop":false,"layers":[],"loader":{"instance":"VK_SUCCESS","version":"1.3"},"narrowed":false,"os":"windows","reasons":[{"code":"driver-too-old","detail":"","fix":"driver","gpu":0,"params":{"api":"1.2","driver":"457.30","gpu":"NVIDIA GeForce GTX 1060 6GB","minimum":"511.65","vendor":"nvidia"},"severity":"warning"},{"code":"compatible-path","detail":"","fix":"driver","gpu":0,"params":{"api":"1.2","features":"Vulkan 1.3","gpu":"NVIDIA GeForce GTX 1060 6GB","vendor":"nvidia"},"severity":"warning"}],"remote":false,"version":1,"virtual":false}"#;

    fn with(code: &str, fix: &str, params: &[(&str, &str)]) -> Reason {
        reason(code, "blocker", fix, params)
    }

    #[test]
    fn the_games_json_is_read() {
        let diagnosis = parse(GTX1060).unwrap();
        assert_eq!(diagnosis.chosen, Some(0));
        assert_eq!(diagnosis.gpus[0].driver_version, "457.30");
        assert_eq!(diagnosis.gpus[0].vram_mib, 6144);
        assert_eq!(diagnosis.reasons[0].code, "driver-too-old");
        assert_eq!(diagnosis.reasons[0].params["minimum"], "511.65");
        assert!(parse("not json").is_none());
    }

    #[test]
    fn the_last_diagnosis_of_a_log_is_found() {
        let log = format!("[info] GPU: x\n[info] {LOG_PREFIX}{{\"reasons\":[{{\"code\":\"old\"}}]}}\n[info] {LOG_PREFIX}{GTX1060}\n[error] bye\n");
        assert_eq!(from_log(&log).unwrap().reasons[0].code, "driver-too-old");
        assert!(from_log("[info] nothing here\n").is_none());
    }

    #[test]
    fn a_missing_loader_is_told_apart() {
        assert!(loader_missing("dwe-game: error while loading shared libraries: libvulkan.so.1: cannot open shared object file: No such file or directory", Some(127)));
        assert!(loader_missing("", Some(0xC000_0135_u32 as i32)));
        assert!(!loader_missing("bad argument", Some(1)));
        assert_eq!(no_loader("x").reasons[0].code, "no-loader");
    }

    #[test]
    fn an_old_games_probe_still_gives_a_reason() {
        let gpu = Gpu { name: "GTX 760".into(), vendor_id: 0x10de, kind: "discrete".into(), api: "1.1.0".into(), driver: "391.35".into() };
        let probe = Probe { result: ProbeResult::Unsuitable, detail: "no 1.3".into(), devices: 1, gpu: Some(gpu.clone()), compatible: false, diagnosis: None };
        let reasons = legacy(&probe);
        assert_eq!(reasons[0].code, "gpu-unsuitable");
        assert_eq!(reasons[0].params["vendor"], "nvidia");
        let ok = Probe { result: ProbeResult::Ok, compatible: true, ..probe.clone() };
        assert_eq!(legacy(&ok)[0].code, "compatible-path");
        assert!(legacy(&Probe { result: ProbeResult::Ok, ..probe.clone() }).is_empty());
        assert_eq!(legacy(&Probe { result: ProbeResult::NoDevice, gpu: None, ..probe })[0].code, "no-gpu");
    }

    #[test]
    fn each_reason_gets_its_steps_and_the_right_buttons() {
        let old = with("driver-too-old", "driver", &[("vendor", "nvidia"), ("driver", "391.35"), ("minimum", "511.65")]);
        let advice = advise(&old, "windows");
        assert_eq!(advice.steps, vec!["update-driver-min", "restart"]);
        assert_eq!(advice.links, vec!["nvidia", "windows-update"]);

        let mesa = with("driver-too-old", "driver", &[("vendor", "amd")]);
        assert_eq!(advise(&mesa, "linux").links, Vec::<&str>::new());
        assert_eq!(advise(&with("no-vendor-driver", "driver", &[("vendor", "amd")]), "linux").steps, vec!["linux-update-mesa", "restart"]);
        assert_eq!(advise(&with("no-vendor-driver", "driver", &[("vendor", "nvidia")]), "linux").steps, vec!["linux-nvidia-driver", "restart"]);

        let dual = with("dedicated-gpu-unused", "driver", &[("vendor", "intel"), ("dedicatedVendor", "nvidia")]);
        assert_eq!(advise(&dual, "windows").links, vec!["windows-graphics", "nvidia"]);
        assert_eq!(advise(&dual, "linux").steps, vec!["linux-nvidia-driver", "linux-prime"]);

        assert_eq!(advise(&with("vulkan-too-old", "hardware", &[("vendor", "intel")]), "windows").links, Vec::<&str>::new());
        assert_eq!(advise(&with("vulkan-too-old", "driver", &[("vendor", "intel")]), "windows").links, vec!["intel", "windows-update"]);
        assert_eq!(advise(&with("basic-display-adapter", "driver", &[("vendor", "amd")]), "windows").links, vec!["amd", "windows-update"]);
        assert_eq!(advise(&with("no-gpu", "unknown", &[]), "windows").links, vec!["nvidia", "amd", "intel", "windows-update"]);
        assert_eq!(advise(&with("window-failed", "settings", &[("display", "none")]), "linux").steps, vec!["linux-display"]);
        assert_eq!(advise(&with("implicit-layers", "settings", &[("layers", "VK_LAYER_RTSS")]), "windows").steps[0], "close-overlays");
    }

    #[test]
    fn a_failure_leaves_out_the_info_reasons() {
        let reasons = vec![with("driver-too-old", "driver", &[]), reason("integrated-gpu", "info", "hardware", &[])];
        assert_eq!(advise_all(&reasons, "windows", true).len(), 1);
        assert_eq!(advise_all(&reasons, "windows", false).len(), 2);
    }

    #[test]
    fn a_reason_the_first_one_already_covers_is_left_out() {
        let on = |mut r: Reason, gpu: usize| {
            r.gpu = Some(gpu);
            r
        };
        let codes = |reasons: &[Reason]| advise_all(reasons, "windows", false).into_iter().map(|a| a.code).collect::<Vec<_>>();
        let diagnosis = parse(GTX1060).unwrap();
        assert_eq!(codes(&diagnosis.reasons), vec!["driver-too-old"]);
        let two_cards = vec![on(with("driver-too-old", "driver", &[]), 0), on(with("vulkan-too-old", "driver", &[]), 1)];
        assert_eq!(codes(&two_cards), vec!["driver-too-old", "vulkan-too-old"]);
        let laptop = vec![on(with("dedicated-gpu-unused", "driver", &[]), 0), on(reason("integrated-gpu", "info", "hardware", &[]), 0)];
        assert_eq!(codes(&laptop), vec!["dedicated-gpu-unused"]);
        let no_driver = vec![with("basic-display-adapter", "driver", &[]), with("no-gpu", "unknown", &[])];
        assert_eq!(codes(&no_driver), vec!["basic-display-adapter"]);
        let overlays = vec![with("implicit-layers", "settings", &[]), with("instance-failed", "driver", &[])];
        assert_eq!(codes(&overlays), vec!["implicit-layers", "instance-failed"]);
    }

    #[test]
    fn what_the_game_does_meanwhile_follows_the_check() {
        assert_eq!(meanwhile(Graphics::Fallback), Some(Meanwhile::Processor));
        assert_eq!(meanwhile(Graphics::Compatible), Some(Meanwhile::Compatible));
        assert_eq!(meanwhile(Graphics::Blocked), Some(Meanwhile::Nothing));
        assert_eq!(meanwhile(Graphics::Unknown), None);
    }

    #[test]
    fn a_failed_run_explains_itself_with_what_the_game_wrote() {
        let written = parse(GTX1060).unwrap();
        let check = vec![advise(&with("no-gpu", "unknown", &[]), "windows")];
        assert_eq!(exit_advice(Some(&written), &check, true, "windows")[0].code, "driver-too-old");
        assert_eq!(exit_advice(None, &check, true, "windows")[0].code, "no-gpu");
        assert!(exit_advice(None, &check, false, "windows").is_empty());
        let window = Diagnosis { reasons: vec![with("window-failed", "settings", &[])], ..Default::default() };
        assert!(keeps_the_step(Some(&window)));
        assert!(!keeps_the_step(Some(&written)));
        assert_eq!(exit_meanwhile(Some(GraphicsStep::Software), true), Some(Meanwhile::Processor));
        assert_eq!(exit_meanwhile(None, true), Some(Meanwhile::Nothing));
        assert_eq!(exit_meanwhile(None, false), None);
    }

    #[test]
    fn the_game_gets_the_first_reason_in_one_line() {
        let advice = vec![advise(&with("driver-too-old", "driver", &[("gpu", "A|B\nC"), ("minimum", "511.65")]), "windows")];
        assert_eq!(reason_env(&advice).unwrap(), "driver-too-old|gpu=A B C|minimum=511.65");
        assert!(reason_env(&[]).is_none());
    }

    #[test]
    fn the_report_names_gpus_reasons_and_the_log() {
        let diagnosis = parse(GTX1060).unwrap();
        let advice = advise_all(&diagnosis.reasons, "windows", false);
        let text = report(Some(&diagnosis), &advice, &["[error] it broke".into(), format!("{LOG_PREFIX}{{}}")]);
        assert!(text.contains("gpu 0 [used]: NVIDIA GeForce GTX 1060 6GB (discrete, 0x10de:0x1c03), driver 457.30 (NVIDIA 457.30), Vulkan 1.2.175, 6144 MiB, floor 1.2, missing Vulkan 1.3"));
        assert!(text.contains("reason: driver-too-old (warning, fix driver)"));
        assert!(text.contains("minimum=511.65"));
        assert!(text.ends_with("log:\n[error] it broke"));
    }
}
