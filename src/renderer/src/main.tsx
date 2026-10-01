import React,{useEffect,useRef,useState} from 'react';
import {createRoot} from 'react-dom/client';
import {SignalingClient} from '../../services/signaling/client';
import {Mesh,preset,type Quality} from '../../services/webrtc/mesh';
import {StatsCollector,type StreamStats} from '../../services/stats/stats';
import type {Peer,ServerMessage} from '../../shared/protocol';
import type {TailscaleStatus} from '../../services/tailscale/status';
import './style.css';
function App(){
 const [tailscale,setTailscale]=useState<TailscaleStatus>({installed:false,connected:false,ip:null,message:'Verificando…'});
 const [name,setName]=useState('');const [host,setHost]=useState('');const [roomHost,setRoomHost]=useState(false);
 const [self,setSelf]=useState<Peer|null>(null);const [peers,setPeers]=useState<Peer[]>([]);const [streamer,setStreamer]=useState<string|null>(null);
 const [remote,setRemote]=useState<MediaStream|null>(null);const [local,setLocal]=useState(false);
 const [sources,setSources]=useState<{id:string;name:string;thumbnail:string}[]>([]);
 const [quality,setQuality]=useState<Quality>('1080p60');const [audio,setAudio]=useState(false);
 const [error,setError]=useState('');const [signalState,setSignalState]=useState('disconnected');
 const [stats,setStats]=useState<StreamStats|null>(null);
 const video=useRef<HTMLVideoElement>(null);const client=useRef(new SignalingClient());const mesh=useRef(new Mesh(message=>client.current.send(message)));
 const peersRef=useRef<Peer[]>([]);const selfRef=useRef<Peer|null>(null);const localRef=useRef(false);const qualityRef=useRef<Quality>('1080p60');
 useEffect(()=>{peersRef.current=peers},[peers]);useEffect(()=>{selfRef.current=self},[self]);useEffect(()=>{localRef.current=local},[local]);useEffect(()=>{qualityRef.current=quality},[quality]);
 useEffect(()=>{if(video.current)video.current.srcObject=remote},[remote]);
 useEffect(()=>{
  const refresh=()=>window.golive.tailscaleStatus().then(status=>{setTailscale(status);if(!status.connected&&selfRef.current){setError('Tailscale desconectado');client.current.close();mesh.current.stop();setSelf(null);setPeers([]);setLocal(false);setRemote(null);}});
  refresh();const timer=setInterval(refresh,5000);return()=>clearInterval(timer);
 },[]);
 useEffect(()=>{
  const service=client.current;const media=mesh.current;
  media.onRemote=setRemote;media.onState=(_id,state)=>{if(state==='failed')setError('Conexão WebRTC falhou; tentando recuperar ICE');};
  service.onState=state=>{setSignalState(state);if(state==='disconnected'&&selfRef.current){setError('Sinalização desconectada. Entre novamente na sala.');media.stop();setSelf(null);setPeers([]);setLocal(false);setRemote(null);}};
  service.onMessage=(message:ServerMessage)=>{void (async()=>{
   if(message.type==='joined-room'){setSelf(message.self);setPeers(message.peers);setStreamer(message.streamerId);console.info('[Signaling] client connected');}
   if(message.type==='user-joined'){setPeers(current=>[...current,message.peer]);if(localRef.current)await media.offer(message.peer.id,qualityRef.current);}
   if(message.type==='user-left'){setPeers(current=>current.filter(p=>p.id!==message.peer.id));media.remove(message.peer.id);}
   if(message.type==='start-stream')setStreamer(message.from);
   if(message.type==='stop-stream'){setStreamer(null);media.stop();setRemote(null);}
   if(message.type==='offer'||message.type==='answer'||message.type==='ice-candidate')await media.handle(message);
   if(message.type==='room-full')setError('Sala cheia (máximo de 5 participantes)');
   if(message.type==='error')setError(message.message);
  })().catch(e=>setError(String(e)));};
  return()=>{service.close();media.stop();};
 },[]);
 useEffect(()=>{if(!self)return;const collector=new StatsCollector();const timer=setInterval(()=>{void collector.collect(mesh.current.connections).then(setStats).catch(()=>{});},2500);return()=>clearInterval(timer);},[self]);
 async function enter(create:boolean){
  if(!tailscale.connected||!name.trim())return;
  setError('');try{
   let address=host.trim();
   if(create){const created=await window.golive.createRoom();address=created.ip;setRoomHost(true);setHost(address);}
   if(!/^100\.(?:\d{1,3}\.){2}\d{1,3}$/.test(address))throw new Error('Informe um IPv4 Tailscale válido (100.x.x.x)');
   await client.current.connect(address,name.trim());
  }catch(e){setError(String(e));if(create)await window.golive.closeRoom();}
 }
 async function showSources(){try{setSources(await window.golive.listSources());}catch(e){setError(String(e));}}
 async function start(id:string){
  try{
   await window.golive.selectSource(id);setSources([]);
   const p=preset(quality);
   const stream=await navigator.mediaDevices.getDisplayMedia({video:{width:{ideal:p.width},height:{ideal:p.height},frameRate:{ideal:p.frameRate}},audio});
   if(audio&&stream.getAudioTracks().length===0)setError('Áudio do sistema indisponível nesta plataforma ou fonte.');
   const track=stream.getVideoTracks()[0];track.onended=()=>stop();
   client.current.send({type:'start-stream'});setStreamer(self!.id);setLocal(true);localRef.current=true;
   await mesh.current.start(stream,peersRef.current.map(peer=>peer.id),quality);
   console.info('[Stream] started',track.getSettings());
  }catch(e){setError(String(e));}
 }
 function stop(){mesh.current.stop();client.current.send({type:'stop-stream'});setStreamer(null);setLocal(false);localRef.current=false;console.info('[Stream] stopped');}
 function leave(){stop();client.current.close();if(roomHost)void window.golive.closeRoom();setRoomHost(false);setSelf(null);setPeers([]);setRemote(null);}
 const all=self?[self,...peers]:[];const streamerName=all.find(p=>p.id===streamer)?.name;
 return <main><header><div className="logo">◉ <span>GoLive</span> P2P</div><div className={'status '+(tailscale.connected?'ok':'bad')}>● {tailscale.connected?'Tailscale conectado':'Tailscale desconectado'} <small>{tailscale.ip||''}</small></div></header>
 {!self?<section className="welcome"><h1>Compartilhe sua tela.<br/><em>Direto para seu grupo.</em></h1><p>Vídeo P2P pela sua tailnet. Até cinco pessoas, sem conta nem servidor de vídeo.</p><div className="card"><label>Seu nome<input value={name} maxLength={32} onChange={e=>setName(e.target.value)} placeholder="Ex.: Jonas"/></label><button disabled={!tailscale.connected||!name.trim()} onClick={()=>void enter(true)}>Criar sala</button><div className="divider">ou entre em uma sala</div><label>IP Tailscale do host<input value={host} onChange={e=>setHost(e.target.value)} placeholder="100.x.x.x"/></label><button className="secondary" disabled={!tailscale.connected||!name.trim()||!host.trim()} onClick={()=>void enter(false)}>Entrar na sala</button></div><p className="hint">{tailscale.message}</p></section>:
 <div className="layout"><aside className="card"><div className="row"><h2>Sala</h2><button className="text" onClick={leave}>Sair</button></div><p>Participantes: {all.length} / 5</p><div className="participants">{all.map(p=><div key={p.id} className="participant"><span className="dot"/> {p.name}{p.id===self.id?' (Você)':''}{p.id===streamer?' · transmitindo':''}</div>)}</div>{roomHost&&<div className="host">IP para convidados<strong>{host}</strong></div>}<div className="diagnostics"><h3>Diagnóstico</h3><div>Tailscale <strong>{tailscale.connected?'Connected':'Disconnected'}</strong></div><div>IP local <strong>{tailscale.ip||'—'}</strong></div><div>Signaling <strong>{signalState}</strong></div><div>WebRTC <strong>{stats?.connection||'—'}</strong></div><div>ICE <strong>{stats?.ice||'—'}</strong></div><div>Peers <strong>{peers.length}</strong></div></div></aside>
 <section className="stage card">{local?<><div className="eyebrow">● TRANSMITINDO</div><h1>Sua tela está ao vivo</h1><div className="stats"><span>{stats?.resolution||'—'}<small>Resolução</small></span><span>{stats?.fps||0} FPS<small>Quadros</small></span><span>{stats?.bitrate||0} Mbps<small>Bitrate</small></span><span>{stats?.rtt??'—'} ms<small>RTT</small></span><span>{stats?.lost||0}<small>Pacotes perdidos</small></span><span>{stats?.codec||'—'}<small>Codec</small></span></div><p>Espectadores: {peers.length}</p><button className="danger" onClick={stop}>Parar transmissão</button></>:
 streamer?<><div className="eyebrow">{streamerName} está compartilhando</div><video ref={video} autoPlay playsInline controls className="video"/><div className="row"><span>{stats?.resolution||'Aguardando vídeo'} · {stats?.fps||0} FPS</span><button className="secondary small" onClick={()=>video.current?.requestFullscreen()}>Tela cheia</button></div></>:
 <><div className="empty">▣</div><h1>Pronto para compartilhar?</h1><p>Selecione a qualidade e escolha uma tela ou janela.</p><label>Qualidade<select value={quality} onChange={e=>setQuality(e.target.value as Quality)}><option value="1080p60">1080p · 60 FPS</option><option value="720p30">720p · 30 FPS</option></select></label><label className="check"><input type="checkbox" checked={audio} onChange={e=>setAudio(e.target.checked)}/> Compartilhar áudio (se disponível)</label><button onClick={()=>void showSources()}>Compartilhar tela</button></>}</section></div>}
 {sources.length>0&&<div className="modal"><div className="modalBody card"><div className="row"><h2>Escolha uma fonte</h2><button className="text" onClick={()=>setSources([])}>Fechar</button></div><div className="sourceGrid">{sources.map(source=><button className="source" key={source.id} onClick={()=>void start(source.id)}><img src={source.thumbnail}/><span>{source.name}</span></button>)}</div></div></div>}
 {error&&<div className="toast" onClick={()=>setError('')}>{error} ✕</div>}
 </main>;
}
createRoot(document.getElementById('root')!).render(<App/>);
