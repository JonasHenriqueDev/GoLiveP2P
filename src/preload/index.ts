import { contextBridge, ipcRenderer } from 'electron';
import {
  nativeEvent,
  nativeRequests,
  type NativeData,
  type NativeEvent,
  type NativeMethod,
} from '../shared/native-media';
const api = {
  platform: process.platform,
  mediaRequest: <M extends NativeMethod>(method: M, data: NativeData<M>) =>
    ipcRenderer.invoke(
      'media:request',
      method,
      nativeRequests[method].parse(data),
    ),
  onMediaEvent: (callback: (event: NativeEvent) => void) => {
    const listener = (_event: Electron.IpcRendererEvent, message: unknown) => {
      const parsed = nativeEvent.safeParse(message);
      if (parsed.success) callback(parsed.data);
    };
    ipcRenderer.on('media:event', listener);
    return () => ipcRenderer.removeListener('media:event', listener);
  },
  systemInfo: () => ipcRenderer.invoke('system:info'),
  updateState: () => ipcRenderer.invoke('update:state'),
  installUpdate: () => ipcRenderer.invoke('update:install'),
  setSessionActive: (active: boolean) =>
    ipcRenderer.send('update:session-active', active),
  onUpdateState: (callback: (state: unknown) => void) => {
    const listener = (_event: Electron.IpcRendererEvent, state: unknown) =>
      callback(state);
    ipcRenderer.on('update:state', listener);
    return () => ipcRenderer.removeListener('update:state', listener);
  },
  tailscaleStatus: () => ipcRenderer.invoke('tailscale:status'),
  discoverHosts: () => ipcRenderer.invoke('tailscale:discover'),
  pingPeer: (ip: string) => ipcRenderer.invoke('tailscale:ping', ip),
  audioSupport: () => ipcRenderer.invoke('audio:support'),
  startAudioCapture: () => ipcRenderer.invoke('audio:start'),
  stopAudioCapture: () => ipcRenderer.invoke('audio:stop'),
  onAudioChunk: (callback: (chunk: Uint8Array) => void) => {
    const listener = (_event: Electron.IpcRendererEvent, chunk: Uint8Array) =>
      callback(chunk);
    ipcRenderer.on('audio:chunk', listener);
    return () => ipcRenderer.removeListener('audio:chunk', listener);
  },
  onAudioError: (callback: (message: string) => void) => {
    const listener = (_event: Electron.IpcRendererEvent, message: string) =>
      callback(message);
    ipcRenderer.on('audio:error', listener);
    return () => ipcRenderer.removeListener('audio:error', listener);
  },
  getLogReport: () => ipcRenderer.invoke('logs:report'),
  saveLogReport: (text: string, name?: string) =>
    ipcRenderer.invoke('logs:save', text, name),
  log: (level: 'info' | 'warn' | 'error', scope: string, message: string) =>
    ipcRenderer.send('logs:event', level, scope, message),
  createRoom: () => ipcRenderer.invoke('room:create'),
  closeRoom: () => ipcRenderer.invoke('room:close'),
  listSources: () => ipcRenderer.invoke('sources:list'),
  selectSource: (id: string) => ipcRenderer.invoke('sources:select', id),
};
contextBridge.exposeInMainWorld('golive', api);
