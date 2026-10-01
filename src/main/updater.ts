import { app, BrowserWindow, ipcMain } from 'electron';
import updater from 'electron-updater';
const { autoUpdater } = updater;

export type UpdateState = {
  status: 'disabled' | 'checking' | 'available' | 'downloading' | 'ready' | 'up-to-date' | 'error';
  version?: string;
  percent?: number;
  message?: string;
};

let state: UpdateState = { status: 'disabled' };
let sessionActive = false;
let downloaded = false;
let restartScheduled = false;

export function setupUpdater(win: BrowserWindow) {
  const publish = (next: UpdateState) => {
    state = next;
    if (!win.isDestroyed()) win.webContents.send('update:state', state);
    console.info('[Update]', next.status, next.version || '', next.percent == null ? '' : Math.round(next.percent) + '%');
  };
  const restartIfIdle = () => {
    if (!downloaded || sessionActive || restartScheduled) return;
    restartScheduled = true;
    setTimeout(() => {
      restartScheduled = false;
      if (downloaded && !sessionActive) autoUpdater.quitAndInstall(false, true);
    }, 1500);
  };

  ipcMain.handle('update:state', () => state);
  ipcMain.handle('update:install', event => {
    if (!BrowserWindow.fromWebContents(event.sender) || !downloaded) return false;
    autoUpdater.quitAndInstall(false, true);
    return true;
  });
  ipcMain.on('update:session-active', (event, value: unknown) => {
    if (!BrowserWindow.fromWebContents(event.sender) || typeof value !== 'boolean') return;
    sessionActive = value;
    if (!value) restartIfIdle();
  });

  if (!app.isPackaged || process.platform !== 'win32') return;
  autoUpdater.autoDownload = true;
  autoUpdater.autoInstallOnAppQuit = true;
  autoUpdater.allowDowngrade = false;
  autoUpdater.logger = console;
  autoUpdater.on('checking-for-update', () => publish({ status: 'checking' }));
  autoUpdater.on('update-available', info => publish({ status: 'available', version: info.version }));
  autoUpdater.on('update-not-available', () => publish({ status: 'up-to-date' }));
  autoUpdater.on('download-progress', progress => publish({ status: 'downloading', percent: progress.percent }));
  autoUpdater.on('update-downloaded', info => {
    downloaded = true;
    publish({ status: 'ready', version: info.version });
    restartIfIdle();
  });
  autoUpdater.on('error', error => {
    console.error('[Update] failed', error);
    publish({ status: 'error', message: error.message });
  });
  setTimeout(() => { void autoUpdater.checkForUpdates().catch(error => console.error('[Update] check failed', error)); }, 2000);
}
