import {
  app,
  BrowserWindow,
  desktopCapturer,
  ipcMain,
  Menu,
  session,
} from 'electron';
import { join } from 'node:path';
import { RoomServer } from '../services/signaling/room';
import { getTailscaleStatus } from '../services/tailscale/status';
import { discoverHosts } from '../services/tailscale/discovery';
import { PORT } from '../shared/protocol';
import { pingPeer } from '../services/tailscale/ping';
import { audioSupport, startAudioCapture, stopAudioCapture } from './audio';
import { record, reportText, saveReport, setupLogs, systemInfo } from './logs';
import { setupUpdater } from './updater';
import { NativeEngine } from './native-engine';
import { nativeRequests, type NativeMethod } from '../shared/native-media';
import { z } from 'zod';
app.commandLine.appendSwitch('disable-features', 'WebRtcHideLocalIpsWithMdns');
let room: RoomServer | null = null;
let selectedSource: string | null = null;
let engine: NativeEngine | null = null;
function createWindow() {
  const win = new BrowserWindow({
    width: 1120,
    height: 780,
    minWidth: 640,
    minHeight: 480,
    webPreferences: {
      preload: join(__dirname, '../preload/index.mjs'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false,
    },
  });
  if (process.env.ELECTRON_RENDERER_URL)
    win.loadURL(process.env.ELECTRON_RENDERER_URL);
  else win.loadFile(join(__dirname, '../renderer/index.html'));
  return win;
}
app.whenReady().then(() => {
  setupLogs();
  Menu.setApplicationMenu(null);
  if (process.platform === 'win32')
    engine = new NativeEngine(
      app.isPackaged
        ? join(process.resourcesPath, 'native-media')
        : join(app.getAppPath(), 'native/windows/runtime'),
      app.getPath('userData'),
    );
  ipcMain.handle(
    'media:request',
    async (event, method: unknown, data: unknown) => {
      const window = BrowserWindow.fromWebContents(event.sender);
      if (!window || !engine)
        throw new Error('Motor nativo Windows indisponível');
      const key = z
        .enum([
          'capabilities',
          'sources',
          'start',
          'offer',
          'signal',
          'remove',
          'stop',
          'bitrate',
          'stats',
          'audio-sessions',
        ])
        .parse(method) as NativeMethod;
      const parsed = nativeRequests[key].parse(data);
      engine.onEvent = (message) => {
        if (message.event === 'error' || message.event === 'warning')
          console.warn('[Native media]', message);
        if (!window.isDestroyed())
          window.webContents.send('media:event', message);
      };
      if (key === 'stop') {
        stopAudioCapture();
      }
      const result = await engine.request(key, parsed);
      if (key === 'start') {
        const config = nativeRequests.start.parse(parsed);
        stopAudioCapture();
        console.info(
          '[Native] requested source',
          config.source,
          config.method,
          'audio',
          config.audio,
        );
      }
      return result;
    },
  );
  session.defaultSession.setDisplayMediaRequestHandler(
    async (_request, callback) => {
      const sources = await desktopCapturer.getSources({
        types: ['screen', 'window'],
      });
      const source = sources.find((s) => s.id === selectedSource);
      if (!source) {
        console.warn('[Capture] selected source unavailable');
        return;
      }
      // Chromium's system loopback can include Discord. Audio is captured by the filtered native helper.
      callback({ video: source });
    },
  );
  ipcMain.handle('tailscale:status', getTailscaleStatus);
  ipcMain.handle('system:info', systemInfo);
  ipcMain.handle('tailscale:discover', discoverHosts);
  ipcMain.handle('tailscale:ping', (_event, ip: string) => pingPeer(ip));
  ipcMain.handle('audio:support', audioSupport);
  ipcMain.handle('audio:start', async (event) => {
    const window = BrowserWindow.fromWebContents(event.sender);
    if (!window || !selectedSource)
      throw new Error('Fonte de tela indisponível');
    const kind = selectedSource.startsWith('screen:') ? 'monitor' : 'janela';
    console.info('[Audio] capture requested', kind, systemInfo().release);
    try {
      await startAudioCapture(selectedSource, window);
    } catch (error) {
      console.error('[Audio] capture failed', error);
      throw error;
    }
  });
  ipcMain.handle('audio:stop', stopAudioCapture);
  ipcMain.handle('logs:report', reportText);
  ipcMain.handle('logs:save', (_event, text: string, name?: string) =>
    saveReport(text, name),
  );
  ipcMain.on(
    'logs:event',
    (_event, level: unknown, scope: unknown, message: unknown) => {
      if (
        (level === 'info' || level === 'warn' || level === 'error') &&
        typeof scope === 'string' &&
        typeof message === 'string'
      )
        record(level, scope.slice(0, 40), message.slice(0, 4000));
    },
  );
  ipcMain.handle('room:create', async () => {
    const status = await getTailscaleStatus();
    if (!status.connected || !status.ip) throw new Error(status.message);
    room?.close();
    const next = new RoomServer(status.ip, PORT);
    await next.ready;
    room = next;
    console.info('[Signaling] listening', status.ip, PORT);
    return { ip: status.ip, port: PORT };
  });
  ipcMain.handle('room:close', () => {
    room?.close();
    room = null;
  });
  ipcMain.handle('sources:list', async () => {
    if (engine) return engine.request('sources', {});
    const sources = await desktopCapturer.getSources({
      types: ['screen', 'window'],
      thumbnailSize: { width: 300, height: 180 },
    });
    return sources.map((s) => ({
      id: s.id,
      name: s.name,
      kind: s.id.startsWith('screen:') ? 'screen' : 'window',
      thumbnail: s.thumbnail.toDataURL(),
    }));
  });
  ipcMain.handle('sources:select', async (_event, id: unknown) => {
    if (typeof id !== 'string' || id.length > 160)
      throw new Error('Fonte de tela inválida');
    const sources = await desktopCapturer.getSources({
      types: ['screen', 'window'],
    });
    if (!sources.some((source) => source.id === id))
      throw new Error('Fonte de tela indisponível');
    selectedSource = id;
    console.info(
      '[Capture] selected source',
      id.startsWith('screen:') ? 'monitor' : 'janela',
      id,
    );
  });
  setupUpdater(createWindow());
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});
app.on('before-quit', () => {
  stopAudioCapture();
  engine?.shutdown();
  room?.close();
});
app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit();
});
