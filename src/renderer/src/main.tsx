import React, { useEffect, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { SignalingClient } from '../../services/signaling/client';
import { Mesh, preset, type Quality } from '../../services/webrtc/mesh';
import { PRESETS } from '../../services/webrtc/presets';
import { StatsCollector, type StreamStats } from '../../services/stats/stats';
import { NativeAudioBridge } from './audio/native-audio';
import type { Peer, ServerMessage } from '../../shared/protocol';
import type { TailscaleStatus } from '../../services/tailscale/status';
import './style.css';

type Source = { id: string; name: string; thumbnail: string; kind: 'screen' | 'window' };
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
  const [bitrate, setBitrate] = useState(6);
  const [customBitrate, setCustomBitrate] = useState(false);
  const [audio, setAudio] = useState(false);
  const [audioActive, setAudioActive] = useState(false);
  const [audioCapability, setAudioCapability] = useState({ available: false, message: 'Verificando áudio…' });
  const [ping, setPing] = useState<Record<string, {ms:number|null;route:string}>>({});
  const [logRecipient, setLogRecipient] = useState('');
  const [incomingLog, setIncomingLog] = useState<{from:string;text:string}|null>(null);
  const [hosts, setHosts] = useState<Host[]>([]);
  const [searching, setSearching] = useState(false);
  const [error, setError] = useState('');
  const [signalState, setSignalState] = useState('disconnected');
  const [stats, setStats] = useState<StreamStats | null>(null);
  const video = useRef<HTMLVideoElement>(null);
  const client = useRef(new SignalingClient());
  const mesh = useRef(new Mesh(message => client.current.send(message)));
  const audioBridge = useRef<NativeAudioBridge | null>(null);
  if (!audioBridge.current) audioBridge.current = new NativeAudioBridge(message => {
    setAudioActive(false);
    setError(message);
  });
  const peersRef = useRef<Peer[]>([]);
  const selfRef = useRef<Peer | null>(null);
  const localRef = useRef(false);
  const qualityRef = useRef<Quality>('1080p60');
  const bitrateRef = useRef(6);
  const streamerRef = useRef<string | null>(null);
  useEffect(() => { peersRef.current = peers; }, [peers]);
  useEffect(() => { selfRef.current = self; }, [self]);
  useEffect(() => { localRef.current = local; }, [local]);
  useEffect(() => { qualityRef.current = quality; }, [quality]);
  useEffect(() => { bitrateRef.current = bitrate; }, [bitrate]);
  useEffect(() => { streamerRef.current = streamer; }, [streamer]);
  useEffect(() => { if (video.current) video.current.srcObject = remote; }, [remote]);

  useEffect(() => {
    const refresh = () => { void window.golive.tailscaleStatus().then(status => setTailscale(status)).catch(() => {}); };
    refresh();
    const timer = setInterval(refresh, 5000);
    return () => clearInterval(timer);
  }, []);

  useEffect(() => {
    void window.golive.audioSupport().then(setAudioCapability).catch(() => {});
    const original = { info: console.info, warn: console.warn, error: console.error };
    for (const level of ['info', 'warn', 'error'] as const) {
      console[level] = (...args: unknown[]) => {
        original[level](...args);
        window.golive.log(level, 'Renderer', args.map(arg => arg instanceof Error ? arg.stack || arg.message : String(arg)).join(' ').slice(0, 4000));
      };
    }
    const onError = (event: ErrorEvent) => window.golive.log('error', 'Renderer', event.message + ' ' + (event.error?.stack || ''));
    const onRejection = (event: PromiseRejectionEvent) => window.golive.log('error', 'Renderer', String(event.reason));
    window.addEventListener('error', onError);
    window.addEventListener('unhandledrejection', onRejection);
    return () => {
      Object.assign(console, original);
      window.removeEventListener('error', onError);
      window.removeEventListener('unhandledrejection', onRejection);
    };
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
        audioBridge.current?.stop();
        setAudioActive(false);
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
            await media.changeBitrate(bitrateRef.current, qualityRef.current);
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
        if (message.type === 'log-report') setIncomingLog({ from: message.from, text: message.text });
        if (message.type === 'error') {
          if (message.message === 'Outra pessoa já transmite' && localRef.current) stop();
          setError(message.message);
        }
      })().catch(cause => setError(String(cause)));
    };
    return () => { service.close(); media.stop(); audioBridge.current?.stop(); };
  }, []);

  useEffect(() => {
    if (!self) return;
    const refresh = () => {
      for (const peer of peers) if (peer.ip) {
        void window.golive.pingPeer(peer.ip).then(value => setPing(current => ({ ...current, [peer.id]: value }))).catch(() => {});
      }
    };
    refresh();
    const timer = setInterval(refresh, 10_000);
    return () => clearInterval(timer);
  }, [self, peers]);

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
  async function start(id: string) {
    try {
      await window.golive.selectSource(id);
      setSources([]);
      const config = preset(quality);
      const stream = await navigator.mediaDevices.getDisplayMedia({
        video: { width: { ideal: config.width }, height: { ideal: config.height }, frameRate: { ideal: config.frameRate } },
        audio: false
      });
      if (audio && id.startsWith('screen:')) {
        setAudioActive(false);
        setError('Vídeo iniciado sem áudio: a mistura de aplicativos permitidos ainda não foi validada.');
      } else if (audio) {
        try {
          stream.addTrack(await audioBridge.current!.start());
          setAudioActive(true);
        } catch (cause) {
          setAudioActive(false);
          setError('Vídeo iniciado sem áudio: ' + String(cause));
        }
      }
      const track = stream.getVideoTracks()[0];
      track.onended = () => stop();
      client.current.send({ type: 'start-stream' });
      setStreamer(selfRef.current!.id);
      localRef.current = true;
      setLocal(true);
      await mesh.current.start(stream, peersRef.current.map(peer => peer.id), quality, bitrateRef.current);
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
    audioBridge.current?.stop();
    setAudioActive(false);
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
    if (!customBitrate) {
      const nextBitrate = preset(next).bitrate / 1_000_000;
      setBitrate(nextBitrate);
      bitrateRef.current = nextBitrate;
      if (localRef.current) await mesh.current.changeBitrate(nextBitrate, next);
    }
    if (localRef.current) {
      try { await mesh.current.changeQuality(next); }
      catch (cause) { setError('Não foi possível alterar a qualidade: ' + String(cause)); }
    }
  }
  async function changeBitrate(value: number) {
    const next = Math.min(20, Math.max(0.5, Number.isFinite(value) ? value : 0.5));
    setBitrate(next);
    bitrateRef.current = next;
    setCustomBitrate(true);
    if (localRef.current) {
      try { await mesh.current.changeBitrate(next, qualityRef.current); }
      catch (cause) { setError('Não foi possível alterar o bitrate: ' + String(cause)); }
    }
  }
  async function saveMyLog() {
    try { await window.golive.saveLogReport(await window.golive.getLogReport()); }
    catch (cause) { setError(String(cause)); }
  }
  async function sendMyLog() {
    try {
      if (!logRecipient) return;
      client.current.send({ type: 'log-report', to: logRecipient, text: await window.golive.getLogReport() });
      setError('Log enviado. O destinatário escolherá se deseja salvar o TXT.');
    } catch (cause) { setError(String(cause)); }
  }
  async function saveIncomingLog() {
    if (!incomingLog) return;
    try {
      const sender = peersRef.current.find(peer => peer.id === incomingLog.from)?.name || 'participante';
      await window.golive.saveLogReport(incomingLog.text, 'golive-log-' + sender.replace(/[^\w-]/g, '_') + '.txt');
      setIncomingLog(null);
    } catch (cause) { setError(String(cause)); }
  }
  const all = self ? [self, ...peers] : [];
  const streamerName = all.find(peer => peer.id === streamer)?.name;
  const peerStats = stats?.peers[streamer || ''] || Object.values(stats?.peers || {})[0];
  const qualitySelector = <label>Qualidade
    <select value={quality} onChange={event => void changeQuality(event.target.value as Quality)}>
      {Object.entries(PRESETS).map(([id, config]) => <option key={id} value={id}>{config.label}</option>)}
    </select>
  </label>;
  const bitrateSelector = <label>Bitrate máximo por espectador (Mbps)
    <input type="number" min="0.5" max="20" step="0.5" value={bitrate}
      onChange={event => void changeBitrate(Number(event.target.value))}/>
    <small>O WebRTC pode reduzir a taxa conforme a rede.</small>
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
        <div className="participants">{all.map(peer => <div key={peer.id} className="participant"><span className="dot"/> {peer.name}{peer.id === self.id ? ' (Você)' : ''}{peer.id === streamer ? ' · transmitindo' : ''}
          {peer.id !== self.id && <span className="ping" title={ping[peer.id]?.route || 'Ping indisponível'}>{ping[peer.id]?.ms == null ? '— ms' : ping[peer.id].ms + ' ms'}</span>}</div>)}</div>
        {roomHost && <div className="host">IP para convidados<strong>{host}</strong></div>}
        <div className="diagnostics"><h3>Diagnóstico</h3>
          <div>Tailscale <strong>{tailscale.connected ? 'Connected' : 'Disconnected'}</strong></div>
          <div>IP local <strong>{tailscale.ip || '—'}</strong></div>
          <div>Signaling <strong>{signalState}</strong></div>
          <div>WebRTC <strong>{peerStats?.connection || '—'}</strong></div>
          <div>ICE <strong>{peerStats?.ice || '—'}</strong></div>
          <div>Peers <strong>{peers.length}</strong></div>
        </div>
        <div className="logTools"><h3>Report de logs</h3>
          <button className="secondary small" onClick={() => void saveMyLog()}>Baixar meu log TXT</button>
          <label>Enviar log para
            <select value={logRecipient} onChange={event => setLogRecipient(event.target.value)}>
              <option value="">Escolha um participante</option>
              {peers.map(peer => <option value={peer.id} key={peer.id}>{peer.name}</option>)}
            </select>
          </label>
          <button className="secondary small" disabled={!logRecipient} onClick={() => void sendMyLog()}>Enviar log</button>
        </div>
      </aside>
      <section className="stage card">{local ? <>
        <div className="eyebrow">● TRANSMITINDO</div><h1>Sua tela está ao vivo</h1>
        {qualitySelector}
        {bitrateSelector}
        <p>Áudio: {audioActive ? 'ativo, com filtro por processo' : 'desativado'}</p>
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
        {bitrateSelector}
        <label className="check"><input type="checkbox" checked={audio} disabled={!audioCapability.available} onChange={event => setAudio(event.target.checked)}/> Áudio do aplicativo (janela)</label>
        <p className="audioHint">{audioCapability.message}</p>
        <button onClick={() => void showSources()}>Compartilhar tela</button>
      </>}</section>
    </div>}
    {sources.length > 0 && <div className="modal"><div className="modalBody card">
      <div className="row"><h2>Escolha uma fonte</h2><button className="text" onClick={() => setSources([])}>Fechar</button></div>
      <p>Janela: áudio do aplicativo selecionado quando disponível. Monitor: vídeo sem áudio enquanto a mistura segura por aplicativo não é validada. Microfone e Discord não são capturados como fontes.</p>
      <div className="sourceGrid">{sources.map(source => <button className="source" key={source.id} onClick={() => void start(source.id)}><img src={source.thumbnail}/><small>{source.kind === 'screen' ? 'Monitor inteiro' : 'Aplicativo / janela'}</small><span>{source.name}</span></button>)}</div>
    </div></div>}
    {incomingLog && <div className="modal"><div className="modalBody card logDialog">
      <h2>Log recebido</h2>
      <p>{all.find(peer => peer.id === incomingLog.from)?.name || 'Participante'} enviou um relatório TXT ({Math.round(incomingLog.text.length / 1024)} KB). Deseja salvá-lo?</p>
      <div className="row"><button className="secondary" onClick={() => setIncomingLog(null)}>Ignorar</button><button onClick={() => void saveIncomingLog()}>Baixar TXT</button></div>
    </div></div>}
    {error && <div className="toast" onClick={() => setError('')}>{error} ✕</div>}
  </main>;
}
createRoot(document.getElementById('root')!).render(<App/>);
