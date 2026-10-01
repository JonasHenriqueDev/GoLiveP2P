import { describe, expect, it } from 'vitest';
import { StatsCollector } from './stats';
describe('per-peer stats', () => {
  it('calculates throughput without mixing spectators', async () => {
    const collector = new StatsCollector();
    let bytesA = 1_000_000;
    let bytesB = 2_000_000;
    let time = 1000;
    const make = (id: string, bytes: () => number) => ({
      iceConnectionState: 'connected',
      connectionState: 'connected',
      getStats: async () => new Map([
        ['codec', { id: 'codec', type: 'codec', mimeType: 'video/H264' }],
        ['video', { id: 'video', type: 'outbound-rtp', kind: 'video', codecId: 'codec', bytesSent: bytes(), timestamp: time, framesPerSecond: 60, frameWidth: 1920, frameHeight: 1080 }]
      ]),
      id
    }) as unknown as RTCPeerConnection;
    const peers = new Map([['A', make('A', () => bytesA)], ['B', make('B', () => bytesB)]]);
    await collector.collect(peers);
    time = 2000; bytesA += 750_000; bytesB += 500_000;
    const result = await collector.collect(peers);
    expect(result.peers.A.bitrate).toBe(6);
    expect(result.peers.B.bitrate).toBe(4);
    expect(result.totalBitrate).toBe(10);
    expect(result.peers.A.codec).toBe('video/H264');
  });
});
