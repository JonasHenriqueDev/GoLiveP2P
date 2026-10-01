import { contextBridge, ipcRenderer } from 'electron';
const api={
 tailscaleStatus:()=>ipcRenderer.invoke('tailscale:status'),
 createRoom:()=>ipcRenderer.invoke('room:create'),
 closeRoom:()=>ipcRenderer.invoke('room:close'),
 listSources:()=>ipcRenderer.invoke('sources:list'),
 selectSource:(id:string)=>ipcRenderer.invoke('sources:select',id)
};
contextBridge.exposeInMainWorld('golive',api);
