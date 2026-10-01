import { app, BrowserWindow } from 'electron';
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { release } from 'node:os';
import { join } from 'node:path';

let capture: ReturnType<typeof spawn> | null = null;
const intentionalStops = new WeakSet<object>();
function helperPath() {
  return app.isPackaged ? join(process.resourcesPath, 'audio-capture.exe')
    : join(app.getAppPath(), 'native', 'windows', 'bin', 'audio-capture.exe');
}
export function audioSupport(): { available: boolean; message: string } {
  if (process.platform !== 'win32') return { available: false, message: 'Áudio filtrado por aplicativo disponível somente no Windows compatível.' };
  const build = Number(release().split('.')[2] || 0);
  if (build < 20348) return { available: false, message: 'Este Windows não oferece a API de áudio por processo. O áudio foi desativado para proteger o Discord.' };
  if (!existsSync(helperPath())) return { available: false, message: 'Módulo nativo de áudio não encontrado.' };
  return { available: true, message: 'Áudio da janela por processo disponível em caráter experimental; requer validação em Windows real.' };
}
export function stopAudioCapture() {
  if (capture) {
    intentionalStops.add(capture);
    capture.kill();
  }
  capture = null;
}
export async function startAudioCapture(sourceId: string, window: BrowserWindow): Promise<void> {
  const support = audioSupport();
  if (!support.available) throw new Error(support.message);
  stopAudioCapture();
  let args: string[];
  if (sourceId.startsWith('screen:')) throw new Error('Áudio de monitor bloqueado: mistura por aplicativo ainda não validada');
  else {
    const match = /^window:(\d+):\d+$/.exec(sourceId);
    if (!match) throw new Error('Janela sem identificador de processo válido');
    args = ['window', match[1]];
  }
  const child = spawn(helperPath(), args, { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  capture = child;
  await new Promise<void>((resolve, reject) => {
    let ready = false;
    let stderr = '';
    const timeout = setTimeout(() => { child.kill(); reject(new Error('Tempo esgotado ao iniciar áudio')); }, 10000);
    child.stdout.on('data', (chunk: Buffer) => {
      if (ready && !window.isDestroyed()) window.webContents.send('audio:chunk', chunk);
    });
    child.stderr.on('data', (chunk: Buffer) => {
      stderr += chunk.toString('utf8');
      if (stderr.includes('READY') && !ready) {
        ready = true;
        clearTimeout(timeout);
        console.info('[Audio] process capture started', sourceId.startsWith('screen:') ? 'screen' : 'window');
        resolve();
      }
      if (stderr.length > 1000) stderr = stderr.slice(-1000);
    });
    child.on('error', error => {
      clearTimeout(timeout);
      if (!ready) reject(error);
      else if (!intentionalStops.has(child) && !window.isDestroyed()) window.webContents.send('audio:error', 'Captura de áudio encerrada: ' + error.message);
    });
    child.on('exit', code => {
      clearTimeout(timeout);
      if (capture === child) capture = null;
      const message = stderr.trim().split('\n').filter(line => !line.includes('READY')).join(' ').trim() || 'código ' + code;
      if (!ready) reject(new Error('Áudio indisponível: ' + message));
      else if (!intentionalStops.has(child) && !window.isDestroyed()) {
        console.warn('[Audio] process capture stopped', message);
        window.webContents.send('audio:error', 'Captura de áudio encerrada: ' + message);
      }
    });
  });
}
