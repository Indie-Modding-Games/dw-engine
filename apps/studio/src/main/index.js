import { app, BrowserWindow, desktopCapturer, dialog, ipcMain, session } from 'electron'
import { spawn, execFileSync } from 'node:child_process'
import * as fsSync from 'node:fs'
import { promises as fs } from 'node:fs'
import path from 'node:path'
import os from 'node:os'
import { fileURLToPath } from 'node:url'
import chokidar from 'chokidar'

const here = path.dirname(fileURLToPath(import.meta.url))
let projectRoot = path.resolve(process.env.DWE_STUDIO_ROOT || path.join(here, '../../../../'))
const decompRoot = path.resolve(process.env.DWE_DECOMP || path.join(os.homedir(), 'code/dw_decomp'))
const ignoredNames = new Set(['.git', 'node_modules', 'build', 'dist', 'out', 'content/base', '.cache', 'target'])
const ignoredDecompNames = new Set(['.git', 'build', 'asm', 'disks', 'node_modules', '.claude', '.codex', '.venv', 'venv', '__pycache__', '.github', '.devcontainer'])
const sourceExtensions = new Set(['.c', '.cc', '.cpp', '.h', '.hh', '.hpp', '.lua', '.yaml', '.yml', '.json', '.md', '.cmake', '.txt', '.sh', '.py'])
const processes = new Map()
const managedSessions = new Map()
let windowRef
let watcher
const selectedCaptureSources = new Map()
let lastAICommand = null
let inputQueueBusy = false
let inputQueueTimer
let managedLogTimer
let managedLogBusy = false
let managedPersistChain = Promise.resolve()
let observerActive = false
let pendingActionObservation = null
const hasSingleInstanceLock = app.requestSingleInstanceLock()

const managedSessionPath = () => path.join(app.getPath('userData'), 'managed-games.json')
const managedLogPath = (kind, stream) => path.join(app.getPath('userData'), 'process-logs', `${kind}.${stream}.log`)
const uniqueTemporaryPath = (filePath) => `${filePath}.${process.pid}.${Date.now()}.${Math.random().toString(16).slice(2, 8)}.tmp`
if (!hasSingleInstanceLock) app.quit()

function persistManagedSessions() {
  const filePath = managedSessionPath()
  const snapshot = JSON.stringify([...managedSessions.values()], null, 2)
  managedPersistChain = managedPersistChain.catch(() => {}).then(async () => {
    await fs.mkdir(path.dirname(filePath), { recursive: true })
    const temporaryPath = uniqueTemporaryPath(filePath)
    await fs.writeFile(temporaryPath, snapshot, { mode: 0o600 })
    await fs.rename(temporaryPath, filePath)
  })
  return managedPersistChain
}

async function managedProcessIsAlive(record) {
  if (!record || !Number.isInteger(record.pid) || record.pid <= 0) return false
  try {
    process.kill(record.pid, 0)
    if (process.platform !== 'linux') return true
    const commandLine = await fs.readFile(`/proc/${record.pid}/cmdline`, 'utf8')
    const expected = record.kind === 'game' ? 'dwe-game' : 'duckstation'
    return commandLine.includes(expected)
  } catch { return false }
}

async function restoreManagedSessions() {
  let records = []
  try { records = JSON.parse(await fs.readFile(managedSessionPath(), 'utf8')) } catch {}
  for (const record of Array.isArray(records) ? records : []) {
    if (!['game', 'duckstation'].includes(record?.kind) || !(await managedProcessIsAlive(record))) continue
    managedSessions.set(record.kind, { ...record, offsets: { stdout: 0, stderr: 0 } })
    for (const stream of ['stdout', 'stderr']) {
      const logPath = managedLogPath(record.kind, stream)
      try {
        const stat = await fs.stat(logPath)
        managedSessions.get(record.kind).offsets[stream] = Math.max(0, stat.size - 20_000)
      } catch {}
    }
  }
  await persistManagedSessions()
}

async function pollManagedLogs() {
  if (managedLogBusy) return
  managedLogBusy = true
  try {
    for (const [kind, record] of managedSessions) {
      if (!(await managedProcessIsAlive(record))) {
        managedSessions.delete(kind)
        emit('process:state', { kind, running: false })
        await persistManagedSessions()
        continue
      }
      for (const stream of ['stdout', 'stderr']) {
        const logPath = managedLogPath(kind, stream)
        try {
          const stat = await fs.stat(logPath)
          let offset = record.offsets[stream] || 0
          if (offset > stat.size) offset = 0
          if (offset === stat.size) continue
          const length = Math.min(64_000, stat.size - offset)
          const buffer = Buffer.alloc(length)
          const handle = await fs.open(logPath, 'r')
          try { await handle.read(buffer, 0, length, offset) } finally { await handle.close() }
          record.offsets[stream] = offset + length
          if (windowRef && !windowRef.isDestroyed() && !windowRef.webContents.isLoading()) {
            emit('process:output', { kind, stream, text: buffer.toString('utf8') })
          }
        } catch {}
      }
    }
  } finally {
    managedLogBusy = false
  }
}

