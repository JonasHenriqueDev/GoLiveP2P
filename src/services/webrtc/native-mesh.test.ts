import { afterEach, describe, expect, it, vi } from 'vitest';
import { NativeMesh } from './native-mesh';
import type { CaptureSettings, NativeEvent } from '../../shared/native-media';
const settings: CaptureSettings = {
  source: 'window:42',
  method: 'wgc',
  encoder: 'auto',
  width: 1920,
  height: 1080,
  fps: 60,
  bitrate: 6000000,
  audio: false,
  allowedAudioApps: [],
};
function bridge() {
  const calls: string[] = [];
  let event: ((value: NativeEvent) => void) | undefined;
  const request = vi.fn(async (method: string) => {
    calls.push(method);
    return {
      encoder: 'openh264',
      method: 'wgc',
      borderRemovalVerified: false,
      audio: false,
    };
  });
  vi.stubGlobal('window', {
    golive: {
      mediaRequest: request,
      onMediaEvent: (callback: typeof event) => {
        event = callback;
        return () => {};
      },
    },
  });
  return { calls, request, emit: (value: NativeEvent) => event?.(value) };
}
afterEach(() => vi.unstubAllGlobals());
describe('native signaling integration', () => {
  it('claims transmission before offers and displays the actual CPU fallback', async () => {
    const { calls } = bridge();
    const media = new NativeMesh(vi.fn());
    const encoder = vi.fn();
    media.onEncoder = encoder;
    await media.startNative(settings, ['peer-a', 'peer-b'], () =>
      calls.push('claim'),
    );
    expect(calls).toEqual(['start', 'claim', 'offer', 'offer']);
    expect(encoder).toHaveBeenCalledWith('openh264');
  });
  it('buffers early ICE until the peer SDP exists', async () => {
    const { request } = bridge();
    const media = new NativeMesh(vi.fn());
    const candidate = {
      candidate: 'candidate:1',
      sdpMid: '0',
      sdpMLineIndex: 0,
    };
    await media.handle({ type: 'ice-candidate', from: 'peer-a', candidate });
    expect(request).not.toHaveBeenCalled();
    await media.handle({ type: 'offer', from: 'peer-a', sdp: 'v=0' });
    expect(request.mock.calls).toEqual([
      ['signal', { type: 'offer', peer: 'peer-a', sdp: 'v=0' }],
      ['signal', { type: 'ice-candidate', peer: 'peer-a', candidate }],
    ]);
  });
  it('keeps video running when attribution blocks audio', async () => {
    const { emit } = bridge();
    const media = new NativeMesh(vi.fn());
    const audio = vi.fn();
    media.onAudio = audio;
    await media.startNative(settings, []);
    emit({ v: 1, event: 'audio-state', active: false, sources: 0 });
    expect(audio).toHaveBeenCalledWith(false);
    expect(media.stream).toBe(true);
  });
});
