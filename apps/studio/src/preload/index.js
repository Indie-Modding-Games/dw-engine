import { contextBridge, ipcRenderer } from 'electron'

const listen = (channel, callback) => {
  const handler = (_event, payload) => callback(payload)
  ipcRenderer.on(channel, handler)
  return () => ipcRenderer.removeListener(channel, handler)
}

contextBridge.exposeInMainWorld('dweStudio', {
  info: () => ipcRenderer.invoke('project:info'),
  tree: () => ipcRenderer.invoke('project:tree'),
  referenceTree: () => ipcRenderer.invoke('reference:tree'),
  readReference: (path) => ipcRenderer.invoke('reference:read', path),
  read: (path) => ipcRenderer.invoke('project:read', path),
  write: (path, content) => ipcRenderer.invoke('project:write', path, content),
  chooseRoot: () => ipcRenderer.invoke('project:choose-root'),
  build: () => ipcRenderer.invoke('process:build'),
  runGame: () => ipcRenderer.invoke('process:game'),
  chooseDuckstationDisc: () => ipcRenderer.invoke('duckstation:choose-disc'),
  launchDuckstation: (discPath) => ipcRenderer.invoke('duckstation:launch', discPath),
  captureSources: () => ipcRenderer.invoke('capture:sources'),
  setCaptureSource: (kind, sourceId) => ipcRenderer.invoke('capture:select', kind, sourceId),
  focusGameWindow: (sourceId) => ipcRenderer.invoke('window:focus-game', sourceId),
  observeGames: (payload) => ipcRenderer.invoke('observer:observe', payload),
  failActionObservation: (id, message) => ipcRenderer.invoke('observer:action-capture-failed', id, message),
  setObserverActive: (active) => ipcRenderer.invoke('observer:active', active),
  reportObserverRuntime: (state) => ipcRenderer.invoke('observer:runtime', state),
  runCommand: (command) => ipcRenderer.invoke('process:terminal', command),
  stop: (kind) => ipcRenderer.invoke('process:stop', kind),
  processStates: () => ipcRenderer.invoke('process:states'),
  windowControl: (action) => ipcRenderer.invoke('window:control', action),
  agents: () => ipcRenderer.invoke('agent:availability'),
  onOutput: (callback) => listen('process:output', callback),
  onProcessState: (callback) => listen('process:state', callback),
  onObserverStatus: (callback) => listen('observer:status', callback),
  onObserverSample: (callback) => listen('observer:sample', callback),
  onObserverAction: (callback) => listen('observer:action', callback),
  onFileChange: (callback) => listen('project:changed', callback)
})