function isIgnored(relativePath) {
  const normalized = relativePath.split(path.sep).join('/')
  if (normalized === 'content/base' || normalized.startsWith('content/base/')) return true
  const parts = normalized.split('/')
  return parts.some((part) => ignoredNames.has(part)) || parts.some((part) => part.startsWith('.') && !['.github', '.agents'].includes(part))
}

function resolveProjectPath(relativePath) {
  const candidate = path.resolve(projectRoot, relativePath || '.')
  const relative = path.relative(projectRoot, candidate)
  if (relative.startsWith('..') || path.isAbsolute(relative)) throw new Error('La ruta sale del proyecto.')
  return candidate
}

async function resolveProjectFile(relativePath) {
  const candidate = resolveProjectPath(relativePath)
  const realRoot = await fs.realpath(projectRoot)
  const realCandidate = await fs.realpath(candidate)
  const relative = path.relative(realRoot, realCandidate)
  if (relative.startsWith('..') || path.isAbsolute(relative)) throw new Error('La ruta sale del proyecto.')
  return realCandidate
}

async function resolveReadOnlyReference(relativePath) {
  const candidate = path.resolve(decompRoot, relativePath || '.')
  const realRoot = await fs.realpath(decompRoot)
  const realCandidate = await fs.realpath(candidate)
  const relative = path.relative(realRoot, realCandidate)
  if (relative.startsWith('..') || path.isAbsolute(relative)) throw new Error('La ruta sale de la referencia decomp.')
  return realCandidate
}

async function listTreeAt(root, directory = '', depth = 0, ignored = ignoredNames) {
  if (depth > 5) return []
  const absolute = path.resolve(root, directory || '.')
  const relativeToRoot = path.relative(root, absolute)
  if (relativeToRoot.startsWith('..') || path.isAbsolute(relativeToRoot)) throw new Error('La ruta sale de la raíz seleccionada.')
  const entries = await fs.readdir(absolute, { withFileTypes: true })
  const output = []
  for (const entry of entries.sort((a, b) => a.isDirectory() === b.isDirectory() ? a.name.localeCompare(b.name) : a.isDirectory() ? -1 : 1)) {
    const relative = path.join(directory, entry.name)
    const normalized = relative.split(path.sep).join('/')
    if (normalized.split('/').some((part) => ignored.has(part)) || (root === projectRoot && isIgnored(normalized)) || entry.isSymbolicLink()) continue
    if (entry.isDirectory()) {
      output.push({ name: entry.name, path: normalized, type: 'directory', children: await listTreeAt(root, normalized, depth + 1, ignored) })
    } else if (sourceExtensions.has(path.extname(entry.name)) || entry.name === 'CMakeLists.txt') {
      output.push({ name: entry.name, path: normalized, type: 'file' })
    }
  }
  return output
}

function emit(channel, payload) {
  if (windowRef && !windowRef.isDestroyed()) windowRef.webContents.send(channel, payload)
}

function startProcess(kind, command, args, options = {}) {
  const existing = processes.get(kind) || managedSessions.get(kind)
  if (existing) throw new Error(`${kind} ya está en ejecución.`)
  const managed = kind === 'game' || kind === 'duckstation'
  let stdoutFd
  let stderrFd
  let stdio = [options.stdin ? 'pipe' : 'ignore', 'pipe', 'pipe']
  if (managed) {
    fsSync.mkdirSync(path.dirname(managedLogPath(kind, 'stdout')), { recursive: true })
    stdoutFd = fsSync.openSync(managedLogPath(kind, 'stdout'), 'a')
    stderrFd = fsSync.openSync(managedLogPath(kind, 'stderr'), 'a')
    stdio = ['ignore', stdoutFd, stderrFd]
  }
  let child
  try {
    child = spawn(command, args, {
      cwd: options.cwd || projectRoot,
      env: { ...process.env, ...options.env },
      stdio,
      detached: process.platform !== 'win32'
    })
  } finally {
    if (stdoutFd !== undefined) fsSync.closeSync(stdoutFd)
    if (stderrFd !== undefined) fsSync.closeSync(stderrFd)
  }
  processes.set(kind, child)
  if (managed && Number.isInteger(child.pid)) {
    const record = {
      kind,
      pid: child.pid,
      command,
      args,
      cwd: options.cwd || projectRoot,
      startedAt: new Date().toISOString(),
      offsets: { stdout: 0, stderr: 0 }
    }
    managedSessions.set(kind, record)
    void persistManagedSessions().catch((error) => emit('process:output', { kind, stream: 'stderr', text: `No se pudo guardar la sesión: ${error.message}\n` }))
  }
  emit('process:state', { kind, running: true, managed: managed, pid: child.pid })
  const forward = (stream) => (chunk) => emit('process:output', { kind, stream, text: chunk.toString() })
  child.stdout?.on('data', forward('stdout'))
  child.stderr?.on('data', forward('stderr'))
  child.on('error', (error) => emit('process:output', { kind, stream: 'stderr', text: `${error.message}\n` }))
  child.on('close', (code, signal) => {
    processes.delete(kind)
    if (managedSessions.delete(kind)) void persistManagedSessions().catch(() => {})
    emit('process:state', { kind, running: false, code, signal })
    emit('process:exit', { kind, code, signal })
  })
  return { kind, pid: child.pid }
}

