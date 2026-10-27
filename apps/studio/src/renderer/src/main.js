import * as monaco from 'monaco-editor'
import editorWorker from '../../../node_modules/monaco-editor/esm/vs/editor/editor.worker.js?worker'
import jsonWorker from '../../../node_modules/monaco-editor/esm/vs/language/json/json.worker.js?worker'
import cssWorker from '../../../node_modules/monaco-editor/esm/vs/language/css/css.worker.js?worker'
import htmlWorker from '../../../node_modules/monaco-editor/esm/vs/language/html/html.worker.js?worker'
import tsWorker from '../../../node_modules/monaco-editor/esm/vs/language/typescript/ts.worker.js?worker'
import './style.css'

self.MonacoEnvironment = {
  getWorker(_workerId, label) {
    if (label === 'json') return new jsonWorker()
    if (label === 'css' || label === 'scss' || label === 'less') return new cssWorker()
    if (label === 'html' || label === 'handlebars' || label === 'razor') return new htmlWorker()
    if (label === 'typescript' || label === 'javascript') return new tsWorker()
    return new editorWorker()
  }
}

const api = window.dweStudio
const state = { tabs: [], activePath: null, activeGame: null, dirty: new Set(), running: new Set(), managedRunning: new Set(), output: [], changed: [], files: [], workspaceMode: 'compare', discPath: null, captureSources: [], captureStreams: new Map(), observing: false, observingBusy: false, observerTimer: null, observerErrorTimer: null, observerLogCursor: 0 }
const app = document.querySelector('#app')
const icon = (name) => ({
  files: '<path d="M4 3.5h11l5 5V20H4z"/><path d="M15 3.5V9h5M8 13h8M8 16.5h6"/>',
  search: '<circle cx="10.5" cy="10.5" r="6.5"/><path d="m16 16 4.5 4.5"/>',
  git: '<circle cx="7" cy="5" r="2"/><circle cx="17" cy="19" r="2"/><circle cx="7" cy="19" r="2"/><path d="M7 7v10m0-6c0 4 10 0 10 6V5"/>',
  bug: '<path d="M8 8h8a3 3 0 0 1 3 3v5a5 5 0 0 1-5 5h-2a5 5 0 0 1-5-5v-5a3 3 0 0 1 3-3zM9 8l-2-3m8 3 2-3M4 12h4m8 0h4M4 17h4m8 0h4M12 3v3"/>',
  blocks: '<rect x="3" y="3" width="8" height="8" rx="1"/><rect x="13" y="3" width="8" height="5" rx="1"/><rect x="13" y="10" width="8" height="11" rx="1"/><rect x="3" y="13" width="8" height="8" rx="1"/>',
  terminal: '<path d="m4 6 6 5-6 5m8 1h8"/>',
  play: '<path d="m7 4 13 8-13 8z"/>',
  stop: '<rect x="6" y="6" width="12" height="12" rx="1"/>',
  save: '<path d="M5 3h12l3 3v15H4V3z"/><path d="M8 3v6h8V3m-8 18v-8h8v8"/>',
  refresh: '<path d="M20 7v5h-5M4 17v-5h5"/><path d="M5.5 9A7 7 0 0 1 18 6l2 6M4 12l2 6a7 7 0 0 0 12.5-3"/>',
  chat: '<path d="M4 5h16v12H9l-5 4z"/><path d="M8 9h8m-8 4h5"/>',
  close: '<path d="m6 6 12 12M18 6 6 18"/>',
  chevron: '<path d="m9 18 6-6-6-6"/>',
  folder: '<path d="M3 6h7l2 2h9v11H3z"/>',
  file: '<path d="M5 3h9l5 5v13H5zM14 3v6h5"/>',
  window: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M3 9h18"/>',
  settings: '<circle cx="12" cy="12" r="3"/><path d="m19.4 15 .1.1a2 2 0 1 1-2.8 2.8l-.1-.1a2 2 0 0 0-3.4 1.4v.3a2 2 0 1 1-4 0v-.2A2 2 0 0 0 5.8 17l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a2 2 0 0 0-1.4-3.4h-.3a2 2 0 1 1 0-4h.2A2 2 0 0 0 3 4.2l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a2 2 0 0 0 3.4-1.4v-.3a2 2 0 1 1 4 0v.2A2 2 0 0 0 17 3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a2 2 0 0 0 1.4 3.4h.3a2 2 0 1 1 0 4h-.2a2 2 0 0 0-2 1.8z"/>',
  warning: '<path d="m12 3 9 17H3z"/><path d="M12 9v5m0 3h.01"/>'
}[name] || '<circle cx="12" cy="12" r="8"/>')
const svg = (name, extra = '') => `<svg ${extra} viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${icon(name)}</svg>`

