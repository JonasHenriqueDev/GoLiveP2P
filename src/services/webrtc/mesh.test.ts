import { describe, expect, it, vi } from 'vitest';
import { Mesh, normalizeBitrate } from './mesh';
describe('bitrate limit', () => {
  it('accepts user values and clamps the safe range', () => {
    expect(normalizeBitrate(5.5)).toBe(5_500_000);
    expect(normalizeBitrate(0)).toBe(500_000);
    expect(normalizeBitrate(50)).toBe(20_000_000);
    expect(() => normalizeBitrate(NaN)).toThrow();
  });
});
describe('native sender streamless tracks', () => {
  it('keeps audio and video in one receiver stream in either arrival order', async () => {
    for (const order of [['audio', 'video'], ['video', 'audio']]) {
      const pcs: { ontrack: (event: unknown) => void }[] = [];
      class FakeStream {
        tracks: { id: string; kind: string }[] = [];
        getTracks() { return this.tracks; }
        addTrack(track: { id: string; kind: string }) { this.tracks.push(track); }
      }
      class FakePeer {
        ontrack = () => {};
        async setRemoteDescription() {}
        async createAnswer() { return { sdp: 'answer' }; }
        async setLocalDescription() {}
        constructor() { pcs.push(this); }
      }
      vi.stubGlobal('MediaStream', FakeStream);
      vi.stubGlobal('RTCPeerConnection', FakePeer);
      try {
        const mesh = new Mesh(() => {});
        const received: MediaStream[] = [];
        mesh.onRemote = stream => { if (stream) received.push(stream); };
        await mesh.handle({ type: 'offer', from: 'peer', sdp: 'offer' });
        for (const kind of order) pcs[0].ontrack({ streams: [], track: { id: kind, kind } });
        expect(received[0]).toBe(received[1]);
        expect(received[1].getTracks().map(track => track.kind).sort()).toEqual(['audio', 'video']);
      } finally { vi.unstubAllGlobals(); }
    }
  });
});