const inputActionFor = (game, action) => ({
  up: 'up', down: 'down', left: 'left', right: 'right',
  confirm: game === 'duckstation' ? 'ps_confirm' : 'confirm',
  cancel: game === 'duckstation' ? 'ps_cancel' : 'cancel',
  start: game === 'duckstation' ? 'ps_start' : 'start'
}[action])

async function writeControlResult(result) {
  const resultPath = path.join(projectRoot, '.dwe-studio', 'observer', 'control', 'result.json')
  await fs.mkdir(path.dirname(resultPath), { recursive: true })
  const nextPath = uniqueTemporaryPath(resultPath)
  await fs.writeFile(nextPath, JSON.stringify(result, null, 2), { mode: 0o600 })
  await fs.rename(nextPath, resultPath)
}

async function failActionObservation(id, message) {
  if (!pendingActionObservation || pendingActionObservation.id !== id) return
  observerActive = false
  await writeControlResult({ ...pendingActionObservation, observation: {
    status: 'failed', error: message, failedAt: new Date().toISOString()
  } })
  pendingActionObservation = null
  emit('observer:status', { status: 'action-capture-error', message })
}

async function pollAIInputQueue() {
  if (!observerActive || inputQueueBusy || process.platform !== 'linux') return
  if (pendingActionObservation) {
    if (Date.now() - Date.parse(pendingActionObservation.executedAt) > 15_000) {
      await failActionObservation(pendingActionObservation.id, 'No llegó la captura posterior al control en 15 segundos. Reinicia el loop después de revisar ambas capturas.')
    }
    return
  }
  const mailbox = path.join(projectRoot, '.dwe-studio', 'observer', 'control')
  const pendingPath = path.join(mailbox, 'pending.json')
  const claimPath = `${pendingPath}.${process.pid}.claimed`
  try { await fs.rename(pendingPath, claimPath) }
  catch (error) {
    if (error.code === 'ENOENT') return
    emit('observer:status', { status: 'error', message: `No se pudo tomar la secuencia de controles: ${error.message}` })
    return
  }
  let command
  try { command = JSON.parse(await fs.readFile(claimPath, 'utf8')) }
  catch (error) {
    await fs.unlink(claimPath).catch(() => {})
    emit('observer:status', { status: 'error', message: 'Se descartó una secuencia de controles ilegible.' })
    return
  }
  inputQueueBusy = true
  const executed = []
  try {
    if (!command || typeof command.id !== 'string' || command.id.length > 100 || command.id === lastAICommand) return
    if (!Array.isArray(command.actions) || command.actions.length === 0 || command.actions.length > 12) throw new Error('El comando debe contener entre 1 y 12 acciones.')
    let budgetMs = 0
    for (const action of command.actions) {
      if (!observerActive) break
      const isText = action.type === 'text' || typeof action.text === 'string'
      if (!['duckstation', 'engine', 'both'].includes(action.target) ||
          (isText ? action.target !== 'engine' : !['up', 'down', 'left', 'right', 'confirm', 'cancel', 'start'].includes(action.button))) {
        throw new Error('El comando contiene un objetivo o botón no permitido.')
      }
      if (isText && (typeof action.text !== 'string' || !/^[A-Za-z0-9 ]{1,6}$/.test(action.text))) {
        throw new Error('El texto solo puede contener hasta 6 letras, números o espacios y solo se envía a DWE.')
      }
      const holdMs = Number(action.holdMs ?? 100)
      const delayAfterMs = Number(action.delayAfterMs ?? 100)
      if ((!isText && (!Number.isInteger(holdMs) || holdMs < 40 || holdMs > 1200)) || !Number.isInteger(delayAfterMs) || delayAfterMs < 0 || delayAfterMs > 2000) {
        throw new Error('Las duraciones de entrada exceden los límites permitidos.')
      }
      const targets = action.target === 'both' ? ['duckstation', 'engine'] : [action.target]
      budgetMs += ((isText ? action.text.length * 100 : holdMs) + delayAfterMs) * targets.length
      if (budgetMs > 20_000) throw new Error('La secuencia de controles no puede superar 20 segundos.')
      for (const game of targets) {
        const sourceId = selectedCaptureSources.get(game)
        const match = sourceId && /^window:((?:0x)?[0-9a-f]+):\d+$/i.exec(sourceId)
        if (!match) throw new Error(`Selecciona la ventana de ${game} antes de enviar controles.`)
        const helper = path.join(app.getAppPath(), 'out', 'native', 'x11-input')
        if (isText) {
          execFileSync(helper, [match[1], 'type', action.text], { timeout: 3000, stdio: 'pipe' })
          executed.push({ game, type: 'text', text: action.text })
        } else {
          execFileSync(helper, [match[1], inputActionFor(game, action.button), String(holdMs)], { timeout: 2500, stdio: 'pipe' })
          executed.push({ game, button: action.button, holdMs })
        }
        if (delayAfterMs) await new Promise((resolve) => setTimeout(resolve, delayAfterMs))
      }
    }
    lastAICommand = command.id
    pendingActionObservation = { id: command.id, executedAt: new Date().toISOString(), actions: executed, observation: { status: 'pending' } }
    await writeControlResult(pendingActionObservation)
    emit('observer:action', { id: command.id, actions: executed })
  } catch (error) {
    const result = { id: command?.id || null, error: error.message, executed, failedAt: new Date().toISOString() }
    if (executed.length) {
      pendingActionObservation = { ...result, executedAt: result.failedAt, actions: executed, observation: { status: 'pending' } }
      await writeControlResult(pendingActionObservation)
      emit('observer:action', { id: command.id, actions: executed })
    } else {
      pendingActionObservation = null
      await writeControlResult(result)
    }
    emit('observer:status', { status: 'error', message: `No se pudo ejecutar el control del juego: ${error.message}` })
  } finally {
    await fs.unlink(claimPath).catch(() => {})
    inputQueueBusy = false
  }
}

