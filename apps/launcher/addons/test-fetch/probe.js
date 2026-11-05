// Test add-on (docs/guides/launcher-addons.md#pruebas): its background page reads the releases of
// the local fake release server through net.fetch (the only host it declares) and shows them as a
// news card; its tab tries everything an add-on must NOT be able to do and lists the results.
// Every line must say PASA.
(async function () {
  "use strict";
  const dwe = window.DweAddons;
  const SERVER = "http://127.0.0.1:8779";
  await dwe.connect();

  if (dwe.view === "background") {
    await dwe.ui.tab({ label: { es: "Aislamiento", en: "Isolation" } });
    try {
      const response = await dwe.net.fetch(`${SERVER}/repos/Indie-Modding-Games/dw-engine/releases?per_page=20`, { headers: { Accept: "application/json" } });
      const releases = JSON.parse(response.body);
      await dwe.ui.news([{ title: "Servidor de pruebas", body: `net.fetch: ${releases.length} versiones publicadas (HTTP ${response.status}). La más nueva: ${releases[0]?.tag_name ?? "ninguna"}.` }]);
    } catch (error) {
      await dwe.ui.news([{ title: "Servidor de pruebas", body: `net.fetch falló: ${error.code}` }]);
    }
    return;
  }

  const results = [];
  async function check(name, probe) {
    let pass;
    let detail = "";
    try {
      [pass, detail] = await probe();
    } catch (error) {
      pass = false;
      detail = `error inesperado: ${error}`;
    }
    results.push({ name, pass, detail });
  }
  const refused = async (promise, code) => {
    try {
      await promise;
      return [false, "no se rechazó"];
    } catch (error) {
      return [error.code === code, error.code];
    }
  };
  const throws = (fn) => {
    try {
      const value = fn();
      return [value === undefined || value === null, `devolvió ${value === null ? "null" : typeof value}`];
    } catch (error) {
      return [true, error.name];
    }
  };

  await check("No ve el lanzador (window.parent.__TAURI__)", () => throws(() => window.parent.__TAURI__));
  await check("No ve el documento del lanzador", () => throws(() => window.parent.document.title));
  await check("Sin IPC propio (__TAURI_INTERNALS__)", () => [typeof window.__TAURI_INTERNALS__ === "undefined", typeof window.__TAURI_INTERNALS__]);
  await check("Sin __TAURI__", () => [typeof window.__TAURI__ === "undefined", typeof window.__TAURI__]);
  await check("No llama al IPC por fetch (ipc://)", async () => {
    try {
      await fetch("ipc://localhost/status", { method: "POST", body: "{}" });
      return [false, "respondió"];
    } catch (error) {
      return [true, error.name];
    }
  });
  await check("No llama al IPC por fetch (http://ipc.localhost)", async () => {
    try {
      await fetch("http://ipc.localhost/status", { method: "POST", body: "{}" });
      return [false, "respondió"];
    } catch (error) {
      return [true, error.name];
    }
  });
  await check("Sin fetch directo a internet", async () => {
    try {
      await fetch(`${SERVER}/repos/Indie-Modding-Games/dw-engine/releases`);
      return [false, "respondió"];
    } catch (error) {
      return [true, error.name];
    }
  });
  for (const command of ["save_token", "status", "install_disc", "play", "update_game", "addon_install", "plugin:opener|open_path"]) {
    await check(`El puente rechaza «${command}»`, () => refused(dwe.call(command, { token: "x", path: "/" }), "addon-denied"));
  }
  await check("net.fetch rechaza un servidor no declarado", () => refused(dwe.net.fetch("https://api.github.com/repos/Indie-Modding-Games/dw-engine/releases"), "addon-host"));
  await check("net.fetch no deja poner Authorization", () => refused(dwe.net.fetch(`${SERVER}/`, { headers: { Authorization: "Bearer x" } }), "addon-denied"));
  await check("Sin permiso de almacenamiento", () => refused(dwe.storage.get("x"), "addon-denied"));
  await check("Sin permiso de avisos", () => refused(dwe.notify("hola"), "addon-denied"));
  await check("No usa ranuras que no declaró (barra lateral)", () => refused(dwe.ui.sidebar({ label: "x", icon: "x.svg" }), "addon-denied"));
  await check("El contexto no lleva rutas ni secretos", async () => {
    const text = JSON.stringify(await dwe.context());
    const leaked = ["/", "\\\\", "token", "disc"].filter((s) => text.includes(s));
    return [leaked.length === 0, leaked.join(", ") || "limpio"];
  });
  // Some engines throw on document.cookie in an opaque origin, others return "" and drop writes.
  await check("Sin cookies", () => {
    try {
      document.cookie = "probe=1";
      const value = document.cookie;
      return [value === "", value === "" ? "vacías, no se guardan" : "se guardó una cookie"];
    } catch (error) {
      return [true, error.name];
    }
  });
  await check("Sin localStorage", () => throws(() => window.localStorage));
  await check("No abre ventanas", () => throws(() => window.open("https://example.org")));
  await check("No carga archivos de otro complemento", () =>
    new Promise((resolve) => {
      const image = new Image();
      image.onload = () => resolve([false, "cargó"]);
      image.onerror = () => resolve([true, "bloqueado"]);
      image.src = location.href.replace(/\/org\.indie-modding-games\.test-fetch\/.*$/, "/org.indie-modding-games.example-notes/icon.svg");
      setTimeout(() => resolve([true, "bloqueado (sin respuesta)"]), 2000);
    }),
  );
  // If the launcher's window could be navigated, this page would be gone before the results show.
  await check("No navega la ventana del lanzador", async () => {
    try {
      window.top.location.href = "https://example.org";
    } catch (error) {
      return [true, error.name];
    }
    await new Promise((resolve) => setTimeout(resolve, 800));
    return [true, "bloqueado"];
  });
  await check("net.fetch al servidor declarado funciona", async () => {
    const response = await dwe.net.fetch(`${SERVER}/repos/Indie-Modding-Games/dw-engine/releases`);
    return [response.status === 200, `HTTP ${response.status}`];
  });

  const list = document.getElementById("results");
  for (const r of results) {
    const item = document.createElement("li");
    item.className = r.pass ? "pass" : "fail";
    const mark = document.createElement("b");
    mark.className = r.pass ? "pass" : "fail";
    mark.textContent = r.pass ? "PASA" : "FALLA";
    item.append(mark, ` ${r.name} `);
    const detail = document.createElement("span");
    detail.className = "muted";
    detail.textContent = `(${r.detail})`;
    item.append(detail);
    list.append(item);
  }
  const failed = results.filter((r) => !r.pass).length;
  document.getElementById("summary").textContent = failed ? `${failed} de ${results.length} FALLAN` : `Las ${results.length} pruebas pasan.`;
  dwe.ui.autoResize();
})();
