import { app, BrowserWindow, desktopCapturer, ipcMain, session } from 'electron';
import { join } from 'node:path';
import { RoomServer } from '../services/signaling/room';
import { getTailscaleStatus } from '../services/tailscale/status';
import { PORT } from '../shared/protocol';
app.commandLine.appendSwitch('disable-features','WebRtcHideLocalIpsWithMdns');
let room:RoomServer|null=null;
let selectedSource:string|null=null;
function createWindow(){
 const win=new BrowserWindow({width:1120,height:780,minWidth:800,minHeight:600,webPreferences:{preload:join(__dirname,'../preload/index.mjs'),contextIsolation:true,nodeIntegration:false,sandbox:false}});
 if(process.env.ELECTRON_RENDERER_URL)win.loadURL(process.env.ELECTRON_RENDERER_URL);else win.loadFile(join(__dirname,'../renderer/index.html'));
}
app.whenReady().then(()=>{
 session.defaultSession.setDisplayMediaRequestHandler(async (request,callback)=>{
  const sources=await desktopCapturer.getSources({types:['screen','window']});
  const source=sources.find(s=>s.id===selectedSource);
  if (!source) { console.warn('[Capture] selected source unavailable'); return; }
  callback({ video: source, audio: process.platform === 'win32' && request.audioRequested ? 'loopback' : undefined });
 });
 ipcMain.handle('tailscale:status',getTailscaleStatus);
 ipcMain.handle('room:create',async()=>{
  const status=await getTailscaleStatus();if(!status.connected||!status.ip)throw new Error(status.message);
  room?.close();room=new RoomServer(status.ip,PORT);console.info('[Signaling] listening',status.ip,PORT);return {ip:status.ip,port:PORT};
 });
 ipcMain.handle('room:close',()=>{room?.close();room=null;});
 ipcMain.handle('sources:list',async()=>{const sources=await desktopCapturer.getSources({types:['screen','window'],thumbnailSize:{width:300,height:180}});return sources.map(s=>({id:s.id,name:s.name,thumbnail:s.thumbnail.toDataURL()}));});
 ipcMain.handle('sources:select',(_event,id:string)=>{selectedSource=id;});
 createWindow();
 app.on('activate',()=>{if(BrowserWindow.getAllWindows().length===0)createWindow();});
});
app.on('before-quit',()=>room?.close());
app.on('window-all-closed',()=>{if(process.platform!=='darwin')app.quit();});
