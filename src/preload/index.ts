import { contextBridge, ipcRenderer } from 'electron';
const api={
 platform:process.platform,
 tailscaleStatus:()=>ipcRenderer.invoke('tailscale:status'),
 discoverHosts:()=>ipcRenderer.invoke('tailscale:discover'),
 pingPeer:(ip:string)=>ipcRenderer.invoke('tailscale:ping',ip),
 audioSupport:()=>ipcRenderer.invoke('audio:support'),
 startAudioCapture:()=>ipcRenderer.invoke('audio:start'),
 stopAudioCapture:()=>ipcRenderer.invoke('audio:stop'),
 onAudioChunk:(callback:(chunk:Uint8Array)=>void)=>{
  const listener=(_event:Electron.IpcRendererEvent,chunk:Uint8Array)=>callback(chunk);
  ipcRenderer.on('audio:chunk',listener);
  return ()=>ipcRenderer.removeListener('audio:chunk',listener);
 },
 onAudioError:(callback:(message:string)=>void)=>{
  const listener=(_event:Electron.IpcRendererEvent,message:string)=>callback(message);
  ipcRenderer.on('audio:error',listener);
  return ()=>ipcRenderer.removeListener('audio:error',listener);
 },
 getLogReport:()=>ipcRenderer.invoke('logs:report'),
 saveLogReport:(text:string,name?:string)=>ipcRenderer.invoke('logs:save',text,name),
 log:(level:'info'|'warn'|'error',scope:string,message:string)=>ipcRenderer.send('logs:event',level,scope,message),
 createRoom:()=>ipcRenderer.invoke('room:create'),
 closeRoom:()=>ipcRenderer.invoke('room:close'),
 listSources:()=>ipcRenderer.invoke('sources:list'),
 selectSource:(id:string)=>ipcRenderer.invoke('sources:select',id)
};
contextBridge.exposeInMainWorld('golive',api);
