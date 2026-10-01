import React, { useEffect, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { SignalingClient } from '../../services/signaling/client';
import { Mesh, preset, type Quality } from '../../services/webrtc/mesh';
import { PRESETS } from '../../services/webrtc/presets';
import { StatsCollector, type StreamStats } from '../../services/stats/stats';
import type { Peer, ServerMessage } from '../../shared/protocol';
import type { TailscaleStatus } from '../../services/tailscale/status';
import './style.css';

type Source = { id: string; name: string; thumbnail: string };
type Host = { ip: string; name: string; participants: number; full: boolean };
function App() {
  const [tailscale, setTailscale] = useState<TailscaleStatus>({ installed: false, connected: false, ip: null, message: 'Verificando…' });
  const [name, setName] = useState('');
  const [host, setHost] = useState('');
  const [roomHost, setRoomHost] = useState(false);
  const [self, setSelf] = useState<Peer | null>(null);
  const [peers, setPeers] = useState<Peer[]>([]);
  const [streamer, setStreamer] = useState<string | null>(null);
  const [remote, setRemote] = useState<MediaStream | null>(null);
  const [local, setLocal] = useState(false);
  const [sources, setSources] = useState<Source[]>([]);
  const [quality, setQuality] = useState<Quality>('1080p60');
  const [audio, setAudio] = useState(false);
  const [audioDevices, setAudioDevices] = useState<MediaDeviceInfo[]>([]);
  const [audioDevice, setAudioDevice] = useState('');
  const [hosts, setHosts] = useState<Host[]>([]);
  const [searching, setSearching] = useState(false);
  const [error, setError] = useState('');
  const [signalState, setSignalState] = useState('disconnected');
  const [stats, setStats] = useState<StreamStats | null>(null);
  const video = useRef<HTMLVideoElement>(null);
  const client = useRef(new SignalingClient());
  const mesh = useRef(new Mesh(message => client.current.send(message)));
  const peersRef = useRef<Peer[]>([]);
  const selfRef = useRef<Peer | null>(null);
  const localRef = useRef(false);
  const qualityRef = useRef<Quality>('1080p60');
  const streamerRef = useRef<string | null>(null);
  useEffect(() => { peersRef.current = peers; }, [peers]);
  useEffect(() => { selfRef.current = self; }, [self]);
  useEffect(() => { localRef.current = local; }, [local]);
  useEffect(() => { qualityRef.current = quality; }, [quality]);
  useEffect(() => { streamerRef.current = streamer; }, [streamer]);
  useEffect(() => { if (video.current) video.current.srcObject = remote; }, [remote]);

  useEffect(() => {
    const refresh = () => { void window.golive.tailscaleStatus().then(status => setTailscale(status)).catch(() => {}); };
    refresh();
    const timer = setInterval(refresh, 5000);
    return () => clearInterval(timer);
  }, []);

  useEffect(() => {
    const service = client.current;
    const media = mesh.current;
    media.onRemote = setRemote;
    media.onState = (_id, state) => {
      if (state === 'failed') setError('Conexão WebRTC falhou; tentando nova negociação ICE.');
    };
    service.onState = state => {
      setSignalState(state);
      if (state === 'reconnecting') setError('Sinalização caiu. Reconectando automaticamente…');
      if (state === 'disconnected' && selfRef.current) {
        setError('Não foi possível reconectar. Entre novamente na sala.');
        media.stop();
        selfRef.current = null;
        setSelf(null);
        setPeers([]);
        setStreamer(null);
        setLocal(false);
        setRemote(null);
      }
    };
    service.onMessage = (message: ServerMessage) => {
      void (async () => {
        if (message.type === 'joined-room') {
          selfRef.current = message.self;
          setSelf(message.self);
          setPeers(message.peers);
          setStreamer(message.streamerId);
          setError('');
          if (media.stream) {
            service.send({ type: 'start-stream' });
            await Promise.all(message.peers.map(peer => media.offer(peer.id, qualityRef.current)));
          }
          console.info('[Signaling] client connected', message.resumed ? 'resumed' : 'new');
        }
        if (message.type === 'user-joined') {
          setPeers(current => [...current.filter(peer => peer.id !== message.peer.id), message.peer]);
          if (localRef.current) await media.offer(message.peer.id, qualityRef.current);
        }
        if (message.type === 'user-left') {
          setPeers(current => current.filter(peer => peer.id !== message.peer.id));
          media.remove(message.peer.id);
          if (message.peer.id === streamerRef.current) { setStreamer(null); setRemote(null); }
        }
        if (message.type === 'start-stream') setStreamer(message.from);
        if (message.type === 'stop-stream') {
          setStreamer(null);
          if (!localRef.current) { media.stop(); setRemote(null); }
        }
        if (message.type === 'offer' || message.type === 'answer' || message.type === 'ice-candidate' || message.type === 'request-restart')
          await media.handle(message);
        if (message.type === 'room-full') setError('Sala cheia (máximo de 5 participantes)');
        if (message.type === 'error') {
          if (message.message === 'Outra pessoa já transmite' && localRef.current) stop();
          setError(message.message);
        }
      })().catch(cause => setError(String(cause)));
    };
    return () => { service.close(); media.stop(); };
  }, []);

  useEffect(() => {
    if (!self) return;
    const collector = new StatsCollector();
    const timer = setInterval(() => {
      void collector.collect(mesh.current.connections).then(setStats).catch(() => {});
    }, 2500);
    return () => clearInterval(timer);
  }, [self]);

  async function enter(create: boolean) {
    if (!tailscale.connected || !name.trim()) return;
    setError('');
    try {
      let address = host.trim();
      if (create) {
        const created = await window.golive.createRoom();
        address = created.ip;
        setRoomHost(true);
        setHost(address);
      }
      if (!/^100\.(?:\d{1,3}\.){2}\d{1,3}$/.test(address))
        throw new Error('Informe um IPv4 Tailscale válido (100.x.x.x)');
      await client.current.connect(address, name.trim());
    } catch (cause) {
      setError(String(cause));
      if (create) { await window.golive.closeRoom(); setRoomHost(false); }
    }
  }
  async function discover() {
    setSearching(true);
    try { setHosts(await window.golive.discoverHosts()); }
    catch (cause) { setError(String(cause)); }
    finally { setSearching(false); }
  }
  async function showSources() {
    try { setSources(await window.golive.listSources()); }
    catch (cause) { setError(String(cause)); }
  }
  async function loadAudioDevices() {
    try {
      const permission = await navigator.mediaDevices.getUserMedia({ audio: true });
      permission.getTracks().forEach(track => track.stop());
      setAudioDevices((await navigator.mediaDevices.enumerateDevices()).filter(device => device.kind === 'audioinput'));
    } catch (cause) { setError('Não foi possível listar entradas de áudio: ' + String(cause)); }
  }
  async function start(id: string) {
    try {
      await window.golive.selectSource(id);
      setSources([]);
      const config = preset(quality);
      const systemAudio = audio && window.golive.platform === 'win32';
      const stream = await navigator.mediaDevices.getDisplayMedia({
        video: { width: { ideal: config.width }, height: { ideal: config.height }, frameRate: { ideal: config.frameRate } },
        audio: systemAudio
      });
      if (audio && window.golive.platform !== 'win32') {
        try {
          const input = await navigator.mediaDevices.getUserMedia({ audio: audioDevice ? { deviceId: { exact: audioDevice } } : true });
          input.getAudioTracks().forEach(track => stream.addTrack(track));
        } catch (cause) { setError('Áudio de entrada indisponível: ' + String(cause)); }
      }
      if (audio && stream.getAudioTracks().length === 0) setError('Áudio indisponível nesta fonte ou plataforma.');
      const track = stream.getVideoTracks()[0];
      track.onended = () => stop();
      client.current.send({ type: 'start-stream' });
      setStreamer(selfRef.current!.id);
      localRef.current = true;
      setLocal(true);
      await mesh.current.start(stream, peersRef.current.map(peer => peer.id), quality);
      console.info('[Stream] started', track.getSettings());
    } catch (cause) {
      if (localRef.current) stop();
      setError(String(cause));
    }
  }
  function stop() {
    if (!localRef.current) return;
    localRef.current = false;
    setLocal(false);
    mesh.current.stop();
    client.current.send({ type: 'stop-stream' });
    setStreamer(null);
    console.info('[Stream] stopped');
  }
  function leave() {
    stop();
    client.current.close();
    if (roomHost) void window.golive.closeRoom();
    setRoomHost(false);
    selfRef.current = null;
    setSelf(null);
    setPeers([]);
    setRemote(null);
    setStreamer(null);
  }
  async function changeQuality(next: Quality) {
    setQuality(next);
    qualityRef.current = next;
    if (localRef.current) {
      try { await mesh.current.changeQuality(next); }
      catch (cause) { setError('Não foi possível alterar a qualidade: ' + String(cause)); }
    }
  }
  const all = self ? [self, ...peers] : [];
  const streamerName = all.find(peer => peer.id === streamer)?.name;
  const peerStats = stats?.peers[streamer || ''] || Object.values(stats?.peers || {})[0];
  const qualitySelector = <label>Qualidade
    <select value={quality} onChange={event => void changeQuality(event.target.value as Quality)}>
      {Object.entries(PRESETS).map(([id, config]) => <option key={id} value={id}>{config.label}</option>)}
    </select>
  </label>;

  return <main>
    <header><div className="logo">◉ <span>GoLive</span> P2P</div>
      <div className={'status ' + (tailscale.connected ? 'ok' : 'bad')}>● {tailscale.connected ? 'Tailscale conectado' : 'Tailscale desconectado'} <small>{tailscale.ip || ''}</small></div>
    </header>
    {!self ? <section className="welcome">
      <h1>Compartilhe sua tela.<br/><em>Direto para seu grupo.</em></h1>
      <p>Vídeo P2P pela sua tailnet. Até cinco pessoas, sem conta nem servidor de vídeo.</p>
      <div className="card">
        <label>Seu nome<input value={name} maxLength={32} onChange={event => setName(event.target.value)} placeholder="Ex.: Jonas"/></label>
        <button disabled={!tailscale.connected || !name.trim()} onClick={() => void enter(true)}>Criar sala</button>
        <div className="divider">ou entre em uma sala</div>
        <label>IP Tailscale do host<input value={host} onChange={event => setHost(event.target.value)} placeholder="100.x.x.x"/></label>
        <button className="secondary" disabled={!tailscale.connected || !name.trim() || !host.trim()} onClick={() => void enter(false)}>Entrar na sala</button>
        <button className="text" disabled={!tailscale.connected || searching} onClick={() => void discover()}>{searching ? 'Procurando…' : 'Procurar salas na tailnet'}</button>
        {hosts.map(found => <button className="secondary discovery" key={found.ip} disabled={found.full} onClick={() => setHost(found.ip)}>{found.name} · {found.ip} · {found.participants}/5 {found.full ? 'cheia' : ''}</button>)}
      </div>
      <p className="hint">{tailscale.message}</p>
    </section> :
    <div className="layout">
      <aside className="card"><div className="row"><h2>Sala</h2><button className="text" onClick={leave}>Sair</button></div>
        <p>Participantes: {all.length} / 5</p>
        <div className="participants">{all.map(peer => <div key={peer.id} className="participant"><span className="dot"/> {peer.name}{peer.id === self.id ? ' (Você)' : ''}{peer.id === streamer ? ' · transmitindo' : ''}</div>)}</div>
        {roomHost && <div className="host">IP para convidados<strong>{host}</strong></div>}
        <div className="diagnostics"><h3>Diagnóstico</h3>
          <div>Tailscale <strong>{tailscale.connected ? 'Connected' : 'Disconnected'}</strong></div>
          <div>IP local <strong>{tailscale.ip || '—'}</strong></div>
          <div>Signaling <strong>{signalState}</strong></div>
          <div>WebRTC <strong>{peerStats?.connection || '—'}</strong></div>
          <div>ICE <strong>{peerStats?.ice || '—'}</strong></div>
          <div>Peers <strong>{peers.length}</strong></div>
        </div>
      </aside>
      <section className="stage card">{local ? <>
        <div className="eyebrow">● TRANSMITINDO</div><h1>Sua tela está ao vivo</h1>
        {qualitySelector}
        <p>Espectadores: {peers.length} · Upload total: {stats?.totalBitrate || 0} Mbps</p>
        <div className="peerStats">{peers.map(peer => {
          const detail = stats?.peers[peer.id];
          return <div key={peer.id}><strong>{peer.name}</strong><span>{detail?.resolution || '—'} · {detail?.fps || 0} FPS · {detail?.bitrate || 0} Mbps · RTT {detail?.rtt ?? '—'} ms · perda {detail?.lost || 0} · {detail?.codec || '—'} · {detail?.connection || '—'}</span></div>;
        })}</div>
        <button className="danger" onClick={stop}>Parar transmissão</button>
      </> : streamer ? <>
        <div className="eyebrow">{streamerName} está compartilhando</div>
        <video ref={video} autoPlay playsInline controls className="video"/>
        <div className="row"><span>{peerStats?.resolution || 'Aguardando vídeo'} · {peerStats?.fps || 0} FPS · {peerStats?.bitrate || 0} Mbps · RTT {peerStats?.rtt ?? '—'} ms · perda {peerStats?.lost || 0}</span>
          <button className="secondary small" onClick={() => video.current?.requestFullscreen()}>Tela cheia</button></div>
      </> : <>
        <div className="empty">▣</div><h1>Pronto para compartilhar?</h1>
        <p>Selecione a qualidade e escolha uma tela ou janela.</p>
        {qualitySelector}
        <label className="check"><input type="checkbox" checked={audio} onChange={event => setAudio(event.target.checked)}/> Compartilhar áudio</label>
        {audio && window.golive.platform !== 'win32' && <>
          <label>Entrada de áudio ou monitor PulseAudio/PipeWire<select value={audioDevice} onChange={event => setAudioDevice(event.target.value)}>
            <option value="">Entrada padrão</option>{audioDevices.map(device => <option key={device.deviceId} value={device.deviceId}>{device.label || device.deviceId}</option>)}
          </select></label>
          <button className="secondary small" onClick={() => void loadAudioDevices()}>Listar entradas de áudio</button>
        </>}
        <button onClick={() => void showSources()}>Compartilhar tela</button>
      </>}</section>
    </div>}
    {sources.length > 0 && <div className="modal"><div className="modalBody card">
      <div className="row"><h2>Escolha uma fonte</h2><button className="text" onClick={() => setSources([])}>Fechar</button></div>
      <div className="sourceGrid">{sources.map(source => <button className="source" key={source.id} onClick={() => void start(source.id)}><img src={source.thumbnail}/><span>{source.name}</span></button>)}</div>
    </div></div>}
    {error && <div className="toast" onClick={() => setError('')}>{error} ✕</div>}
  </main>;
}
createRoot(document.getElementById('root')!).render(<App/>);
