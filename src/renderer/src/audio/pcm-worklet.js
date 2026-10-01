/* AudioWorklet: converts interleaved 48 kHz stereo s16le to a MediaStream track. */
class PcmCaptureProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.queue = [];
    this.offset = 0;
    this.pendingSamples = 0;
    this.port.onmessage = event => {
      const samples = new Int16Array(event.data);
      this.queue.push(samples);
      this.pendingSamples += samples.length;
      // Bound latency if the renderer receives audio faster than it consumes it.
      while (this.pendingSamples > 48000 && this.queue.length > 1) {
        this.pendingSamples -= this.queue[0].length - this.offset;
        this.queue.shift();
        this.offset = 0;
      }
    };
  }
  process(_inputs, outputs) {
    const [left, right] = outputs[0];
    for (let frame = 0; frame < left.length; frame++) {
      while (this.queue.length && this.offset >= this.queue[0].length) {
        this.queue.shift(); this.offset = 0;
      }
      if (!this.queue.length) break;
      const current = this.queue[0];
      left[frame] = current[this.offset++] / 32768;
      right[frame] = current[this.offset++] / 32768;
      this.pendingSamples -= 2;
    }
    return true;
  }
}
registerProcessor('pcm-capture', PcmCaptureProcessor);