app.innerHTML = `
  <div class="titlebar"><div class="brand-mark">D</div><div class="title-project">DWE Studio <span class="title-sep">/</span> <span id="project-name">engine</span></div><button class="command-pill" id="quick-open" aria-label="Buscar archivos">${svg('search')} <span>Buscar archivos, símbolos y comandos</span><kbd>⌘ P</kbd></button><div class="title-actions"><button class="icon-button" title="Abrir otro proyecto" id="choose-root">${svg('folder')}</button><button class="icon-button" title="Ajustes">${svg('settings')}</button></div><div class="window-controls" id="window-controls"><button title="Minimizar" id="window-min">−</button><button title="Maximizar" id="window-max">□</button><button title="Cerrar" id="window-close">×</button></div></div>
  <div class="workspace">
    <nav class="activitybar" aria-label="Vistas"><button class="activity active" title="Explorador">${svg('files')}</button><button class="activity" title="Buscar">${svg('search')}</button><button class="activity" title="Control de código fuente">${svg('git')}<i class="activity-badge">3</i></button><button class="activity" title="Depuración">${svg('bug')}</button><button class="activity" title="Extensiones">${svg('blocks')}</button><div class="activity-spacer"></div><button class="activity" title="Cuenta">${svg('chat')}</button><button class="activity" title="Configuración">${svg('settings')}</button></nav>
    <aside class="sidebar"><div class="side-heading"><span>EXPLORADOR</span><button class="tiny-icon" id="refresh-tree" title="Actualizar">${svg('refresh')}</button></div><div class="side-workspace">${svg('chevron')} <span id="tree-root">ENGINE</span> <span class="tree-actions">···</span></div><div id="file-tree" class="file-tree"></div><div class="reference-heading" id="decomp-heading">${svg('chevron')} <span>DW_DECOMP</span><small>SOLO LECTURA</small></div><div id="decomp-tree" class="decomp-tree"></div><div class="sidebar-lower"><div class="side-heading"><span>AGENTES</span><button class="tiny-icon" title="Nueva sesión">+</button></div><div class="agent-row"><span class="agent-orb codex-orb">C</span><div><strong>Codex</strong><small>Este workspace</small></div><span class="connection-dot on"></span></div><div class="agent-row"><span class="agent-orb claude-orb">✳</span><div><strong>Claude Code</strong><small id="claude-status">Comprobando CLI…</small></div><span id="claude-dot" class="connection-dot"></span></div><div class="agent-note">Los cambios de ambos agentes aparecen aquí al guardar archivos.</div></div></aside>
    <main class="main-area"><div class="editor-tabs" id="tabs"><div class="empty-tabs">DWE Studio</div><div class="tab-tools"><button class="icon-button" title="Dividir editor">▣</button><button class="icon-button" title="Más acciones">···</button></div></div><div class="breadcrumb" id="breadcrumb"><span>Selecciona un archivo</span></div><div class="editor-host" id="editor"><div class="welcome"><div class="welcome-symbol">D</div><h1>Tu juego, en contexto</h1><p>Edita el motor y los scripts desde el mismo espacio donde compilas y ejecutas.</p><div class="welcome-actions"><button id="open-main" class="welcome-link">Abrir <code>apps/game/src/main.cpp</code></button><button id="open-script" class="welcome-link">Abrir <code>apps/game/scripts/main.lua</code></button></div><div class="welcome-shortcuts"><span>⌘ S <small>guardar</small></span><span>⌘ B <small>compilar</small></span><span>F5 <small>ejecutar juego</small></span></div></div></div><div class="right-rail"><div class="rail-heading">SESIÓN DE JUEGO <button class="tiny-icon">···</button></div><div class="run-card"><div class="run-card-top"><div class="run-icon">${svg('window')}</div><div><strong>DWE Game</strong><small id="game-state">Listo para ejecutar</small></div><span id="game-dot" class="connection-dot"></span></div><div class="run-preview"><div class="preview-grid"></div><div class="preview-center"><span class="preview-orbit"></span><div class="preview-mark">DWE</div><span class="preview-label">VENTANA NATIVA DEL JUEGO</span></div></div><div class="run-controls"><button class="run-button" id="run-game">${svg('play')} Ejecutar</button><button class="stop-button" id="stop-game" title="Detener" disabled>${svg('stop')}</button></div><div class="run-meta"><span><i class="mini-dot green"></i> Watch de scripts</span><span>Lua · live</span></div></div><div class="rail-heading agents-heading">ASISTENTES</div><div class="assistant-card"><div class="assistant-head"><span class="agent-orb codex-orb">C</span><div><strong>Codex</strong><small>Compartiendo archivos</small></div><button class="tiny-icon">↗</button></div><p>Abre tu conversación en Codex. Al guardar cambios, este editor se sincroniza con el archivo.</p></div><div class="assistant-card muted-card"><div class="assistant-head"><span class="agent-orb claude-orb">✳</span><div><strong>Claude Code</strong><small id="claude-panel">CLI no detectada</small></div><button class="tiny-icon" id="claude-open" title="Abrir Claude Code en terminal">↗</button></div><p>Instala Claude Code para abrir sesiones de terminal aquí. El chat de Claude aún no se incrusta en esta versión.</p></div><div class="change-heading">CAMBIOS RECIENTES <span id="change-count">0</span></div><div id="change-list" class="change-list"><div class="no-changes">Esperando cambios en el árbol…</div></div></div><section class="bottom-panel" id="bottom-panel"><div class="panel-tabs"><button class="panel-tab active" data-panel="terminal">${svg('terminal')} TERMINAL</button><button class="panel-tab" data-panel="output">SALIDA</button><button class="panel-tab" data-panel="problems">PROBLEMAS <span class="problem-count">0</span></button><div class="panel-tab-spacer"></div><button class="tiny-icon" id="build-project" title="Compilar">${svg('play')}</button><button class="tiny-icon" id="stop-build" title="Detener compilación" disabled>${svg('stop')}</button><button class="tiny-icon" id="clear-output" title="Limpiar">⌫</button></div><div class="panel-content"><div class="terminal-output" id="terminal-output"><div class="terminal-welcome">DWE Studio terminal <span>·</span> <code>apps/studio</code></div><div class="terminal-hint">Compila con <code>scripts/dev.sh build</code> o escribe un comando abajo.</div></div><form id="terminal-form" class="terminal-input-row"><span class="terminal-prompt">❯</span><input id="terminal-input" autocomplete="off" spellcheck="false" placeholder="Escribe un comando…"/><button type="submit" aria-label="Ejecutar comando">${svg('play')}</button></form></div></section></main>
  </div>
  <footer class="statusbar"><div class="status-left"><span class="status-branch">${svg('git')} codex/dwe-studio-live-workbench</span><span id="status-sync">●  Sincronizado</span><span id="status-file-change"></span></div><div class="status-right"><span id="cursor-status">Ln 1, Col 1</span><span id="language-status">DWE Studio</span><span>UTF-8</span><span>Spaces: 2</span><span class="status-ready"><i></i> DWE · local</span></div></footer>
  <div class="toast-stack" id="toasts"></div>
`

document.querySelector('#tabs').insertAdjacentHTML('afterbegin', `<div class="surface-tabs"><button id="editor-mode">${svg('files')} Código</button><button id="compare-mode" class="active">${svg('window')} Comparar</button></div>`)
document.querySelector('.right-rail').insertAdjacentHTML('beforebegin', `
  <section class="comparison-view" id="comparison-view">
    <div class="compare-toolbar"><div><h1>Comparar ejecución</h1><p>DuckStation muestra el binario de PS1; DWE ejecuta el engine del proyecto.</p></div><div class="compare-tools"><button class="observe-button" id="observe-games">${svg('bug')} Iniciar loop IA</button><button class="refresh-capture" id="refresh-captures">${svg('refresh')} Buscar ventanas</button></div></div>
    <div class="compare-grid">
      <article class="capture-card duck-card">
        <header class="capture-header"><div class="capture-brand duck-brand">PS1</div><div class="capture-title"><strong>DuckStation</strong><small>Binario original · PlayStation</small></div><span class="process-indicator" id="duckstation-indicator"></span><div class="capture-actions"><button id="choose-duck-disc">Disco…</button><button id="launch-duckstation" class="capture-launch">${svg('play')} Iniciar</button><button id="stop-duckstation" class="capture-stop" title="Detener" disabled>${svg('stop')}</button></div></header>
        <div class="capture-screen" id="duckstation-screen" tabindex="0" role="button" aria-label="Dar controles a DuckStation"><video id="duckstation-video" autoplay muted playsinline></video><div class="capture-placeholder" id="duckstation-placeholder"><div class="placeholder-cross">+</div><strong>DuckStation</strong><span>Inicia el emulador y captura su ventana aquí.</span></div><div class="capture-live-label" id="duckstation-live-label">SIN CAPTURA</div></div>
        <footer class="capture-footer"><select id="duckstation-source" aria-label="Ventana de DuckStation"><option value="">Buscar una ventana…</option></select><button id="duckstation-capture">Capturar</button><button id="duckstation-stop-capture" disabled>${svg('stop')}</button></footer>
      </article>
      <article class="capture-card engine-card">
        <header class="capture-header"><div class="capture-brand engine-brand">DWE</div><div class="capture-title"><strong>DWE Engine</strong><small>DW1 · ejecución del proyecto</small></div><span class="process-indicator" id="engine-indicator"></span><div class="capture-actions"><button id="launch-engine" class="capture-launch">${svg('play')} Ejecutar</button><button id="stop-engine" class="capture-stop" title="Detener" disabled>${svg('stop')}</button></div></header>
        <div class="capture-screen" id="engine-screen" tabindex="0" role="button" aria-label="Dar controles al motor DWE"><video id="engine-video" autoplay muted playsinline></video><div class="capture-placeholder" id="engine-placeholder"><div class="placeholder-cross">+</div><strong>DWE Engine</strong><span>Ejecuta el juego y captura su ventana aquí.</span></div><div class="capture-live-label" id="engine-live-label">SIN CAPTURA</div></div>
        <footer class="capture-footer"><select id="engine-source" aria-label="Ventana de DWE Engine"><option value="">Buscar una ventana…</option></select><button id="engine-capture">Capturar</button><button id="engine-stop-capture" disabled>${svg('stop')}</button></footer>
      </article>
    </div>
    <section class="observer-panel" id="observer-panel"><div class="observer-heading"><span class="agent-orb codex-orb">C</span><strong>Loop de juego y corrección</strong><span class="observer-state" id="observer-state">Detenido</span><button id="observe-now" disabled>Enviar muestra ahora</button><button id="observe-motion" disabled>Capturar movimiento (2 s)</button></div><div class="observer-response" id="observer-response">Compara secuencias de DuckStation y DWE, guarda capturas/logs para este chat y acepta secuencias de controles de Codex.</div></section>
    <div class="compare-note" id="compare-note"><span class="mini-dot green"></span><span>Haz clic en una pantalla para enviarle el foco; el mando y el teclado controlan ese juego.</span><span id="capture-warning"></span><span id="disc-label">Sin disco seleccionado</span></div>
  </section>
`)
document.querySelector('.run-preview').innerHTML = `<div class="editor-preview-empty">${svg('window')}<span>Las dos ventanas se muestran en Comparar</span><button id="right-compare">Abrir comparación</button></div>`
document.querySelector('.main-area').classList.add('compare-mode')

