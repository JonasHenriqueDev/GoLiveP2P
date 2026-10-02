import { describe, expect, it } from 'vitest';
import { captureSettings, nativeEvent, nativeRequests } from './native-media';
const config = {
  source: 'monitor:65537',
  method: 'dxgi',
  encoder: 'auto',
  width: 1920,
  height: 1080,
  fps: 60,
  bitrate: 6000000,
  audio: false,
};
describe('native media boundary', () => {
  it('validates measured audio levels without equating an open source with sound', () => {
    const capturingSilence = {
      v: 1,
      event: 'audio-state',
      active: true,
      sources: 1,
      rms: 0,
      nonSilentFrames: 0,
    };
    expect(nativeEvent.parse(capturingSilence)).toEqual(capturingSilence);
    for (const rms of [-1, 1.1, NaN, Infinity])
      expect(nativeEvent.safeParse({ ...capturingSilence, rms }).success).toBe(
        false,
      );
    expect(
      nativeEvent.safeParse({ ...capturingSilence, nonSilentFrames: -1 })
        .success,
    ).toBe(false);
  });
  it('accepts automatic native selection and limits PrintWindow to windows', () => {
    expect(
      captureSettings.safeParse({ ...config, method: 'auto' }).success,
    ).toBe(true);
    expect(
      captureSettings.safeParse({
        ...config,
        source: 'window:123',
        method: 'auto',
      }).success,
    ).toBe(true);
    expect(
      captureSettings.safeParse({
        ...config,
        source: 'window:123',
        method: 'printwindow',
      }).success,
    ).toBe(true);
    expect(
      captureSettings.safeParse({ ...config, method: 'printwindow' }).success,
    ).toBe(false);
  });
  it('accepts explicit monitor DXGI and covered-window WGC', () => {
    expect(captureSettings.safeParse(config).success).toBe(true);
    expect(
      captureSettings.safeParse({
        ...config,
        source: 'window:123',
        method: 'wgc',
      }).success,
    ).toBe(true);
    expect(
      captureSettings.safeParse({ ...config, source: 'window:123' }).success,
    ).toBe(false);
  });
  it('rejects arbitrary pipeline injection, unknown fields and invalid quality', () => {
    for (const changed of [
      { source: 'window:123 ! fakesink' },
      { pipeline: 'wasapisrc loopback=true' },
      { bitrate: NaN },
      { fps: 120 },
      { width: 9999 },
      { encoder: 'unknown' },
    ])
      expect(captureSettings.safeParse({ ...config, ...changed }).success).toBe(
        false,
      );
  });
  it('bounds media and signaling payloads and rejects protocol mismatches', () => {
    expect(
      nativeEvent.safeParse({ v: 2, event: 'ready', protocol: 1 }).success,
    ).toBe(false);
    expect(
      nativeEvent.safeParse({
        v: 1,
        event: 'frame',
        peer: 'local',
        jpeg: 'x'.repeat(2000001),
      }).success,
    ).toBe(false);
    expect(
      nativeRequests.signal.safeParse({
        type: 'offer',
        peer: 'not-a-uuid',
        sdp: 'x',
      }).success,
    ).toBe(false);
    expect(nativeRequests.pcm.safeParse({ data: 'not base64' }).success).toBe(
      false,
    );
  });
});
