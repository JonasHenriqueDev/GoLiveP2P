import {spawn,execFileSync} from 'node:child_process';
import {mkdirSync,readFileSync,existsSync,writeFileSync} from 'node:fs';
import {resolve,join} from 'node:path';
const root=resolve('.'),work=resolve('work/qt-smoke',new Date().toISOString().replace(/[:.]/g,'-'));
mkdirSync(work,{recursive:true});
const wait=ms=>new Promise(r=>setTimeout(r,ms));
const children=[];
function child(exe,args,label){const p=spawn(exe,args,{cwd:root,windowsHide:false,stdio:['ignore','pipe','pipe']});children.push(p);p.stderr.on('data',data=>writeFileSync(join(work,label+'.stderr.log'),data,{flag:'a'}));p.on('error',e=>writeFileSync(join(work,label+'.error.txt'),String(e)));return p;}
try{
 const fixture=child(resolve('work/native-build/media-fixture.exe'),[],'fixture');
 let handle;
 for(let i=0;i<30;++i){await wait(200);handle=execFileSync('powershell.exe',['-NoProfile','-Command',`(Get-Process -Id ${fixture.pid}).MainWindowHandle.ToInt64()`],{windowsHide:true,encoding:'utf8'}).trim();if(handle!=='0')break;}
 if(!handle || handle==='0')throw Error('Fixture window unavailable');
 const ip=execFileSync('C:/Program Files/Tailscale/tailscale.exe',['ip','-4'],{windowsHide:true,encoding:'utf8'}).trim();
 const exe=resolve('release/qt-unpacked/GoLive P2P.exe'),host=join(work,'host.json'),viewer=join(work,'viewer.json');
 const h=child(exe,['--host','--port=47622','--source=window:'+handle,'--report='+host,'--duration=38'],'host');
 let ready=false;for(let i=0;i<80;++i){await wait(250);try{const v=await(await fetch(`http://${ip}:47622/discover`,{signal:AbortSignal.timeout(700)})).json();if(v.app==='golive-p2p'){ready=true;break;}}catch{}}
 if(!ready)throw Error('Qt host room did not open');
 const v=child(exe,['--connect='+ip,'--port=47622','--report='+viewer,'--duration=18'],'viewer');
 await new Promise((done,reject)=>{v.once('exit',code=>code===0?done():reject(Error('Qt viewer exit '+code)));setTimeout(()=>reject(Error('Viewer timeout')),30000).unref();});
 await new Promise((done,reject)=>{if(h.exitCode!==null){done();return;}h.once('exit',code=>code===0?done():reject(Error('Qt host exit '+code)));setTimeout(()=>reject(Error('Host timeout')),30000).unref();});
 if(!existsSync(host)||!existsSync(viewer))throw Error('Missing Qt reports');
 const a=JSON.parse(readFileSync(host)),b=JSON.parse(readFileSync(viewer));
 const media=b.samples.flatMap(s=>Object.values(s.peers||{}));
 const pass=a.previewFrames>20 && a.encoder==='nvenc-d3d11' && b.displayedFrames>20 && media.some(p=>p.audioFrames>48000 && p.audioRms>0.0001 && p.receivedFrames>20);
 const summary={passed:pass,ui:'Qt Widgets only',hostPreview:a.previewFrames,viewerFrames:b.displayedFrames,receiverAudioFrames:Math.max(0,...media.map(p=>p.audioFrames||0)),receiverAudioRms:Math.max(0,...media.map(p=>p.audioRms||0)),reports:work};
 writeFileSync(join(work,'result.json'),JSON.stringify(summary,null,2));console.log(JSON.stringify(summary,null,2));if(!pass)throw Error('Qt audio/video integration failed');
}finally{for(const p of children)if(p.exitCode===null)p.kill();}