const editorHost = document.querySelector('#editor')
const editor = monaco.editor.create(editorHost, {
  value: '', language: 'cpp', theme: 'dwe-dark', automaticLayout: true, minimap: { enabled: false },
  fontSize: 13, fontFamily: '"JetBrains Mono", "Cascadia Code", monospace', lineHeight: 22,
  tabSize: 2, insertSpaces: true, scrollBeyondLastLine: false, wordWrap: 'off',
  padding: { top: 10 }, renderLineHighlight: 'all', smoothScrolling: true,
  bracketPairColorization: { enabled: true }, guides: { indentation: true, bracketPairs: true },
  overviewRulerBorder: false, hideCursorInOverviewRuler: true
})
monaco.editor.defineTheme('dwe-dark', {
  base: 'vs-dark', inherit: true, rules: [
    { token: 'keyword', foreground: 'C792EA' }, { token: 'string', foreground: 'C3E88D' },
    { token: 'comment', foreground: '697586', fontStyle: 'italic' }, { token: 'number', foreground: 'F78C6C' }
  ], colors: { 'editor.background': '#15171d', 'editor.foreground': '#c7cbd4', 'editorLineNumber.foreground': '#555d6b', 'editorLineNumber.activeForeground': '#aeb6c5', 'editor.lineHighlightBackground': '#202631', 'editorCursor.foreground': '#70b7ff', 'editor.selectionBackground': '#31435e' }
})

