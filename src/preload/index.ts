import { contextBridge, ipcRenderer } from 'electron';
const api={
 platform:process.platform,
 tailscaleStatus:()=>ipcRenderer.invoke('tailscale:status'),
 discoverHosts:()=>ipcRenderer.invoke('tailscale:discover'),
 createRoom:()=>ipcRenderer.invoke('room:create'),
 closeRoom:()=>ipcRenderer.invoke('room:close'),
 listSources:()=>ipcRenderer.invoke('sources:list'),
 selectSource:(id:string)=>ipcRenderer.invoke('sources:select',id)
};
contextBridge.exposeInMainWorld('golive',api);
