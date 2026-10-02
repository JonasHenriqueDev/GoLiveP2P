import type { ClientMessage, ServerMessage } from '../../shared/protocol';
import type { CaptureSettings, NativeEvent } from '../../shared/native-media';
import type { StreamStats } from '../stats/stats';
import { preset, type Quality } from './presets';
import { normalizeBitrate } from './mesh';

export class NativeMesh {
  stream = false;
  onRemote: (stream: MediaStream | null) => void = () => {};
  onState: (peer: string, state: string) => void = () => {};
  onFrame: (peer: string, jpeg: string) => void = () => {};
  onError: (message: string) => void = () => {};
  onAudio: (active: boolean) => void = () => {};
  onAudioLevel: (rms: number) => void = () => {};
  onEncoder: (encoder: string) => void = () => {};
  private settings: CaptureSettings | null = null;
  private peers = new Set<string>();
  private pending = new Map<
    string,
    Extract<ServerMessage, { type: 'ice-candidate' }>[]
  >();
  private states = new Map<string, string>();
  private unsubscribe: (() => void) | null = null;
  private restarting = new Set<string>();
  private previous = new Map<
    string,
    { bytes: number; time: number; frames: number }
  >();
  constructor(private send: (message: ClientMessage) => void) {}
  private listen() {
    if (this.unsubscribe) return;
    this.unsubscribe = window.golive.onMediaEvent((event) => this.event(event));
  }
  private event(event: NativeEvent) {
    if (event.event === 'frame') this.onFrame(event.peer, event.jpeg);
    if (event.event === 'audio-state') {
      this.onAudio(event.active);
      this.onAudioLevel(event.rms ?? 0);
      console.info('[Native audio]', {
        active: event.active,
        sources: event.sources,
        rms: event.rms,
        nonSilentFrames: event.nonSilentFrames,
      });
    }
    if (event.event === 'warning') this.onError(event.message);
    if (event.event === 'error') this.onError(event.message);
    if (event.event === 'signal') {
      if ((event.type === 'offer' || event.type === 'answer') && event.sdp)
        this.send({ type: event.type, to: event.peer, sdp: event.sdp });
      if (event.type === 'ice-candidate' && event.candidate)
        this.send({
          type: event.type,
          to: event.peer,
          candidate: event.candidate,
        });
    }
    if (event.event === 'state') {
      this.states.set(event.peer, event.state);
      this.onState(event.peer, event.state);
      if (event.state === 'connected') this.restarting.delete(event.peer);
      if (event.state === 'failed' && !this.restarting.has(event.peer)) {
        this.restarting.add(event.peer);
        if (this.stream)
          void this.offer(event.peer).catch((error) =>
            this.onError(String(error)),
          );
        else this.send({ type: 'request-restart', to: event.peer });
      }
    }
  }
  async startNative(
    settings: CaptureSettings,
    peerIds: string[],
    beforeOffer?: () => void,
  ) {
    this.listen();
    const result = await window.golive.mediaRequest('start', settings);
    this.onEncoder(result.encoder);
    this.settings = settings;
    this.stream = true;
    beforeOffer?.();
    console.info('[Native] capture started', result);
    await Promise.all(peerIds.map((id) => this.offer(id)));
    return result;
  }
  async offer(id: string, quality?: Quality) {
    void quality;
    if (!this.stream) return;
    this.listen();
    this.peers.add(id);
    await window.golive.mediaRequest('offer', { peer: id });
  }
  async handle(message: ServerMessage) {
    this.listen();
    if (message.type === 'request-restart' && this.stream)
      return this.offer(message.from);
    if (message.type === 'offer' || message.type === 'answer') {
      this.peers.add(message.from);
      await window.golive.mediaRequest('signal', {
        type: message.type,
        peer: message.from,
        sdp: message.sdp,
      });
      for (const ice of this.pending.get(message.from) || [])
        await window.golive.mediaRequest('signal', {
          type: 'ice-candidate',
          peer: message.from,
          candidate: ice.candidate,
        });
      this.pending.delete(message.from);
    }
    if (message.type === 'ice-candidate') {
      if (this.peers.has(message.from))
        await window.golive.mediaRequest('signal', {
          type: message.type,
          peer: message.from,
          candidate: message.candidate,
        });
      else {
        const queued = this.pending.get(message.from) || [];
        if (queued.length >= 128) throw new Error('Native ICE queue full');
        this.pending.set(message.from, [...queued, message]);
      }
    }
  }
  async changeQuality(quality: Quality) {
    if (!this.settings) return;
    const peers = [...this.peers];
    const { width, height, frameRate } = preset(quality);
    await this.startNative(
      { ...this.settings, width, height, fps: frameRate as 30 | 60 },
      peers,
    );
  }
  async changeBitrate(mbps: number, quality?: Quality) {
    void quality;
    const bitrate = normalizeBitrate(mbps);
    if (this.settings) this.settings.bitrate = bitrate;
    await window.golive.mediaRequest('bitrate', { bitrate });
  }
  async collect(): Promise<StreamStats> {
    const stats = await window.golive.mediaRequest('stats', {});
    const peers: StreamStats['peers'] = {};
    for (const [id, report] of Object.entries(stats.peers)) {
      // Keep the complete native report in logs; only measured fields enter UI stats.
      const result = {
        resolution: '—',
        fps: 0,
        bitrate: 0,
        rtt: null as number | null,
        lost: 0,
        codec: 'H264',
        ice: [
          'new',
          'checking',
          'connected',
          'completed',
          'failed',
          'disconnected',
          'closed',
        ][report.ice],
        connection: this.states.get(id) || 'new',
      };
      const now = performance.now();
      const entries = Object.values(report.raw).filter(
        (entry): entry is Record<string, unknown> =>
          typeof entry === 'object' && entry !== null,
      );
      const rtp = entries.filter(
        (entry) =>
          entry.type === (this.stream ? 'outbound-rtp' : 'inbound-rtp') &&
          entry.kind === 'video',
      );
      const bytes = rtp.reduce(
        (sum, entry) =>
          sum +
          Number(entry[this.stream ? 'bytes-sent' : 'bytes-received'] || 0),
        0,
      );
      const frames = this.stream ? stats.encodedFrames : report.receivedFrames;
      const old = this.previous.get(id);
      if (old) {
        result.bitrate = Math.max(
          0,
          ((bytes - old.bytes) * 8) / (now - old.time) / 1000,
        );
        result.fps = Math.max(
          0,
          Math.round(((frames - old.frames) * 1000) / (now - old.time)),
        );
      }
      this.previous.set(id, { bytes, time: now, frames });
      result.resolution = this.stream
        ? stats.width + '×' + stats.height
        : report.width
          ? report.width + '×' + report.height
          : '—';
      result.lost = rtp.reduce(
        (sum, entry) => sum + Number(entry['packets-lost'] || 0),
        0,
      );
      const pair = entries.find(
        (entry) =>
          entry.type === 'candidate-pair' &&
          typeof entry['current-round-trip-time'] === 'number',
      );
      if (pair)
        result.rtt = Math.round(Number(pair['current-round-trip-time']) * 1000);
      peers[id] = result;
    }
    return {
      peers,
      totalBitrate: Object.values(peers).reduce(
        (sum, peer) => sum + peer.bitrate,
        0,
      ),
    };
  }
  remove(id: string) {
    this.peers.delete(id);
    this.pending.delete(id);
    this.states.delete(id);
    this.previous.delete(id);
    void window.golive
      .mediaRequest('remove', { peer: id })
      .catch((error) => this.onError(String(error)));
  }
  stop() {
    this.stream = false;
    this.settings = null;
    this.peers.clear();
    this.pending.clear();
    this.states.clear();
    this.restarting.clear();
    this.previous.clear();
    void window.golive
      .mediaRequest('stop', {})
      .catch((error) => this.onError(String(error)));
    this.onRemote(null);
    this.onFrame('local', '');
  }
  dispose() {
    this.stop();
    this.unsubscribe?.();
    this.unsubscribe = null;
  }
}