function langFor(file) {
  const ext = file.split('.').pop().toLowerCase()
  return ({ c: 'c', cc: 'cpp', cpp: 'cpp', h: 'cpp', hh: 'cpp', hpp: 'cpp', lua: 'lua', yaml: 'yaml', yml: 'yaml', json: 'json', md: 'markdown', sh: 'shell', py: 'python', cmake: 'cmake' })[ext] || 'plaintext'
}
function fileGlyph(file) {
  const ext = file.split('.').pop().toLowerCase()
  const cls = ['cpp','cc','c','hpp','h'].includes(ext) ? 'cpp' : ext === 'lua' ? 'lua' : ['yaml','yml'].includes(ext) ? 'yaml' : ext === 'md' ? 'md' : ext === 'json' ? 'json' : 'other'
  return `<span class="file-type ${cls}">${cls === 'cpp' ? 'C++' : cls === 'lua' ? '◆' : cls === 'yaml' ? 'Y' : cls === 'md' ? 'M' : cls === 'json' ? '{}' : '·'}</span>`
}
function renderTree(nodes, host, depth = 0, namespace = 'project') {
  for (const node of nodes) {
    const row = document.createElement('div')
    const fileKey = namespace === 'decomp' ? `decomp:${node.path}` : node.path
    row.className = `tree-row ${node.type} ${state.activePath === fileKey ? 'selected' : ''}`
    row.style.setProperty('--depth', depth)
    if (node.type === 'directory') {
      row.innerHTML = `${svg('chevron')} ${svg('folder')}<span>${node.name}</span>`
      const children = document.createElement('div')
      children.className = 'tree-children'
      row.addEventListener('click', () => { row.classList.toggle('collapsed'); children.hidden = !children.hidden })
      host.append(row, children)
      renderTree(node.children || [], children, depth + 1, namespace)
    } else {
      row.innerHTML = `${fileGlyph(node.name)}<span>${node.name}</span>${state.dirty.has(node.path) ? '<i class="dirty-mark"></i>' : ''}`
      row.addEventListener('click', () => openFile(fileKey))
      host.append(row)
    }
  }
}
async function refreshTree() {
  try {
    state.files = await api.tree()
    const tree = document.querySelector('#file-tree')
    tree.innerHTML = ''
    renderTree(state.files, tree)
  } catch (error) { toast(error.message, 'error') }
}
async function refreshDecompTree() {
  const tree = document.querySelector('#decomp-tree')
  try {
    const files = await api.referenceTree()
    tree.innerHTML = ''
    renderTree(files, tree, 0, 'decomp')
  } catch {
    tree.innerHTML = '<div class="decomp-unavailable">Configura <code>DWE_DECOMP</code> para ver la referencia.</div>'
  }
}
function findTab(file) { return state.tabs.find((tab) => tab.path === file) }
function renderTabs() {
  const host = document.querySelector('#tabs')
  host.querySelectorAll('.file-tab').forEach((tab) => tab.remove())
  host.querySelector('.empty-tabs')?.remove()
  for (const tab of state.tabs) {
    const button = document.createElement('button')
    button.className = `file-tab ${tab.path === state.activePath ? 'active' : ''}`
    button.innerHTML = `${fileGlyph(tab.name)}<span>${tab.name}</span>${tab.readOnly ? '<i class="readonly-lock">R</i>' : ''}${state.dirty.has(tab.path) ? '<i class="dirty-tab"></i>' : ''}<span class="tab-close">${svg('close')}</span>`
    button.addEventListener('click', (event) => {
      if (event.target.closest('.tab-close')) { closeTab(tab.path); return }
      activateTab(tab.path)
    })
    host.insertBefore(button, host.querySelector('.tab-tools'))
  }
  if (!state.tabs.length) host.insertAdjacentHTML('afterbegin', '<div class="empty-tabs">DWE Studio</div>')
}
async function openFile(file) {
  try {
    let tab = findTab(file)
    if (!tab) {
      const readOnly = file.startsWith('decomp:')
      const sourcePath = file.replace(/^decomp:/, '')
      const data = readOnly ? await api.readReference(sourcePath) : await api.read(sourcePath)
      tab = { path: file, name: pathName(sourcePath), readOnly, content: data.content, model: monaco.editor.createModel(data.content, langFor(sourcePath), monaco.Uri.parse(`${readOnly ? 'dwe-decomp' : 'file'}:///${sourcePath}`)) }
      state.tabs.push(tab)
      tab.model.onDidChangeContent(() => {
        if (tab.model.getValue() === tab.content) state.dirty.delete(tab.path)
        else state.dirty.add(tab.path)
        renderTabs(); refreshTree()
      })
    }
    activateTab(file)
  } catch (error) { toast(error.message, 'error') }
}
function pathName(file) { return file.split('/').pop() }
function activateTab(file) {
  const tab = findTab(file)
  if (!tab) return
  state.activePath = file
  editor.setModel(tab.model)
  editor.updateOptions({ readOnly: tab.readOnly })
  const sourcePath = file.replace(/^decomp:/, '')
  document.querySelector('#breadcrumb').innerHTML = `${tab.readOnly ? '<span class="read-only-pill">DW_DECOMP · SOLO LECTURA</span><span class="crumb-chevron">›</span>' : ''}${sourcePath.split('/').map((part, index) => `${index ? '<span class="crumb-chevron">›</span>' : ''}<span>${part}</span>`).join('')}`
  document.querySelector('#language-status').textContent = langFor(sourcePath).toUpperCase()
  document.querySelector('.welcome').hidden = true
  editorHost.classList.add('has-model')
  editor.focus()
  renderTabs(); refreshTree()
}
function closeTab(file) {
  if (state.dirty.has(file) && !confirm(`Hay cambios sin guardar en ${pathName(file)}. ¿Cerrar de todos modos?`)) return
  const tab = findTab(file)
  tab?.model.dispose()
  state.tabs = state.tabs.filter((item) => item.path !== file)
  state.dirty.delete(file)
  if (state.activePath === file) {
    state.activePath = state.tabs.at(-1)?.path || null
    if (state.activePath) activateTab(state.activePath)
    else { editor.setModel(null); document.querySelector('.welcome').hidden = false; editorHost.classList.remove('has-model') }
  }
  renderTabs(); refreshTree()
}
async function saveActive() {
  const tab = findTab(state.activePath)
  if (!tab) return
  if (tab.readOnly) { toast('El decomp está montado como referencia de solo lectura.'); return }
  try {
    await api.write(tab.path, tab.model.getValue())
    tab.content = tab.model.getValue()
    state.dirty.delete(tab.path)
    document.querySelector('#status-sync').textContent = '●  Guardado'
    setTimeout(() => { document.querySelector('#status-sync').textContent = '●  Sincronizado' }, 1500)
    renderTabs(); refreshTree(); toast(`${pathName(tab.path)} guardado`, 'success')
  } catch (error) { toast(error.message, 'error') }
}
function appendOutput(kind, text, stream = 'stdout') {
  const output = document.querySelector('#terminal-output')
  const line = document.createElement('div')
  line.className = `output-line ${stream === 'stderr' ? 'stderr' : ''}`
  line.innerHTML = `<span class="output-source">${escapeHtml(kind)}</span><pre>${escapeHtml(text)}</pre>`
  output.append(line); output.scrollTop = output.scrollHeight
  state.output.push({ kind, text, stream, time: Date.now() })
  if (state.output.length > 1000) state.output.splice(0, state.output.length - 1000)
  if (state.observing && ['game', 'duckstation'].includes(kind) && /\b(error|fatal|assert|exception|segmentation fault|segfault|crash)\b/i.test(text)) {
    clearTimeout(state.observerErrorTimer)
    state.observerErrorTimer = setTimeout(() => void observeGames(), 900)
  }
}
function observerStatus(status, message = '') {
  const label = document.querySelector('#observer-state')
  const panel = document.querySelector('#observer-panel')
  const live = state.observing && status !== 'error'
  label.textContent = message || ({ connecting: 'Capturando…', capturing: 'Guardando muestra para Codex…', ready: live ? 'Loop activo · esperando revisión de Codex' : 'Listo', analyzing: 'Codex está revisando…', stopped: 'Loop detenido', error: 'Error de captura' }[status] || status)
  panel.classList.toggle('observer-active', live)
  panel.classList.toggle('observer-error', status === 'error')
}
function reportObserverRuntime(status, error = null) {
  const labelFor = (kind) => document.querySelector(`#${kind}-source`).selectedOptions[0]?.textContent || ''
  const gameSources = state.captureSources
    .filter((source) => /duckstation|psx|playstation|\b(dwe(?:-game)?|dw1|digimon world)\b/i.test(source.name))
    .map((source) => source.name)
  void api.reportObserverRuntime({
    status,
    error,
    availableSources: gameSources,
    selectedSources: { duckstation: labelFor('duckstation'), engine: labelFor('engine') },
    captures: { duckstation: state.captureStreams.has('duckstation'), engine: state.captureStreams.has('engine') },
    observing: state.observing
  }).catch(() => {})
}
function captureFrame(kind, { width = 800, quality = 0.7 } = {}) {
  const video = document.querySelector(`#${kind}-video`)
  if (!state.captureStreams.has(kind) || video.readyState < HTMLMediaElement.HAVE_CURRENT_DATA || !video.videoWidth || !video.videoHeight) return null
  const scale = Math.min(1, width / video.videoWidth)
  const canvas = document.createElement('canvas')
  canvas.width = Math.round(video.videoWidth * scale)
  canvas.height = Math.round(video.videoHeight * scale)
  const context = canvas.getContext('2d', { alpha: false })
  context.drawImage(video, 0, 0, canvas.width, canvas.height)
  return { image: canvas.toDataURL('image/jpeg', quality), capturedAt: new Date().toISOString() }
}
async function captureBurst({ motion = false } = {}) {
  const frames = { duckstation: [], engine: [], frameTimes: { duckstation: [], engine: [] } }
  const count = motion ? 40 : 3
  const interval = motion ? 50 : 500
  const started = performance.now()
  for (let index = 0; index < count; index += 1) {
    for (const kind of ['duckstation', 'engine']) {
      const frame = captureFrame(kind, motion ? { width: 640, quality: 0.65 } : {})
      if (frame) {
        frames[kind].push(frame.image)
        frames.frameTimes[kind].push(frame.capturedAt)
      }
    }
    if (index < count - 1) await new Promise((resolve) => setTimeout(resolve, Math.max(0, started + (index + 1) * interval - performance.now())))
  }
  return frames
}

async function decodeFrame(dataUrl) {
  const image = new Image()
  image.src = dataUrl
  await image.decode()
  return image
}

