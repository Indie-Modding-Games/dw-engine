// «Notas», the example add-on (docs/guides/launcher-addons.md). One page, three views:
// - background (always loaded, never shown): registers the sidebar icon, the tab, the settings
//   section and a news card;
// - tab: the notes, kept in the add-on's own storage;
// - settings: two options, also in storage.
(async function () {
  "use strict";
  const dwe = window.DweAddons;

  const TEXTS = {
    es: {
      title: "Notas",
      newLabel: "Nueva nota",
      save: "Guardar nota",
      empty: "Aún no hay notas. Escribe la primera arriba.",
      remove: "Borrar",
      saved: "Nota guardada.",
      count: (n) => (n === 1 ? "1 nota" : `${n} notas`),
      game: (v, running) => (v ? `Juego ${v}${running ? " · en juego" : ""}` : "El juego aún no está instalado"),
      notifyOnSave: "Avisar al guardar una nota",
      newestFirst: "Mostrar primero las más recientes",
      storage: (kb) => `Usa ${kb} KB de sus 256 KB.`,
      newsTitle: "Tus notas",
      newsBody: (n, last) => (n ? `Tienes ${n === 1 ? "1 nota" : `${n} notas`}. La última: «${last}»` : "Abre la pestaña Notas para apuntar algo."),
      placeholder: "Por ejemplo: entrenar velocidad antes del torneo del sábado…",
    },
    en: {
      title: "Notes",
      newLabel: "New note",
      save: "Save note",
      empty: "No notes yet. Write the first one above.",
      remove: "Delete",
      saved: "Note saved.",
      count: (n) => (n === 1 ? "1 note" : `${n} notes`),
      game: (v, running) => (v ? `Game ${v}${running ? " · playing" : ""}` : "The game is not installed yet"),
      notifyOnSave: "Tell me when a note is saved",
      newestFirst: "Show the newest first",
      storage: (kb) => `Uses ${kb} KB of its 256 KB.`,
      newsTitle: "Your notes",
      newsBody: (n, last) => (n ? `You have ${n === 1 ? "1 note" : `${n} notes`}. The last one: “${last}”` : "Open the Notes tab to write something down."),
      placeholder: "For example: train speed before Saturday's tournament…",
    },
  };
  const LABEL = { es: "Notas", en: "Notes" };

  await dwe.connect();
  const context = await dwe.context();
  let language = context.language === "en" ? "en" : "es";
  const text = (key, ...args) => {
    const value = TEXTS[language][key];
    return typeof value === "function" ? value(...args) : value;
  };

  async function loadNotes() {
    return (await dwe.storage.get("notes")) || [];
  }
  async function loadOptions() {
    return { notifyOnSave: true, newestFirst: true, ...((await dwe.storage.get("options")) || {}) };
  }

  async function publishNews(notes) {
    const last = notes.length ? notes[notes.length - 1].text.split("\n")[0].slice(0, 60) : "";
    await dwe.ui.news([{ title: text("newsTitle"), body: text("newsBody", notes.length, last) }]);
  }

  function fillTexts() {
    document.documentElement.lang = language;
    for (const node of document.querySelectorAll("[data-text]")) node.textContent = text(node.dataset.text);
  }

  // ---- background ------------------------------------------------------------------------
  if (dwe.view === "background") {
    await dwe.ui.sidebar({ label: LABEL, icon: "icon.svg" });
    await dwe.ui.tab({ label: LABEL });
    await dwe.ui.settings({ label: LABEL });
    await publishNews(await loadNotes());
    dwe.on("language", async (event) => {
      language = event.language === "en" ? "en" : "es";
      await publishNews(await loadNotes());
    });
    return;
  }

  fillTexts();

  // ---- tab -------------------------------------------------------------------------------
  if (dwe.view === "tab") {
    document.getElementById("tab").hidden = false;
    const list = document.getElementById("notes");
    const input = document.getElementById("noteText");
    input.placeholder = text("placeholder");
    let status = { running: false, gameVersion: context.game_version };

    const showGame = () => {
      document.getElementById("gameLine").textContent = text("game", status.gameVersion, status.running);
    };
    showGame();
    dwe.on("game.status", (next) => {
      status = next;
      showGame();
    });

    async function draw() {
      const notes = await loadNotes();
      const options = await loadOptions();
      const shown = options.newestFirst ? [...notes].reverse() : notes;
      list.replaceChildren(
        ...shown.map((note) => {
          const item = document.createElement("li");
          const body = document.createElement("p");
          body.textContent = note.text;
          const meta = document.createElement("div");
          meta.className = "meta";
          const time = document.createElement("time");
          time.textContent = new Date(note.at).toLocaleString(language);
          const remove = document.createElement("button");
          remove.type = "button";
          remove.className = "button subtle";
          remove.textContent = text("remove");
          remove.addEventListener("click", async () => {
            const all = (await loadNotes()).filter((n) => n.at !== note.at);
            await dwe.storage.set("notes", all);
            await publishNews(all);
            draw();
          });
          meta.append(time, remove);
          item.append(body, meta);
          return item;
        }),
      );
      document.getElementById("empty").hidden = notes.length > 0;
      document.getElementById("count").textContent = text("count", notes.length);
    }

    document.getElementById("newNote").addEventListener("submit", async (event) => {
      event.preventDefault();
      const value = input.value.trim();
      if (!value) return;
      const notes = await loadNotes();
      notes.push({ text: value, at: Date.now() });
      try {
        await dwe.storage.set("notes", notes);
      } catch (error) {
        input.setCustomValidity(error.message);
        input.reportValidity();
        return;
      }
      input.value = "";
      await publishNews(notes);
      if ((await loadOptions()).notifyOnSave) dwe.notify(text("saved"), "success").catch(() => {});
      draw();
    });
    // Ctrl+Enter saves too.
    input.addEventListener("keydown", (event) => {
      if (event.key === "Enter" && event.ctrlKey) document.getElementById("newNote").requestSubmit();
    });
    draw();
    return;
  }

  // ---- settings --------------------------------------------------------------------------
  if (dwe.view === "settings") {
    document.getElementById("settings").hidden = false;
    document.body.classList.add("is-settings");
    const options = await loadOptions();
    for (const key of ["notifyOnSave", "newestFirst"]) {
      const box = document.getElementById(key);
      box.checked = options[key];
      box.addEventListener("change", async () => {
        const now = await loadOptions();
        now[key] = box.checked;
        await dwe.storage.set("options", now);
      });
    }
    const size = JSON.stringify(await loadNotes()).length + JSON.stringify(options).length;
    document.getElementById("storageLine").textContent = text("storage", (size / 1024).toFixed(1));
    dwe.ui.autoResize();
  }
})();