function registerIpc() {
  ipcMain.handle('project:info', async () => ({
    root: projectRoot,
    name: path.basename(projectRoot),
    platform: process.platform,
    discPath: process.env.DWE_DISC || managedSessions.get('duckstation')?.args?.[0] || null,
    decompAvailable: await fs.access(decompRoot).then(() => true, () => false),
    decompRoot
  }))
  ipcMain.handle('process:states', () => Object.fromEntries(['game', 'duckstation'].map((kind) => [kind, {
    running: processes.has(kind) || managedSessions.has(kind),
    managed: processes.has(kind) || managedSessions.has(kind),
    pid: processes.get(kind)?.pid || managedSessions.get(kind)?.pid || null
  }])))
  ipcMain.handle('project:tree', () => listTreeAt(projectRoot))
  ipcMain.handle('reference:tree', () => listTreeAt(decompRoot, '', 0, ignoredDecompNames))
  ipcMain.handle('project:read', async (_event, relativePath) => {
    const absolute = await resolveProjectFile(relativePath)
    const stat = await fs.stat(absolute)
    if (!stat.isFile() || stat.size > 2_000_000) throw new Error('El archivo no existe o supera el límite de 2 MB.')
    return { path: relativePath, content: await fs.readFile(absolute, 'utf8'), modifiedAt: stat.mtimeMs }
  })
  ipcMain.handle('reference:read', async (_event, relativePath) => {
    const absolute = await resolveReadOnlyReference(relativePath)
    const stat = await fs.stat(absolute)
    if (!stat.isFile() || stat.size > 2_000_000) throw new Error('El archivo de referencia no existe o supera el límite de 2 MB.')
    return { path: relativePath, content: await fs.readFile(absolute, 'utf8'), modifiedAt: stat.mtimeMs }
  })
  ipcMain.handle('project:write', async (_event, relativePath, content) => {
    if (typeof content !== 'string' || content.length > 2_000_000) throw new Error('El contenido supera el límite de 2 MB.')
    const absolute = await resolveProjectFile(relativePath)
    await fs.writeFile(absolute, content, 'utf8')
    return { path: relativePath, savedAt: Date.now() }
  })
  ipcMain.handle('project:choose-root', async () => {
    const result = await dialog.showOpenDialog(windowRef, { properties: ['openDirectory'] })
    if (result.canceled) return null
    const chosenRoot = result.filePaths[0]
    try { await fs.access(path.join(chosenRoot, 'scripts/dev.sh')) }
    catch { throw new Error('Selecciona la raíz de un proyecto DWE con scripts/dev.sh.') }
    projectRoot = chosenRoot
    await watcher?.close()
    startWatcher()
    return { root: projectRoot, name: path.basename(projectRoot) }
  })
  ipcMain.handle('process:build', () => startProcess('build', 'bash', ['scripts/dev.sh', 'build']))
  ipcMain.handle('process:game', async () => {
    if (managedSessions.has('game')) throw new Error('DWE Engine ya está abierto; vuelve a seleccionar su ventana para adjuntarla.')
    if (await findExistingGameWindow('game')) {
      emit('process:state', { kind: 'game', running: true, managed: false, attached: true })
      return { kind: 'game', attached: true }
    }
    return startProcess('game', 'bash', ['scripts/studio-run.sh'])
  })
  ipcMain.handle('duckstation:choose-disc', async () => {
    const result = await dialog.showOpenDialog(windowRef, {
      title: 'Seleccionar imagen de disco para DuckStation',
      properties: ['openFile'],
      filters: [{ name: 'Imágenes de PlayStation', extensions: ['cue', 'bin', 'chd', 'iso', 'img', 'pbp', 'm3u'] }, { name: 'Todos los archivos', extensions: ['*'] }]
    })
    return result.canceled ? null : result.filePaths[0]
  })
  ipcMain.handle('duckstation:launch', async (_event, discPath) => {
    if (managedSessions.has('duckstation')) throw new Error('DuckStation ya está abierto; vuelve a seleccionar su ventana para adjuntarla.')
    if (await findExistingGameWindow('duckstation')) {
      emit('process:state', { kind: 'duckstation', running: true, managed: false, attached: true })
      return { kind: 'duckstation', attached: true }
    }
    let executable
    try { executable = execFileSync('which', ['duckstation'], { encoding: 'utf8' }).trim() }
    catch { throw new Error('No se encontró DuckStation en el PATH.') }
    const args = []
    if (discPath) {
      const stat = await fs.stat(discPath)
      if (!stat.isFile()) throw new Error('La imagen seleccionada no es un archivo.')
      args.push(discPath)
    }
    return startProcess('duckstation', executable, args, { cwd: path.dirname(discPath || executable) })
  })
  ipcMain.handle('capture:sources', async (event) => {
    if (event.sender !== windowRef.webContents) throw new Error('Captura solicitada desde una ventana no autorizada.')
    const sources = await desktopCapturer.getSources({
      types: ['window'],
      thumbnailSize: { width: 240, height: 135 },
      fetchWindowIcons: false
    })
    return sources
      .filter((source) => !source.name.toLowerCase().includes('dwe studio'))
      .map((source) => ({ id: source.id, name: source.name, thumbnail: source.thumbnail.toDataURL() }))
  })
  ipcMain.handle('window:focus-game', (_event, sourceId) => {
    if (process.platform !== 'linux') throw new Error('El foco interactivo aún requiere X11 en Linux.')
    if (typeof sourceId !== 'string') throw new Error('Selecciona una ventana de juego.')
    const match = /^window:((?:0x)?[0-9a-f]+):\d+$/i.exec(sourceId)
    if (!match) throw new Error('Esta fuente no expone un identificador de ventana X11.')
    const helper = path.join(app.getAppPath(), 'out', 'native', 'x11-focus')
    try {
      execFileSync(helper, [match[1]], { timeout: 1000, stdio: 'pipe' })
      return true
    } catch (error) {
      throw new Error(error.stderr?.toString().trim() || error.message)
    }
  })
  ipcMain.handle('capture:select', (event, kind, sourceId) => {
    if (event.sender !== windowRef.webContents) throw new Error('Selección de captura solicitada desde una ventana no autorizada.')
    if (!['duckstation', 'engine'].includes(kind)) throw new Error('Tipo de ventana no válido.')
    if (typeof sourceId !== 'string' || !sourceId) { selectedCaptureSources.delete(kind); return true }
    if (!/^window:((?:0x)?[0-9a-f]+):\d+$/i.test(sourceId)) throw new Error('La ventana seleccionada no tiene un ID X11 válido.')
    selectedCaptureSources.set(kind, sourceId)
    return true
  })
  ipcMain.handle('observer:active', async (event, active) => {
    if (event.sender !== windowRef.webContents) throw new Error('Control del loop solicitado desde una ventana no autorizada.')
    observerActive = active === true
    if (!observerActive && pendingActionObservation) await failActionObservation(pendingActionObservation.id, 'El loop se detuvo antes de guardar la captura posterior al control.')
    if (!observerActive) await fs.unlink(path.join(projectRoot, '.dwe-studio', 'observer', 'control', 'pending.json')).catch(() => {})
    return observerActive
  })
  ipcMain.handle('observer:action-capture-failed', async (event, id, message) => {
    if (event.sender !== windowRef.webContents) throw new Error('Error de captura solicitado desde una ventana no autorizada.')
    await failActionObservation(id, typeof message === 'string' ? message.slice(0, 1000) : 'Falló la captura posterior al control.')
  })
  ipcMain.handle('observer:runtime', async (event, state) => {
    if (event.sender !== windowRef.webContents) throw new Error('Estado del observador solicitado desde una ventana no autorizada.')
    if (!state || typeof state !== 'object') throw new Error('El estado del observador no es válido.')
    const mailbox = path.join(projectRoot, '.dwe-studio', 'observer')
    await fs.mkdir(mailbox, { recursive: true })
    const runtimePath = path.join(mailbox, 'runtime.json')
    const nextPath = uniqueTemporaryPath(runtimePath)
    let previousRuntime = {}
    try { previousRuntime = JSON.parse(await fs.readFile(runtimePath, 'utf8')) } catch {}
    const runtime = {
      updatedAt: new Date().toISOString(),
      status: typeof state.status === 'string' ? state.status.slice(0, 80) : 'unknown',
      error: typeof state.error === 'string' ? state.error.slice(0, 1000) : null,
      availableSources: Array.isArray(state.availableSources) ? state.availableSources.filter((name) => typeof name === 'string').slice(0, 20).map((name) => name.slice(0, 200)) : [],
      selectedSources: state.selectedSources && typeof state.selectedSources === 'object' ? {
        duckstation: typeof state.selectedSources.duckstation === 'string' ? state.selectedSources.duckstation.slice(0, 200) : '',
        engine: typeof state.selectedSources.engine === 'string' ? state.selectedSources.engine.slice(0, 200) : ''
      } : {},
      captures: state.captures && typeof state.captures === 'object' ? {
        duckstation: state.captures.duckstation === true,
        engine: state.captures.engine === true
      } : {},
      observing: state.observing === true,
      lastSampleId: typeof state.lastSampleId === 'string' ? state.lastSampleId.slice(0, 100) : previousRuntime.lastSampleId || null
    }
    await fs.writeFile(nextPath, JSON.stringify(runtime, null, 2), { mode: 0o600 })
    await fs.rename(nextPath, runtimePath)
    return runtime
  })
  ipcMain.handle('process:terminal', (_event, command) => {
    if (typeof command !== 'string' || !command.trim()) throw new Error('Escribe un comando.')
    if (command.length > 16_000) throw new Error('El comando supera el límite de 16 KB.')
    let child = processes.get('terminal')
    if (!child) {
      startProcess('terminal', 'bash', ['--noprofile', '--norc'], { stdin: true })
      child = processes.get('terminal')
    }
    child.stdin.write(`${command}\n`)
    return { kind: 'terminal', pid: child.pid }
  })
  ipcMain.handle('observer:observe', async (event, payload) => {
    if (event.sender !== windowRef.webContents) throw new Error('Observación solicitada desde una ventana no autorizada.')
    if (!payload || typeof payload !== 'object') throw new Error('Faltan capturas para analizar.')
    const action = payload.actionId == null ? null : pendingActionObservation
    if (payload.actionId != null && (!action || payload.actionId !== action.id)) throw new Error('La captura no corresponde al control pendiente.')
    const motion = payload.motion === true
    if (action && motion) throw new Error('La captura posterior al control y la de movimiento son muestras separadas.')
    const frameLimit = motion ? 40 : 3
    const byteLimit = motion ? 16_000_000 : 8_000_000
    const images = {}
    let totalImageBytes = 0
    for (const kind of ['duckstation', 'engine']) {
      const frames = Array.isArray(payload[kind]) ? payload[kind] : payload[kind] ? [payload[kind]] : []
      if (frames.length > frameLimit) throw new Error(`La muestra de ${kind} excede los ${frameLimit} fotogramas permitidos.`)
      images[kind] = []
      for (const dataUrl of frames) {
        if (typeof dataUrl !== 'string' || dataUrl.length > 1_500_000 || !/^data:image\/jpeg;base64,[A-Za-z0-9+/]+=*$/.test(dataUrl)) {
          throw new Error(`La captura de ${kind} no es un JPEG válido o supera el límite.`)
        }
        const image = Buffer.from(dataUrl.slice(dataUrl.indexOf(',') + 1), 'base64')
        totalImageBytes += image.length
        if (totalImageBytes > byteLimit) throw new Error('La muestra de capturas supera el límite total de tamaño.')
        images[kind].push(image)
      }
    }
    if (motion && (images.duckstation.length !== frameLimit || images.engine.length !== frameLimit)) throw new Error('La captura de movimiento necesita 40 fotogramas de ambos juegos.')
    if (action && (images.duckstation.length !== 3 || images.engine.length !== 3)) throw new Error('El control necesita tres fotogramas nuevos de ambos juegos antes de continuar.')
    if (!images.duckstation.length && !images.engine.length) throw new Error('Captura al menos una de las dos ventanas antes de observar.')
    let comparison = null
    if (payload.comparison != null) {
      if (typeof payload.comparison !== 'string' || payload.comparison.length > 3_000_000 || !/^data:image\/jpeg;base64,[A-Za-z0-9+/]+=*$/.test(payload.comparison)) throw new Error('La comparación no es un JPEG válido o supera el límite.')
      comparison = Buffer.from(payload.comparison.slice(payload.comparison.indexOf(',') + 1), 'base64')
      totalImageBytes += comparison.length
    }
    const motionSheets = {}
    if (motion) {
      for (const kind of ['duckstation', 'engine']) {
        const value = payload.motionSheets?.[kind]
        if (typeof value !== 'string' || value.length > 3_000_000 || !/^data:image\/jpeg;base64,[A-Za-z0-9+/]+=*$/.test(value)) throw new Error(`La secuencia visual de ${kind} no es un JPEG válido o supera el límite.`)
        motionSheets[kind] = Buffer.from(value.slice(value.indexOf(',') + 1), 'base64')
        totalImageBytes += motionSheets[kind].length
      }
    }
    if (totalImageBytes > byteLimit) throw new Error('La muestra de capturas supera el límite total de tamaño.')
    const logs = typeof payload.logs === 'string' ? payload.logs.slice(-20_000) : ''
    const mailbox = path.join(projectRoot, '.dwe-studio', 'observer')
    const samplesPath = path.join(mailbox, 'samples')
    await fs.mkdir(samplesPath, { recursive: true })
    const id = `${Date.now()}-${Math.random().toString(16).slice(2, 8)}`
    const samplePath = path.join(samplesPath, id)
    await fs.mkdir(samplePath, { mode: 0o700 })
    const files = {}
    for (const kind of ['duckstation', 'engine']) {
      files[kind] = []
      for (const [index, image] of images[kind].entries()) {
        const filename = `${kind}-${index + 1}.jpg`
        await fs.writeFile(path.join(samplePath, filename), image, { mode: 0o600 })
        files[kind].push(filename)
      }
    }
    if (comparison) {
      files.comparison = 'comparison.jpg'
      await fs.writeFile(path.join(samplePath, files.comparison), comparison, { mode: 0o600 })
    }
    if (motion) {
      files.motion = {}
      for (const kind of ['duckstation', 'engine']) {
        files.motion[kind] = `motion-${kind}.jpg`
        await fs.writeFile(path.join(samplePath, files.motion[kind]), motionSheets[kind], { mode: 0o600 })
      }
    }
    const sample = {
      id,
      createdAt: new Date().toISOString(),
      projectRoot,
      sourceNames: payload.sourceNames || {},
      comparison: { reference: 'duckstation', remake: 'engine' },
      mode: action ? 'after-action' : motion ? 'motion' : 'observation',
      ...(action ? { action: { id: action.id, executedAt: action.executedAt, actions: action.actions } } : {}),
      frameTimes: Object.fromEntries(['duckstation', 'engine'].map((kind) => [kind,
        Array.isArray(payload.frameTimes?.[kind]) ? payload.frameTimes[kind].slice(0, frameLimit).filter((time) => typeof time === 'string' && Number.isFinite(Date.parse(time))).map((time) => new Date(time).toISOString()) : []
      ])),
      running: {
        engine: processes.has('game') || managedSessions.has('game') || selectedCaptureSources.has('engine'),
        duckstation: processes.has('duckstation') || managedSessions.has('duckstation') || selectedCaptureSources.has('duckstation')
      },
      files,
      logs
    }
    await fs.writeFile(path.join(samplePath, 'sample.json'), JSON.stringify(sample, null, 2), { mode: 0o600 })
    const manifestPath = path.join(mailbox, 'manifest.json')
    let manifest = { samples: [] }
    try { manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8')) } catch {}
    const previousSamples = Array.isArray(manifest.samples)
      ? manifest.samples.filter((entry) => typeof entry?.id === 'string' && /^\d{13}-[a-f0-9]{6}$/.test(entry.id))
      : []
    const retained = [...previousSamples.filter((entry) => entry.id !== id), { id, createdAt: sample.createdAt, ...(action ? { actionId: action.id } : {}) }].slice(-30)
    const nextManifest = uniqueTemporaryPath(manifestPath)
    await fs.writeFile(nextManifest, JSON.stringify({ samples: retained }, null, 2), { mode: 0o600 })
    await fs.rename(nextManifest, manifestPath)
    const runtimePath = path.join(mailbox, 'runtime.json')
    let runtime = {}
    try { runtime = JSON.parse(await fs.readFile(runtimePath, 'utf8')) } catch {}
    const nextRuntimePath = uniqueTemporaryPath(runtimePath)
    await fs.writeFile(nextRuntimePath, JSON.stringify({ ...runtime, updatedAt: sample.createdAt, status: 'captured', error: null, lastSampleId: id }, null, 2), { mode: 0o600 })
    await fs.rename(nextRuntimePath, runtimePath)
    if (action && pendingActionObservation?.id === action.id) {
      await writeControlResult({ ...action, observation: { status: 'captured', sampleId: id, createdAt: sample.createdAt } })
      pendingActionObservation = null
    }
    // Reviewed evidence and action results outlive the rolling capture window.
    // A malformed review ledger must never cause evidence to be deleted.
    let reviewed = { processed: [] }
    try { reviewed = JSON.parse(await fs.readFile(path.join(mailbox, 'processed.json'), 'utf8')) }
    catch (error) { if (error.code !== 'ENOENT') throw error }
    if (!Array.isArray(reviewed.processed)) throw new Error('processed.json debe contener una lista processed; se conserva la evidencia sin limpiar.')
    const reviewedIds = new Set(reviewed.processed.map((entry) => entry.id))
    for (const old of previousSamples) {
      if (retained.some((entry) => entry.id === old.id)) continue
      const oldPath = path.join(samplesPath, old.id)
      if (old.actionId || reviewedIds.has(old.id)) {
        const historyPath = path.join(mailbox, 'history')
        await fs.mkdir(historyPath, { recursive: true })
        try { await fs.rename(oldPath, path.join(historyPath, old.id)) }
        catch (error) { if (error.code !== 'ENOENT') throw error }
      } else {
        await fs.rm(oldPath, { recursive: true, force: true })
      }
    }
    emit('observer:sample', { id, createdAt: sample.createdAt })
    return { id, createdAt: sample.createdAt }
  })
  ipcMain.handle('process:stop', async (_event, kind) => {
    const child = processes.get(kind)
    const managed = managedSessions.get(kind)
    if (!child && !managed) return false
    const pid = child?.pid || managed?.pid
    if (managed && !child && !(await managedProcessIsAlive(managed))) {
      managedSessions.delete(kind)
      await persistManagedSessions().catch(() => {})
      return false
    }
    try {
      if (process.platform === 'win32') child ? child.kill('SIGTERM') : process.kill(pid, 'SIGTERM')
      else process.kill(-pid, 'SIGTERM')
    } catch {
      child?.kill('SIGTERM')
    }
    managedSessions.delete(kind)
    await persistManagedSessions().catch(() => {})
    return true
  })
  ipcMain.handle('window:control', (_event, action) => {
    if (action === 'minimize') windowRef.minimize()
    else if (action === 'toggle-maximize') windowRef.isMaximized() ? windowRef.unmaximize() : windowRef.maximize()
    else if (action === 'close') windowRef.close()
    return true
  })
  ipcMain.handle('agent:availability', () => {
    const available = (command) => {
      try {
        execFileSync('which', [command], { stdio: 'ignore' })
        return true
      } catch { return false }
    }
    return { codexCli: available('codex'), claudeCode: available('claude') }
  })
}

function createWindow() {
  windowRef = new BrowserWindow({
    width: 1500,
    height: 960,
    minWidth: 980,
    minHeight: 640,
    title: 'DWE Studio',
    backgroundColor: '#111318',
    titleBarStyle: 'hidden',
    trafficLightPosition: { x: 14, y: 14 },
    webPreferences: {
      preload: path.join(here, '../preload/index.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true
    }
  })
  windowRef.webContents.setWindowOpenHandler(() => ({ action: 'deny' }))
  if (process.env.ELECTRON_RENDERER_URL) windowRef.loadURL(process.env.ELECTRON_RENDERER_URL)
  else windowRef.loadFile(path.join(here, '../renderer/index.html'))
}

function startWatcher() {
  watcher = chokidar.watch(projectRoot, {
    ignored: (target) => {
      const relative = path.relative(projectRoot, target)
      return relative ? isIgnored(relative) : false
    },
    ignoreInitial: true,
    ignorePermissionErrors: true,
    awaitWriteFinish: { stabilityThreshold: 180, pollInterval: 60 },
    followSymlinks: false
  })
  for (const event of ['add', 'change', 'unlink']) watcher.on(event, (target) => {
    const relative = path.relative(projectRoot, target).split(path.sep).join('/')
    emit('project:changed', { event, path: relative, time: Date.now() })
  })
}

async function findExistingGameWindow(kind) {
  const sources = await desktopCapturer.getSources({ types: ['window'], thumbnailSize: { width: 0, height: 0 }, fetchWindowIcons: false })
  const engine = sources.filter((source) => /\b(dwe(?:-game)?|dw1)\b/i.test(source.name))
  const duckstation = sources.filter((source) => /duckstation|psx|playstation|\b(?:SLUS|SCUS|SLES|SCES)[-_ ]?\d{3,}/i.test(source.name))
  const worldTitle = sources.filter((source) => /digimon world/i.test(source.name))
  if (kind === 'game') return engine[0] || (!duckstation.length ? worldTitle[0] : null) || null
  return duckstation[0] || (engine.length ? worldTitle[0] : null) || null
}

if (hasSingleInstanceLock) {
  app.on('second-instance', () => {
    if (!windowRef || windowRef.isDestroyed()) return
    if (windowRef.isMinimized()) windowRef.restore()
    windowRef.show()
    windowRef.focus()
  })
}

if (hasSingleInstanceLock) app.whenReady().then(async () => {
  await fs.unlink(path.join(projectRoot, '.dwe-studio', 'observer', 'control', 'pending.json')).catch(() => {})
  await restoreManagedSessions()
  registerIpc()
  session.defaultSession.setPermissionRequestHandler((webContents, permission, callback) => {
    const trustedWindow = windowRef && webContents === windowRef.webContents
    callback(trustedWindow && permission === 'display-capture')
  })
  session.defaultSession.setDisplayMediaRequestHandler(async (_request, callback) => {
    try {
      const sources = await desktopCapturer.getSources({ types: ['window', 'screen'], thumbnailSize: { width: 240, height: 135 } })
      callback({ video: sources[0] || null })
    } catch {
      callback(null)
    }
  }, { useSystemPicker: true })
  startWatcher()
  inputQueueTimer = setInterval(() => void pollAIInputQueue(), 350)
  managedLogTimer = setInterval(() => void pollManagedLogs(), 250)
  createWindow()
})

app.on('window-all-closed', () => {
  clearInterval(inputQueueTimer)
  clearInterval(managedLogTimer)
  watcher?.close()
  for (const [kind, child] of processes) {
    if (kind === 'game' || kind === 'duckstation') continue
    try { process.platform === 'win32' ? child.kill('SIGTERM') : process.kill(-child.pid, 'SIGTERM') }
    catch { child.kill('SIGTERM') }
  }
  if (process.platform !== 'darwin') app.quit()
})

app.on('activate', () => { if (BrowserWindow.getAllWindows().length === 0) createWindow() })