async function comparisonImage(frames) {
  if (!frames.duckstation.length || !frames.engine.length) return null
  const images = await Promise.all(['duckstation', 'engine'].map((kind) => decodeFrame(frames[kind].at(-1))))
  const canvas = document.createElement('canvas')
  canvas.width = images[0].width + images[1].width + 16
  canvas.height = Math.max(...images.map((image) => image.height)) + 32
  const context = canvas.getContext('2d', { alpha: false })
  context.fillStyle = '#101419'
  context.fillRect(0, 0, canvas.width, canvas.height)
  context.fillStyle = '#ffffff'
  context.font = '16px sans-serif'
  const labels = ['PS1 original · DuckStation', 'Remake · DWE']
  let x = 0
  for (const [index, image] of images.entries()) {
    context.fillText(labels[index], x + 8, 22)
    context.drawImage(image, x, 32)
    x += image.width + 16
  }
  return canvas.toDataURL('image/jpeg', 0.8)
}

async function motionSheet(frames, kind) {
  const count = Math.min(12, frames[kind].length)
  if (!count) return null
  const indices = Array.from({ length: count }, (_, index) => Math.round(index * (frames[kind].length - 1) / Math.max(1, count - 1)))
  const images = await Promise.all(indices.map((index) => decodeFrame(frames[kind][index])))
  const width = 320
  const height = Math.round(images[0].height * width / images[0].width)
  const canvas = document.createElement('canvas')
  canvas.width = width * 4
  canvas.height = (height + 28) * Math.ceil(count / 4)
  const context = canvas.getContext('2d', { alpha: false })
  context.fillStyle = '#101419'
  context.fillRect(0, 0, canvas.width, canvas.height)
  context.font = '14px sans-serif'
  const start = Date.parse(frames.frameTimes[kind][0])
  for (const [index, image] of images.entries()) {
    const x = (index % 4) * width
    const y = Math.floor(index / 4) * (height + 28)
    const elapsed = Date.parse(frames.frameTimes[kind][indices[index]]) - start
    context.fillStyle = '#ffffff'
    context.fillText(`${kind === 'duckstation' ? 'PS1' : 'DWE'} · #${indices[index] + 1} · ${elapsed} ms`, x + 6, y + 19)
    context.drawImage(image, x, y + 28, width, height)
  }
  return canvas.toDataURL('image/jpeg', 0.8)
}

let observationTask = null
function observeGames(options = {}) {
  if (observationTask) {
    return options.actionId || options.motion
      ? observationTask.then(() => state.observing ? observeGames(options) : null)
      : observationTask
  }
  observationTask = collectObservation(options).finally(() => { observationTask = null })
  return observationTask
}

