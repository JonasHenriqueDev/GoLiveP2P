import type { ClientMessage, ServerMessage } from '../../shared/protocol';
import { preset, type Quality } from './presets';
export type { Quality } from './presets';
export { preset } from './presets';

export class Mesh {
  private pcs = new Map<string, RTCPeerConnection>();
  private pending = new Map<string, RTCIceCandidateInit[]>();
  private restartAttempts = new Map<string, number>();
  stream: MediaStream | null = null;
  onRemote: (stream: MediaStream | null) => void = () => {};
  onState: (peer: string, state: string) => void = () => {};
  constructor(private send: (message: ClientMessage) => void) {}
  private peer(id: string) {
    let pc = this.pcs.get(id);
    if (pc) return pc;
    pc = new RTCPeerConnection({ iceServers: [], iceTransportPolicy: 'all' });
    this.pcs.set(id, pc);
    const current = pc;
    pc.onicecandidate = event => {
      if (event.candidate) this.send({ type: 'ice-candidate', to: id, candidate: { ...event.candidate.toJSON(), candidate: event.candidate.candidate } });
    };
    pc.ontrack = event => this.onRemote(event.streams[0] || new MediaStream([event.track]));
    pc.onconnectionstatechange = () => {
      this.onState(id, current.connectionState);
      if (current.connectionState === 'connected') this.restartAttempts.delete(id);
      if (current.connectionState === 'failed' && !this.stream && this.restartAttempts.get(id) !== 1) {
        this.restartAttempts.set(id, 1);
        this.send({ type: 'request-restart', to: id });
      }
      if (current.connectionState === 'failed' && this.stream && this.restartAttempts.get(id) !== 1) {
        this.restartAttempts.set(id, 1);
        void this.restart(id).catch(error => console.warn('[WebRTC] ICE restart failed', error));
      }
    };
    return pc;
  }
  async start(stream: MediaStream, peerIds: string[], quality: Quality) {
    this.stream = stream;
    await Promise.all(peerIds.map(id => this.offer(id, quality)));
  }
  async offer(id: string, quality: Quality) {
    if (!this.stream) return;
    this.remove(id);
    const pc = this.peer(id);
    for (const track of this.stream.getTracks()) {
      const sender = pc.addTrack(track, this.stream);
      if (track.kind === 'video') await this.configureSender(sender, quality);
    }
    const caps = RTCRtpSender.getCapabilities('video');
    const h264 = caps?.codecs.filter(codec => codec.mimeType.toLowerCase() === 'video/h264') || [];
    if (h264.length) {
      const transceiver = pc.getTransceivers().find(item => item.sender.track?.kind === 'video');
      transceiver?.setCodecPreferences([...h264, ...caps!.codecs.filter(codec => !h264.includes(codec))]);
    }
    const offer = await pc.createOffer();
    await pc.setLocalDescription(offer);
    this.send({ type: 'offer', to: id, sdp: offer.sdp! });
    console.info('[WebRTC] creating offer', id);
  }
  private async restart(id: string) {
    const pc = this.pcs.get(id);
    if (!pc || !this.stream || pc.signalingState !== 'stable') return;
    const offer = await pc.createOffer({ iceRestart: true });
    await pc.setLocalDescription(offer);
    this.send({ type: 'offer', to: id, sdp: offer.sdp! });
  }
  private async configureSender(sender: RTCRtpSender, quality: Quality) {
    const params = sender.getParameters();
    params.encodings = params.encodings?.length ? params.encodings : [{}];
    params.encodings[0].maxBitrate = preset(quality).bitrate;
    params.encodings[0].maxFramerate = preset(quality).frameRate;
    await sender.setParameters(params).catch(() => {});
  }
  async changeQuality(quality: Quality) {
    const track = this.stream?.getVideoTracks()[0];
    if (!track) return;
    const config = preset(quality);
    await track.applyConstraints({ width: { ideal: config.width }, height: { ideal: config.height }, frameRate: { ideal: config.frameRate } });
    await Promise.all([...this.pcs.values()].flatMap(pc => pc.getSenders().filter(sender => sender.track?.kind === 'video').map(sender => this.configureSender(sender, quality))));
  }
  async handle(message: ServerMessage) {
    if (message.type === 'request-restart' && this.stream) await this.restart(message.from);
    if (message.type === 'offer') {
      let pc = this.pcs.get(message.from);
      if (!pc || pc.signalingState !== 'stable') {
        const queued = this.pending.get(message.from);
        this.remove(message.from);
        if (queued) this.pending.set(message.from, queued);
        pc = this.peer(message.from);
      }
      await pc.setRemoteDescription({ type: 'offer', sdp: message.sdp });
      await this.flush(message.from);
      const answer = await pc.createAnswer();
      await pc.setLocalDescription(answer);
      this.send({ type: 'answer', to: message.from, sdp: answer.sdp! });
    }
    if (message.type === 'answer') {
      const pc = this.pcs.get(message.from);
      if (pc && pc.signalingState === 'have-local-offer') {
        await pc.setRemoteDescription({ type: 'answer', sdp: message.sdp });
        await this.flush(message.from);
      }
    }
    if (message.type === 'ice-candidate') {
      const pc = this.pcs.get(message.from);
      if (pc?.remoteDescription) await pc.addIceCandidate(message.candidate).catch(() => {});
      else this.pending.set(message.from, [...(this.pending.get(message.from) || []), message.candidate]);
    }
  }
  private async flush(id: string) {
    const pc = this.pcs.get(id);
    for (const candidate of this.pending.get(id) || []) await pc?.addIceCandidate(candidate).catch(() => {});
    this.pending.delete(id);
  }
  get connections() { return new Map(this.pcs); }
  remove(id: string) {
    this.pcs.get(id)?.close();
    this.pcs.delete(id);
    this.pending.delete(id);
    this.restartAttempts.delete(id);
  }
  stop() {
    this.stream?.getTracks().forEach(track => track.stop());
    this.stream = null;
    for (const id of this.pcs.keys()) this.remove(id);
    this.onRemote(null);
  }
}
