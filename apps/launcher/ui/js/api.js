// The launcher's privileged calls (src-tauri/src/commands.rs): disc, keyring, downloads and
// starting the game. Only the launcher's own code imports this module; an add-on bridge must not
// forward any of it.
const tauri = window.__TAURI__;

const call = (command, args) => tauri.core.invoke(command, args);

export const api = {
  status: () => call("status"),
  background: () => call("background"),
  checkUpdates: () => call("check_updates"),
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
};

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

export async function pickFolder(title) {
  const picked = await tauri.dialog.open({ title, multiple: false, directory: true });
  return typeof picked === "string" ? picked : null;
}

// Files dropped on the window: enter, over, leave and drop (with the paths).
export function onDragDrop(handler) {
  return tauri.webview.getCurrentWebview().onDragDropEvent((e) => handler(e.payload));
}
