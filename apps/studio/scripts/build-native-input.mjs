import { spawnSync } from 'node:child_process'
import { mkdirSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

if (process.platform !== 'linux') process.exit(0)

const appRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const outputFolder = path.join(appRoot, 'out', 'native')
mkdirSync(outputFolder, { recursive: true })
const result = spawnSync('cc', [
  path.join(appRoot, 'src/main/x11_focus.c'),
  '-O2',
  '-std=c11',
  '-o',
  path.join(outputFolder, 'x11-focus'),
  '-lX11'
], { stdio: 'inherit' })

if (result.error) throw result.error
if (result.status !== 0) process.exit(result.status ?? 1)

const inputResult = spawnSync('cc', [
  path.join(appRoot, 'src/main/x11_input.c'),
  '-O2',
  '-std=c11',
  '-o',
  path.join(outputFolder, 'x11-input'),
  '-lX11',
  '-Wl,-l:libXtst.so.6'
], { stdio: 'inherit' })

if (inputResult.error) throw inputResult.error
if (inputResult.status !== 0) process.exit(inputResult.status ?? 1)
