// Test add-on (docs/guides/launcher-addons.md#tests): its background page reads the releases of
// the local fake release server through net.fetch (the only host it declares) and shows them as a
// news card; its tab tries everything an add-on must NOT be able to do and lists the results.
// Every line must say PASS. Its texts follow the launcher's language (es, en), like the example.
(async function () {
  "use strict";
  const dwe = window.DweAddons;
  const SERVER = "http://127.0.0.1:8779";
  await dwe.connect();
  const language = (await dwe.context()).language === "es" ? "es" : "en";
  const L = (es, en) => (language === "es" ? es : en);
  document.documentElement.lang = language;

  if (dwe.view === "background") {
    await dwe.ui.tab({ label: { es: "Aislamiento", en: "Isolation" } });
    try {
      const response = await dwe.net.fetch(`${SERVER}/repos/Indie-Modding-Games/dw-engine/releases?per_page=20`, { headers: { Accept: "application/json" } });
      const releases = JSON.parse(response.body);
      const newest = releases[0]?.tag_name;
      await dwe.ui.news([{
        title: L("Servidor de pruebas", "Test server"),
        body: L(`net.fetch: ${releases.length} versiones publicadas (HTTP ${response.status}). La más nueva: ${newest ?? "ninguna"}.`, `net.fetch: ${releases.length} releases published (HTTP ${response.status}). The newest: ${newest ?? "none"}.`),
      }]);
    } catch (error) {
      await dwe.ui.news([{ title: L("Servidor de pruebas", "Test server"), body: L(`net.fetch falló: ${error.code}`, `net.fetch failed: ${error.code}`) }]);
    }
    return;
  }

  document.getElementById("summary").textContent = L("Comprobando…", "Checking…");
  const results = [];
  async function check(name, probe) {
    let pass;
    let detail = "";
    try {
      [pass, detail] = await probe();
    } catch (error) {
      pass = false;
      detail = L(`error inesperado: ${error}`, `unexpected error: ${error}`);
    }
    results.push({ name, pass, detail });
  }
  const refused = async (promise, code) => {
    try {
      await promise;
      return [false, L("no se rechazó", "not refused")];
    } catch (error) {
      return [error.code === code, error.code];
    }
  };
  const throws = (fn) => {
    try {
      const value = fn();
      return [value === undefined || value === null, L(`devolvió ${value === null ? "null" : typeof value}`, `returned ${value === null ? "null" : typeof value}`)];
    } catch (error) {
      return [true, error.name];
    }
  };

  await check(L("No ve el lanzador (window.parent.__TAURI__)", "Cannot see the launcher (window.parent.__TAURI__)"), () => throws(() => window.parent.__TAURI__));
  await check(L("No ve el documento del lanzador", "Cannot see the launcher's document"), () => throws(() => window.parent.document.title));
  await check(L("Sin IPC propio (__TAURI_INTERNALS__)", "No IPC of its own (__TAURI_INTERNALS__)"), () => [typeof window.__TAURI_INTERNALS__ === "undefined", typeof window.__TAURI_INTERNALS__]);
  await check(L("Sin __TAURI__", "No __TAURI__"), () => [typeof window.__TAURI__ === "undefined", typeof window.__TAURI__]);
  await check(L("No llama al IPC por fetch (ipc://)", "Cannot call the IPC through fetch (ipc://)"), async () => {
    try {
      await fetch("ipc://localhost/status", { method: "POST", body: "{}" });
      return [false, L("respondió", "answered")];
    } catch (error) {
      return [true, error.name];
    }
  });
  await check(L("No llama al IPC por fetch (http://ipc.localhost)", "Cannot call the IPC through fetch (http://ipc.localhost)"), async () => {
    try {
      await fetch("http://ipc.localhost/status", { method: "POST", body: "{}" });
      return [false, L("respondió", "answered")];
    } catch (error) {
      return [true, error.name];
    }
  });
  await check(L("Sin fetch directo a internet", "No direct fetch to the internet"), async () => {
    try {
      await fetch(`${SERVER}/repos/Indie-Modding-Games/dw-engine/releases`);
      return [false, L("respondió", "answered")];
    } catch (error) {
      return [true, error.name];
    }
  });
  for (const command of ["save_token", "status", "install_disc", "play", "update_game", "addon_install", "plugin:opener|open_path"]) {
    await check(L(`El puente rechaza «${command}»`, `The bridge refuses "${command}"`), () => refused(dwe.call(command, { token: "x", path: "/" }), "addon-denied"));
  }
  await check(L("net.fetch rechaza un servidor no declarado", "net.fetch refuses an undeclared server"), () => refused(dwe.net.fetch("https://api.github.com/repos/Indie-Modding-Games/dw-engine/releases"), "addon-host"));
  await check(L("net.fetch no deja poner Authorization", "net.fetch does not allow Authorization"), () => refused(dwe.net.fetch(`${SERVER}/`, { headers: { Authorization: "Bearer x" } }), "addon-denied"));
  await check(L("Sin permiso de almacenamiento", "No storage permission"), () => refused(dwe.storage.get("x"), "addon-denied"));
  await check(L("Sin permiso de avisos", "No notification permission"), () => refused(dwe.notify(L("hola", "hello")), "addon-denied"));
  await check(L("No usa ranuras que no declaró (barra lateral)", "Cannot use slots it did not declare (sidebar)"), () => refused(dwe.ui.sidebar({ label: "x", icon: "x.svg" }), "addon-denied"));
  await check(L("El contexto no lleva rutas ni secretos", "The context has no paths or secrets"), async () => {
    const text = JSON.stringify(await dwe.context());
    const leaked = ["/", "\\\\", "token", "disc"].filter((s) => text.includes(s));
    return [leaked.length === 0, leaked.join(", ") || L("limpio", "clean")];
  });
  // Some engines throw on document.cookie in an opaque origin, others return "" and drop writes.
  await check(L("Sin cookies", "No cookies"), () => {
    try {
      document.cookie = "probe=1";
      const value = document.cookie;
      return [value === "", value === "" ? L("vacías, no se guardan", "empty, not stored") : L("se guardó una cookie", "a cookie was stored")];
    } catch (error) {
      return [true, error.name];
    }
  });
  await check(L("Sin localStorage", "No localStorage"), () => throws(() => window.localStorage));
  await check(L("No abre ventanas", "Cannot open windows"), () => throws(() => window.open("https://example.org")));
  await check(L("No carga archivos de otro complemento", "Cannot load files of another add-on"), () =>
    new Promise((resolve) => {
      const image = new Image();
      image.onload = () => resolve([false, L("cargó", "loaded")]);
      image.onerror = () => resolve([true, L("bloqueado", "blocked")]);
      image.src = location.href.replace(/\/org\.indie-modding-games\.test-fetch\/.*$/, "/org.indie-modding-games.example-notes/icon.svg");
      setTimeout(() => resolve([true, L("bloqueado (sin respuesta)", "blocked (no answer)")]), 2000);
    }),
  );
  // If the launcher's window could be navigated, this page would be gone before the results show.
  await check(L("No navega la ventana del lanzador", "Cannot navigate the launcher's window"), async () => {
    try {
      window.top.location.href = "https://example.org";
    } catch (error) {
      return [true, error.name];
    }
    await new Promise((resolve) => setTimeout(resolve, 800));
    return [true, L("bloqueado", "blocked")];
  });
  await check(L("net.fetch al servidor declarado funciona", "net.fetch to the declared server works"), async () => {
    const response = await dwe.net.fetch(`${SERVER}/repos/Indie-Modding-Games/dw-engine/releases`);
    return [response.status === 200, `HTTP ${response.status}`];
  });

  document.title = L("Prueba", "Test");
  document.querySelector("h1").textContent = L("Aislamiento del complemento", "Add-on isolation");
  const list = document.getElementById("results");
  for (const r of results) {
    const item = document.createElement("li");
    item.className = r.pass ? "pass" : "fail";
    const mark = document.createElement("b");
    mark.className = r.pass ? "pass" : "fail";
    mark.textContent = r.pass ? L("PASA", "PASS") : L("FALLA", "FAIL");
    item.append(mark, ` ${r.name} `);
    const detail = document.createElement("span");
    detail.className = "muted";
    detail.textContent = `(${r.detail})`;
    item.append(detail);
    list.append(item);
  }
  const failed = results.filter((r) => !r.pass).length;
  document.getElementById("summary").textContent = failed ? L(`${failed} de ${results.length} FALLAN`, `${failed} of ${results.length} FAIL`) : L(`Las ${results.length} pruebas pasan.`, `All ${results.length} tests pass.`);
  dwe.ui.autoResize();
})();
