import workletUrl from './pcm-worklet.js?url';

export class NativeAudioBridge {
  private context: AudioContext | null = null;
  private node: AudioWorkletNode | null = null;
  private track: MediaStreamTrack | null = null;
  private unsubscribeChunk: (() => void) | null = null;
  private unsubscribeError: (() => void) | null = null;
  private pendingBytes = new Uint8Array(0);
  constructor(private onError: (message: string) => void) {}
  async start(): Promise<MediaStreamTrack> {
    this.stop();
    const context = new AudioContext({ sampleRate: 48000 });
    this.context = context;
    try {
      await context.audioWorklet.addModule(workletUrl);
      const node = new AudioWorkletNode(context, 'pcm-capture', { outputChannelCount: [2] });
      const destination = context.createMediaStreamDestination();
      node.connect(destination);
      this.node = node;
      this.track = destination.stream.getAudioTracks()[0];
      this.unsubscribeChunk = window.golive.onAudioChunk(chunk => {
        const bytes = new Uint8Array(this.pendingBytes.length + chunk.length);
        bytes.set(this.pendingBytes);
        bytes.set(chunk, this.pendingBytes.length);
        const aligned = bytes.length - bytes.length % 4;
        this.pendingBytes = bytes.slice(aligned);
        if (aligned) {
          const copy = bytes.slice(0, aligned).buffer;
          node.port.postMessage(copy, [copy]);
        }
      });
      this.unsubscribeError = window.golive.onAudioError(message => { this.onError(message); this.stop(); });
      await window.golive.startAudioCapture();
      await context.resume();
      return this.track;
    } catch (error) { this.stop(); throw error; }
  }
  stop() {
    const hadCapture = !!this.context || !!this.unsubscribeChunk;
    this.unsubscribeChunk?.(); this.unsubscribeChunk = null;
    this.pendingBytes = new Uint8Array(0);
    this.unsubscribeError?.(); this.unsubscribeError = null;
    this.track?.stop(); this.track = null;
    this.node?.disconnect(); this.node = null;
    if (this.context) void this.context.close();
    this.context = null;
    if (hadCapture) void window.golive.stopAudioCapture();
  }
}