async function collectObservation({ actionId = null, motion = false } = {}) {
  state.observingBusy = true
  document.querySelector('#observe-motion').disabled = true
  const response = document.querySelector('#observer-response')
  try {
    const frames = await captureBurst({ motion })
    if (!frames.duckstation.length && !frames.engine.length) throw new Error('Las ventanas seleccionadas no entregan fotogramas de vídeo.')
    if ((actionId || motion) && (!frames.duckstation.length || !frames.engine.length)) throw new Error('Faltan fotogramas de uno de los dos juegos.')
    response.textContent = `${response.textContent}\n\n── Muestra ${new Date().toLocaleTimeString()} ──\n`.slice(-4000)
    response.scrollTop = response.scrollHeight
    observerStatus('capturing')
    const recent = state.output.slice(Math.max(0, state.observerLogCursor), state.output.length)
    const initial = state.observerLogCursor === 0 ? state.output.slice(-80) : recent
    const logs = initial
      .filter((item) => ['game', 'duckstation', 'build'].includes(item.kind))
      .map((item) => `[${new Date(item.time).toLocaleTimeString()}] ${item.kind}${item.stream === 'stderr' ? ' stderr' : ''}: ${item.text}`)
      .join('')
      .slice(-20_000)
    state.observerLogCursor = state.output.length
    const comparison = await comparisonImage(frames)
    const motionSheets = motion ? Object.fromEntries(await Promise.all(['duckstation', 'engine'].map(async (kind) => [kind, await motionSheet(frames, kind)]))) : null
    return await api.observeGames({
      ...frames, logs, actionId, motion, comparison, motionSheets,
      sourceNames: {
        duckstation: document.querySelector('#duckstation-source').selectedOptions[0]?.textContent,
        engine: document.querySelector('#engine-source').selectedOptions[0]?.textContent
      }
    })
  } catch (error) {
    if (actionId) {
      await api.failActionObservation(actionId, error.message).catch(() => {})
      stopGameObservation()
    }
    reportObserverRuntime('capture-error', error.message)
    if (state.observing) {
      response.append(document.createTextNode(`\nError: ${error.message}`))
      observerStatus('error', error.message)
    }
    if (actionId) observerStatus('error', error.message)
  } finally {
    state.observingBusy = false
    document.querySelector('#observe-motion').disabled = !state.observing
    response.scrollTop = response.scrollHeight
    if (state.observing && !document.querySelector('#observer-panel').classList.contains('observer-error')) observerStatus('ready')
  }
}
function beginGameObservation({ automatic = false } = {}) {
  if (!state.captureStreams.has('duckstation') || !state.captureStreams.has('engine')) {
    toast('Captura DuckStation y DWE Engine antes de iniciar el loop comparativo.', 'info')
    return
  }
  state.observing = true
  localStorage.setItem('dwe-studio-loop', 'on')
  const button = document.querySelector('#observe-games')
  const nowButton = document.querySelector('#observe-now')
  state.observerLogCursor = 0
  clearInterval(state.observerAutoTimer)
  state.observerAutoTimer = null
  button.innerHTML = `${svg('stop')} Detener loop`
  button.classList.add('active')
  nowButton.disabled = false
  document.querySelector('#observe-motion').disabled = false
  void api.setObserverActive(true)
  reportObserverRuntime('observing')
  observerStatus('connecting', automatic ? 'Autoloop iniciado · capturando los dos juegos' : '')
  void observeGames()
  clearInterval(state.observerTimer)
  state.observerTimer = setInterval(() => void observeGames(), 20_000)
}
function stopGameObservation() {
  state.observing = false
  localStorage.setItem('dwe-studio-loop', 'off')
  clearInterval(state.observerTimer)
  state.observerTimer = null
  clearInterval(state.observerAutoTimer)
  state.observerAutoTimer = null
  document.querySelector('#observe-games').innerHTML = `${svg('bug')} Iniciar loop IA`
  document.querySelector('#observe-games').classList.remove('active')
  document.querySelector('#observe-now').disabled = true
  document.querySelector('#observe-motion').disabled = true
  void api.setObserverActive(false)
  reportObserverRuntime('stopped')
  observerStatus('stopped')
}
function toggleGameObservation() {
  if (state.observing) stopGameObservation()
  else beginGameObservation()
}
async function initializeGameObservation() {
  if (localStorage.getItem('dwe-studio-loop') === 'off') return
  let attemptBusy = false
  const attempt = async () => {
    if (state.observing || localStorage.getItem('dwe-studio-loop') === 'off') return true
    if (attemptBusy) return false
    attemptBusy = true
    try {
      await refreshCaptureSources()
      if (!document.querySelector('#duckstation-source').value || !document.querySelector('#engine-source').value) return false
      for (const kind of ['duckstation', 'engine']) {
        if (!state.captureStreams.has(kind)) await startWindowCapture(kind, { automatic: true })
      }
      if (!state.captureStreams.has('duckstation') || !state.captureStreams.has('engine')) return false
      beginGameObservation({ automatic: true })
      return true
    } finally {
      attemptBusy = false
    }
  }
  if (await attempt()) return
  state.observerAutoTimer = setInterval(() => { void attempt() }, 15_000)
}
function escapeHtml(text) { return text.replace(/[&<>"']/g, (char) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[char]) }
function toast(message, type = 'info') {
  const el = document.createElement('div'); el.className = `toast ${type}`; el.textContent = message
  document.querySelector('#toasts').append(el); setTimeout(() => el.remove(), 3000)
}
function setRunning(kind, running, managed = running) {
  if (running) state.running.add(kind); else state.running.delete(kind)
  if (running && managed) state.managedRunning.add(kind); else state.managedRunning.delete(kind)
  if (kind === 'game') {
    document.querySelector('#game-state').textContent = running ? 'Ejecutándose · ventana nativa' : 'Listo para ejecutar'
    document.querySelector('#game-dot').classList.toggle('on', running)
    document.querySelector('#run-game').disabled = running
    document.querySelector('#stop-game').disabled = !running || !state.managedRunning.has(kind)
    document.querySelector('#launch-engine').disabled = running
    document.querySelector('#stop-engine').disabled = !running || !state.managedRunning.has(kind)
    document.querySelector('#engine-indicator').classList.toggle('on', running)
  }
  if (kind === 'duckstation') {
    document.querySelector('#launch-duckstation').disabled = running
    document.querySelector('#stop-duckstation').disabled = !running || !state.managedRunning.has(kind)
    document.querySelector('#duckstation-indicator').classList.toggle('on', running)
  }
  if (kind === 'build') {
    document.querySelector('#build-project').disabled = running
    document.querySelector('#stop-build').disabled = !running
    if (running) appendOutput('build', 'Compilando con scripts/dev.sh build…\n', 'stdout')
  }
}
function setWorkspaceMode(mode) {
  state.workspaceMode = mode
  document.querySelector('.main-area').classList.toggle('compare-mode', mode === 'compare')
  document.querySelector('#editor-mode').classList.toggle('active', mode === 'editor')
  document.querySelector('#compare-mode').classList.toggle('active', mode === 'compare')
  if (mode === 'editor') editor.layout()
}
function captureSourceGroups(sources) {
  const engineWindows = sources.filter((source) => /\b(dwe(?:-game)?|dw1)\b/i.test(source.name))
  const duckstationWindows = sources.filter((source) => /duckstation|psx|playstation|\b(?:SLUS|SCUS|SLES|SCES)[-_ ]?\d{3,}/i.test(source.name))
  const worldTitleWindows = sources.filter((source) => /digimon world/i.test(source.name))
  const engineCandidates = engineWindows.length ? engineWindows : (!duckstationWindows.length ? worldTitleWindows : [])
  const duckstationCandidates = duckstationWindows.length
    ? duckstationWindows
    : engineWindows.length ? worldTitleWindows : sources.filter((source) => !engineCandidates.includes(source))
  return {
    duckstation: { candidates: duckstationCandidates, preferred: duckstationWindows[0] || (engineWindows.length ? worldTitleWindows[0] : null) },
    engine: { candidates: engineCandidates, preferred: engineWindows[0] || engineCandidates[0] }
  }
}
function renderCaptureSources() {
  const assignments = captureSourceGroups(state.captureSources)
  for (const [kind, assignment] of Object.entries(assignments)) {
    const group = { id: `${kind}-source`, ...assignment }
    const select = document.querySelector(`#${group.id}`)
    const previous = select.value
    const placeholder = group.id === 'engine-source' && !group.candidates.length
      ? 'Ejecuta DWE para detectar su ventana…'
      : 'Elige la ventana del juego…'
    select.innerHTML = `<option value="">${placeholder}</option>` + group.candidates
      .map((source) => `<option value="${escapeHtml(source.id)}">${escapeHtml(source.name)}</option>`).join('')
    select.value = group.candidates.some((source) => source.id === previous) ? previous : group.preferred?.id || ''
    if (state.captureStreams.has(kind)) {
      if (select.value !== previous) void startWindowCapture(kind)
      else syncCaptureSelection(kind)
    }
  }
  const note = document.querySelector('#compare-note')
  const warning = document.querySelector('#capture-warning')
  note.classList.toggle('capture-limitation', state.captureSources.length < 2)
  warning.textContent = state.captureSources.length === 0
    ? 'Capturar abre el selector del sistema; Buscar ventanas llena la lista cuando está disponible.'
    : state.captureSources.length === 1
      ? 'Solo hay una ventana de captura disponible; PipeWire en Linux puede limitar capturas simultáneas.'
      : ''
}
async function refreshCaptureSources() {
  try {
    state.captureSources = await api.captureSources()
    renderCaptureSources()
    const assignments = captureSourceGroups(state.captureSources)
    const hasDuckStation = assignments.duckstation.candidates.length > 0
    const hasEngine = assignments.engine.candidates.length > 0
    const duckstationManaged = state.managedRunning.has('duckstation')
    const engineManaged = state.managedRunning.has('game')
    setRunning('duckstation', hasDuckStation || duckstationManaged, duckstationManaged)
    setRunning('game', hasEngine || engineManaged, engineManaged)
    reportObserverRuntime(hasDuckStation && hasEngine ? 'windows-detected' : 'waiting-for-windows',
      hasDuckStation && hasEngine ? null : `Ventanas reconocidas: DuckStation=${hasDuckStation}, DWE=${hasEngine}; fuentes visibles=${state.captureSources.length}.`)
  } catch (error) {
    document.querySelector('#compare-note').classList.add('capture-limitation')
    document.querySelector('#capture-warning').textContent = `No se pudieron enumerar las ventanas: ${error.message}`
    reportObserverRuntime('source-enumeration-error', error.message)
  }
}
function syncCaptureSelection(kind) {
  const select = document.querySelector(`#${kind}-source`)
  api.setCaptureSource(kind, select.value).catch((error) => toast(error.message, 'error'))
}
async function waitForCaptureSource(kind, attempts = 40) {
  const select = document.querySelector(`#${kind}-source`)
  for (let attempt = 0; attempt < attempts && !select.value; attempt += 1) {
    await new Promise((resolve) => setTimeout(resolve, 750))
    await refreshCaptureSources()
  }
}
async function startWindowCapture(kind, { automatic = false } = {}) {
  const select = document.querySelector(`#${kind}-source`)
  const sourceId = select.value
  if (!sourceId) { toast('Selecciona primero la ventana que quieres mostrar.'); return }
  try {
    stopWindowCapture(kind)
    const stream = await navigator.mediaDevices.getUserMedia({
      audio: false,
      video: { mandatory: { chromeMediaSource: 'desktop', chromeMediaSourceId: sourceId, maxWidth: 1280, maxHeight: 720, maxFrameRate: 30 } }
    })
    const video = document.querySelector(`#${kind}-video`)
    state.captureStreams.set(kind, stream)
    video.srcObject = stream
    await video.play()
    await api.setCaptureSource(kind, sourceId)
    document.querySelector(`#${kind}-placeholder`).hidden = true
    document.querySelector(`#${kind}-live-label`).textContent = sourceId && state.captureSources.length > 1
      ? select.selectedOptions[0]?.textContent || 'CAPTURA EN VIVO'
      : stream.getVideoTracks()[0]?.label || 'CAPTURA EN VIVO'
    document.querySelector(`#${kind}-live-label`).classList.add('on')
    document.querySelector(`#${kind}-stop-capture`).disabled = false
    stream.getVideoTracks()[0]?.addEventListener('ended', () => stopWindowCapture(kind), { once: true })
    reportObserverRuntime('capturing')
    return true
  } catch (error) {
    if (!automatic) toast(`No se pudo capturar esa ventana: ${error.message}`, 'error')
    reportObserverRuntime('capture-error', error.message)
    return false
  }
}
async function focusGame(kind) {
  const sourceId = document.querySelector(`#${kind}-source`).value
  if (!sourceId) {
    toast('Primero busca y selecciona la ventana del juego.', 'info')
    return
  }
  try {
    await api.focusGameWindow(sourceId)
    state.activeGame = kind
    document.querySelector('.duck-card').classList.toggle('game-focus', kind === 'duckstation')
    document.querySelector('.engine-card').classList.toggle('game-focus', kind === 'engine')
  } catch (error) {
    toast(`No se pudo dar el foco a la ventana: ${error.message}`, 'error')
  }
}
function stopWindowCapture(kind) {
  const stream = state.captureStreams.get(kind)
  stream?.getTracks().forEach((track) => track.stop())
  state.captureStreams.delete(kind)
  const video = document.querySelector(`#${kind}-video`)
  if (video) video.srcObject = null
  const placeholder = document.querySelector(`#${kind}-placeholder`)
  if (placeholder) placeholder.hidden = false
  const label = document.querySelector(`#${kind}-live-label`)
  if (label) { label.textContent = 'SIN CAPTURA'; label.classList.remove('on') }
  const stopButton = document.querySelector(`#${kind}-stop-capture`)
  if (stopButton) stopButton.disabled = true
  void api.setCaptureSource(kind, '')
  reportObserverRuntime('capture-stopped')
}
function changedFile(event) {
  const index = state.changed.findIndex((item) => item.path === event.path)
  if (index >= 0) state.changed.splice(index, 1)
  state.changed.unshift(event)
  state.changed = state.changed.slice(0, 8)
  document.querySelector('#change-count').textContent = String(state.changed.length)
  document.querySelector('#change-list').innerHTML = state.changed.map((item) => `<div class="change-item"><span class="change-kind ${item.event}">${item.event === 'change' ? 'M' : item.event === 'add' ? 'A' : 'D'}</span><span class="change-path">${escapeHtml(item.path)}</span><time>ahora</time></div>`).join('')
  document.querySelector('#status-file-change').textContent = `↻ ${event.path}`
  const tab = findTab(event.path)
  if (tab && !state.dirty.has(tab.path)) {
    api.read(tab.path).then(({ content }) => {
      tab.content = content; tab.model.setValue(content)
    }).catch(() => {})
  } else if (tab && state.dirty.has(tab.path)) toast(`${pathName(tab.path)} cambió en disco; tus cambios locales se conservaron.`, 'info')
  refreshTree()
}
function flatten(files, result = []) {
  for (const node of files) node.type === 'directory' ? flatten(node.children || [], result) : result.push(node.path)
  return result
}
function openQuickFile() {
  const existing = document.querySelector('.quick-open')
  if (existing) { existing.remove(); return }
  const modal = document.createElement('div'); modal.className = 'quick-open'
  modal.innerHTML = `<input placeholder="Buscar archivo por nombre…" autofocus><div class="quick-results"></div>`
  document.body.append(modal)
  const input = modal.querySelector('input'); input.focus()
  const draw = () => {
    const query = input.value.toLowerCase(); const matches = flatten(state.files).filter((file) => file.toLowerCase().includes(query)).slice(0, 12)
    modal.querySelector('.quick-results').innerHTML = matches.map((file) => `<button>${fileGlyph(file)} ${escapeHtml(file)}</button>`).join('')
    modal.querySelectorAll('button').forEach((button, index) => button.addEventListener('click', () => { openFile(matches[index]); modal.remove() }))
  }
  input.addEventListener('input', draw); draw()
  modal.addEventListener('click', (event) => { if (event.target === modal) modal.remove() })
  input.addEventListener('keydown', (event) => { if (event.key === 'Escape') modal.remove(); if (event.key === 'Enter') modal.querySelector('button')?.click() })
}

editor.onDidChangeCursorPosition(({ position }) => { document.querySelector('#cursor-status').textContent = `Ln ${position.lineNumber}, Col ${position.column}` })
document.querySelector('#refresh-tree').addEventListener('click', refreshTree)
document.querySelector('#quick-open').addEventListener('click', openQuickFile)
document.querySelector('#decomp-heading').addEventListener('click', () => {
  const tree = document.querySelector('#decomp-tree')
  tree.hidden = !tree.hidden
  document.querySelector('#decomp-heading').classList.toggle('collapsed', tree.hidden)
})
document.querySelector('#open-main').addEventListener('click', () => openFile('apps/game/src/main.cpp'))
document.querySelector('#open-script').addEventListener('click', () => openFile('apps/game/scripts/main.lua'))
document.querySelector('#choose-root').addEventListener('click', async () => {
  let project
  try { project = await api.chooseRoot() }
  catch (error) { toast(error.message, 'error'); return }
  if (!project) return
  for (const tab of state.tabs) tab.model.dispose()
  state.tabs = []; state.activePath = null; state.dirty.clear(); state.changed = []
  editor.setModel(null)
  document.querySelector('.welcome').hidden = false
  editorHost.classList.remove('has-model')
  document.querySelector('#project-name').textContent = project.name
  document.querySelector('#tree-root').textContent = project.name.toUpperCase()
  document.querySelector('#change-list').innerHTML = '<div class="no-changes">Esperando cambios en el árbol…</div>'
  document.querySelector('#change-count').textContent = '0'
  renderTabs(); refreshTree()
})
document.querySelector('#window-min').addEventListener('click', () => api.windowControl('minimize'))
document.querySelector('#window-max').addEventListener('click', () => api.windowControl('toggle-maximize'))
document.querySelector('#window-close').addEventListener('click', () => api.windowControl('close'))
document.querySelector('#editor-mode').addEventListener('click', () => setWorkspaceMode('editor'))
document.querySelector('#compare-mode').addEventListener('click', () => setWorkspaceMode('compare'))
document.querySelector('#right-compare').addEventListener('click', () => setWorkspaceMode('compare'))
document.querySelector('#refresh-captures').addEventListener('click', refreshCaptureSources)
document.querySelector('#observe-games').addEventListener('click', toggleGameObservation)
document.querySelector('#observe-now').addEventListener('click', () => void observeGames())
document.querySelector('#observe-motion').addEventListener('click', () => void observeGames({ motion: true }))
document.querySelector('#choose-duck-disc').addEventListener('click', async () => {
  try {
    const selected = await api.chooseDuckstationDisc()
    if (!selected) return
    state.discPath = selected
    document.querySelector('#disc-label').textContent = pathName(selected)
  } catch (error) { toast(error.message, 'error') }
})
document.querySelector('#launch-duckstation').addEventListener('click', async () => {
  try {
    await api.launchDuckstation(state.discPath)
    toast(state.discPath ? `DuckStation abrió ${pathName(state.discPath)}.` : 'DuckStation iniciado; selecciona una ventana para capturar.')
    void waitForCaptureSource('duckstation')
  } catch (error) { toast(error.message, 'error') }
})
document.querySelector('#stop-duckstation').addEventListener('click', () => api.stop('duckstation'))
document.querySelector('#launch-engine').addEventListener('click', async () => {
  try { await api.runGame() }
  catch (error) { toast(error.message, 'error') }
})
document.querySelector('#stop-engine').addEventListener('click', () => api.stop('game'))
document.querySelector('#duckstation-capture').addEventListener('click', () => startWindowCapture('duckstation'))
document.querySelector('#engine-capture').addEventListener('click', () => startWindowCapture('engine'))
document.querySelector('#duckstation-stop-capture').addEventListener('click', () => stopWindowCapture('duckstation'))
document.querySelector('#engine-stop-capture').addEventListener('click', () => stopWindowCapture('engine'))
document.querySelector('#duckstation-screen').addEventListener('pointerdown', () => focusGame('duckstation'))
document.querySelector('#engine-screen').addEventListener('pointerdown', () => focusGame('engine'))
document.querySelector('#duckstation-source').addEventListener('change', () => {
  if (state.captureStreams.has('duckstation')) void startWindowCapture('duckstation')
})
document.querySelector('#engine-source').addEventListener('change', () => {
  if (state.captureStreams.has('engine')) void startWindowCapture('engine')
})
document.querySelector('#duckstation-source').addEventListener('change', () => focusGame('duckstation'))
document.querySelector('#engine-source').addEventListener('change', () => focusGame('engine'))
document.querySelector('#build-project').addEventListener('click', () => api.build().catch((error) => toast(error.message, 'error')))
document.querySelector('#run-game').addEventListener('click', () => api.runGame().catch((error) => toast(error.message, 'error')))
document.querySelector('#stop-game').addEventListener('click', () => api.stop('game'))
document.querySelector('#stop-build').addEventListener('click', () => api.stop('build'))
document.querySelector('#clear-output').addEventListener('click', () => { document.querySelector('#terminal-output').innerHTML = '' })
document.querySelector('#terminal-form').addEventListener('submit', async (event) => {
  event.preventDefault(); const input = document.querySelector('#terminal-input'); const command = input.value.trim(); if (!command) return
  appendOutput('terminal', `❯ ${command}\n`); input.value = ''
  try { await api.runCommand(command) } catch (error) { appendOutput('terminal', `${error.message}\n`, 'stderr') }
})
document.querySelectorAll('.panel-tab').forEach((button) => button.addEventListener('click', () => {
  document.querySelectorAll('.panel-tab').forEach((item) => item.classList.toggle('active', item === button))
  if (button.dataset.panel === 'output') document.querySelector('#terminal-output').classList.add('show-output')
  else document.querySelector('#terminal-output').classList.remove('show-output')
  if (button.dataset.panel === 'problems') toast('Los diagnósticos del compilador aparecerán aquí en una siguiente etapa.')
}))
document.querySelector('#claude-open').addEventListener('click', () => { document.querySelector('#terminal-input').value = 'claude'; document.querySelector('#terminal-input').focus() })
document.addEventListener('keydown', (event) => {
  if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 's') { event.preventDefault(); saveActive() }
  if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'b') { event.preventDefault(); api.build().catch((error) => toast(error.message, 'error')) }
  if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'p') { event.preventDefault(); openQuickFile() }
  if (event.key === 'F5') { event.preventDefault(); api.runGame().catch((error) => toast(error.message, 'error')) }
})

