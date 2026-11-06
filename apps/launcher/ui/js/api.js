// The launcher's privileged calls (src-tauri/src/commands.rs): disc, keyring, downloads and
// starting the game. Only the launcher's own code imports this module; the add-on bridge
// (addons.js) forwards nothing but addonCall, for the calls the backend checks per add-on.
const tauri = window.__TAURI__;

const call = (command, args) => tauri.core.invoke(command, args);

export const api = {
  status: () => call("status"),
  background: () => call("background"),
  // record: false for the quiet look right after an update (keeps its "updated" result).
  checkUpdates: (record = true) => call("check_updates", { recordResult: record }),
  installDisc: (path) => call("install_disc", { path }),
  reextract: (path = null) => call("reextract", { path }),
  updateGame: () => call("update_game"),
  rollbackGame: () => call("rollback_game"),
  cancel: () => call("cancel"),
  play: () => call("play"),
  setLanguage: (language) => call("set_language", { language }),
  setCheckUpdates: (enabled) => call("set_check_updates", { enabled }),
  setDataFolder: (path) => call("set_data_folder", { path }),
  saveToken: (token) => call("save_token", { token }),
  clearToken: () => call("clear_token"),
  openLogs: () => call("open_logs"),
  installLauncherUpdate: () => call("install_launcher_update"),
  restartLauncher: () => call("restart_launcher"),
  setAutoUpdate: (what, enabled) => call("set_auto_update", { what, enabled }),
  // "stable" or "testing"; the release found by the last check is dropped.
  setUpdateChannel: (channel) => call("set_update_channel", { channel }),
  // Free space and the GPU check (src-tauri/src/system_check.rs); again: check once more.
  checkSystem: (again = false) => call("check_system", { again }),
  // nvidia, amd, intel or windows-update: the backend knows the addresses.
  openDriverPage: (page) => call("open_driver_page", { page }),
  // The add-on manager (addon-manager.js) and the add-ons' backend calls (addons.js).
  addonsList: () => call("addons_list"),
  addonInspect: (path) => call("addon_inspect", { path }),
  addonInstall: (ticket, accept) => call("addon_install", { ticket, accept }),
  addonSetEnabled: (id, enabled) => call("addon_set_enabled", { id, enabled }),
  addonUninstall: (id) => call("addon_uninstall", { id }),
  addonsSafeMode: (on) => call("addons_safe_mode", { on }),
  addonsDismissCrash: () => call("addons_dismiss_crash"),
  addonCall: (addon, method, params) => call("addon_call", { addon, method, params }),
  // The player's images (custom-images.js, src-tauri/src/image_commands.rs).
  imagesOverview: () => call("images_overview"),
  imagesPrepare: () => call("images_prepare"),
  imagesInspect: (path) => call("images_inspect", { path }),
  imagesApply: (path) => call("images_apply", { path }),
  imagesAssign: (target, path) => call("images_assign", { target, path }),
  imagesReset: (target) => call("images_reset", { target }),
  imagesThumbnail: (target, custom) => call("images_thumbnail", { target, custom }),
  imagesSetEnabled: (enabled) => call("images_set_enabled", { enabled }),
  imagesRemoveAll: () => call("images_remove_all"),
  imagesExport: (folder) => call("images_export", { folder }),
};

// Opens a web page in the player's browser (the opener plugin: http and https only).
export function openUrl(url) {
  return tauri.opener.openUrl(url);
}

export function listen(event, handler) {
  return tauri.event.listen(event, (e) => handler(e.payload));
}

// The disc picker: the readable files first, every file as the second filter, so a format the
// launcher does not read can still be picked and explained.
export async function pickDisc(title, filterName, extensions, allName) {
  const filters = [{ name: filterName, extensions }, { name: allName, extensions: ["*"] }];
  const picked = await tauri.dialog.open({ title, multiple: false, directory: false, filters });
  return typeof picked === "string" ? picked : picked?.path ?? null;
}

export async function pickAddonFile(title, filterName) {
  const filters = [{ name: filterName, extensions: ["zip", "json"] }];
  const picked = await tauri.dialog.open({ title, multiple: false, directory: false, filters });
  return typeof picked === "string" ? picked : picked?.path ?? null;
}

// A file of one kind: a .zip of images, a PNG.
export async function pickFile(title, filterName, extensions) {
  const filters = [{ name: filterName, extensions }];
  const picked = await tauri.dialog.open({ title, multiple: false, directory: false, filters });
  return typeof picked === "string" ? picked : picked?.path ?? null;
}

export async function pickFolder(title) {
  const picked = await tauri.dialog.open({ title, multiple: false, directory: true });
  return typeof picked === "string" ? picked : null;
}

// Files dropped on the window: enter, over, leave and drop (with the paths).
export function onDragDrop(handler) {
  return tauri.webview.getCurrentWebview().onDragDropEvent((e) => handler(e.payload));
}
