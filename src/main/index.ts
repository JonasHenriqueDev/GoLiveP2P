import { app, BrowserWindow, desktopCapturer, ipcMain, session } from 'electron';
import { join } from 'node:path';
import { RoomServer } from '../services/signaling/room';
import { getTailscaleStatus } from '../services/tailscale/status';
import { discoverHosts } from '../services/tailscale/discovery';
import { PORT } from '../shared/protocol';
import { pingPeer } from '../services/tailscale/ping';
import { audioSupport, startAudioCapture, stopAudioCapture } from './audio';
import { record, reportText, saveReport, setupLogs } from './logs';
app.commandLine.appendSwitch('disable-features','WebRtcHideLocalIpsWithMdns');
let room:RoomServer|null=null;
let selectedSource:string|null=null;
function createWindow(){
 const win=new BrowserWindow({width:1120,height:780,minWidth:800,minHeight:600,webPreferences:{preload:join(__dirname,'../preload/index.mjs'),contextIsolation:true,nodeIntegration:false,sandbox:false}});
 if(process.env.ELECTRON_RENDERER_URL)win.loadURL(process.env.ELECTRON_RENDERER_URL);else win.loadFile(join(__dirname,'../renderer/index.html'));
}
app.whenReady().then(()=>{
 setupLogs();
 session.defaultSession.setDisplayMediaRequestHandler(async (_request,callback)=>{
  const sources=await desktopCapturer.getSources({types:['screen','window']});
  const source=sources.find(s=>s.id===selectedSource);
  if (!source) { console.warn('[Capture] selected source unavailable'); return; }
  // Chromium's system loopback can include Discord. Audio is captured by the filtered native helper.
  callback({ video: source });
 });
 ipcMain.handle('tailscale:status',getTailscaleStatus);
 ipcMain.handle('tailscale:discover',discoverHosts);
 ipcMain.handle('tailscale:ping',(_event,ip:string)=>pingPeer(ip));
 ipcMain.handle('audio:support',audioSupport);
 ipcMain.handle('audio:start',(event)=>{
  const window=BrowserWindow.fromWebContents(event.sender);
  if(!window||!selectedSource)throw new Error('Fonte de tela indisponível');
  return startAudioCapture(selectedSource,window);
 });
 ipcMain.handle('audio:stop',stopAudioCapture);
 ipcMain.handle('logs:report',reportText);
 ipcMain.handle('logs:save',(_event,text:string,name?:string)=>saveReport(text,name));
 ipcMain.on('logs:event',(_event,level:unknown,scope:unknown,message:unknown)=>{
  if((level==='info'||level==='warn'||level==='error')&&typeof scope==='string'&&typeof message==='string')
   record(level,scope.slice(0,40),message.slice(0,4000));
 });
 ipcMain.handle('room:create',async()=>{
  const status=await getTailscaleStatus();if(!status.connected||!status.ip)throw new Error(status.message);
  room?.close();const next=new RoomServer(status.ip,PORT);await next.ready;room=next;console.info('[Signaling] listening',status.ip,PORT);return {ip:status.ip,port:PORT};
 });
 ipcMain.handle('room:close',()=>{room?.close();room=null;});
 ipcMain.handle('sources:list',async()=>{const sources=await desktopCapturer.getSources({types:['screen','window'],thumbnailSize:{width:300,height:180}});return sources.map(s=>({id:s.id,name:s.name,kind:s.id.startsWith('screen:')?'screen':'window',thumbnail:s.thumbnail.toDataURL()}));});
 ipcMain.handle('sources:select',async(_event,id:unknown)=>{
  if(typeof id!=='string'||id.length>160)throw new Error('Fonte de tela inválida');
  const sources=await desktopCapturer.getSources({types:['screen','window']});
  if(!sources.some(source=>source.id===id))throw new Error('Fonte de tela indisponível');
  selectedSource=id;
 });
 createWindow();
 app.on('activate',()=>{if(BrowserWindow.getAllWindows().length===0)createWindow();});
});
app.on('before-quit',()=>{stopAudioCapture();room?.close();});
app.on('window-all-closed',()=>{if(process.platform!=='darwin')app.quit();});