api.onOutput(({ kind, text, stream }) => appendOutput(kind.replace('terminal-', 'terminal'), text, stream))
api.onObserverStatus(({ status, message }) => {
  if (status === 'action-capture-error') {
    stopGameObservation()
    reportObserverRuntime('capture-error', message)
    observerStatus('error', message)
  } else observerStatus(status, message)
})
api.onObserverSample(({ id }) => {
  document.querySelector('#observer-state').textContent = `Muestra ${id} enviada a la cola`
})
api.onObserverAction(({ id, actions }) => {
  const label = actions.map((action) => `${action.game}: ${action.type === 'text' ? `texto “${action.text}”` : action.button}`).join(' · ')
  const response = document.querySelector('#observer-response')
  response.textContent = `${response.textContent}\nControles enviados por Codex: ${label}\n`.slice(-4000)
  response.scrollTop = response.scrollHeight
  if (state.observing) void observeGames({ actionId: id })
})
api.onProcessState(({ kind, running, managed, code }) => {
  if (kind !== 'terminal') setRunning(kind, running, managed ?? running)
  if (running && (kind === 'game' || kind === 'duckstation')) void waitForCaptureSource(kind === 'game' ? 'engine' : 'duckstation')
  if (!running) {
    if (kind === 'build') appendOutput('build', `\nCompilación finalizada con código ${code ?? 'desconocido'}.\n`, code === 0 ? 'stdout' : 'stderr')
    if (kind === 'game') appendOutput('game', `\nDWE Engine se cerró (código ${code ?? 'desconocido'}).\n`)
    if (kind === 'duckstation') appendOutput('duckstation', `\nDuckStation se cerró (código ${code ?? 'desconocido'}).\n`)
  }
})
api.onFileChange(changedFile)
const info = await api.info()
api.processStates().then((states) => {
  for (const [kind, status] of Object.entries(states)) setRunning(kind, status.running, status.managed)
}).catch(() => {})
document.querySelector('#project-name').textContent = info.name
document.querySelector('#tree-root').textContent = info.name.toUpperCase()
if (info.discPath) {
  state.discPath = info.discPath
  document.querySelector('#disc-label').textContent = pathName(info.discPath)
}
refreshTree()
void initializeGameObservation()
if (info.decompAvailable) refreshDecompTree()
else {
  document.querySelector('#decomp-heading small').textContent = 'NO ENCONTRADO'
  document.querySelector('#decomp-heading').title = 'Define DWE_DECOMP para apuntar al decomp local.'
}
if (info.platform === 'darwin') document.querySelector('#window-controls').hidden = true
api.agents().then(({ codexCli, claudeCode }) => {
  if (!codexCli) document.querySelector('.agent-row .codex-orb').closest('.agent-row').querySelector('.connection-dot').classList.remove('on')
  document.querySelector('#claude-status').textContent = claudeCode ? 'CLI detectada' : 'CLI no instalada'
  document.querySelector('#claude-panel').textContent = claudeCode ? 'Disponible en terminal' : 'CLI no detectada'
  document.querySelector('#claude-dot').classList.toggle('on', claudeCode)
})
