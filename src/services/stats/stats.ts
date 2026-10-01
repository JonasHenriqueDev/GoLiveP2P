export type PeerStats = {
  resolution: string; fps: number; bitrate: number; rtt: number | null;
  lost: number; codec: string; ice: string; connection: string
};
export type StreamStats = { peers: Record<string, PeerStats>; totalBitrate: number };
const empty = (): PeerStats => ({
  resolution: '—', fps: 0, bitrate: 0, rtt: null, lost: 0,
  codec: '—', ice: '—', connection: '—'
});
export class StatsCollector {
  private previous = new Map<string, { bytes: number; time: number }>();
  async collect(pcs: Map<string, RTCPeerConnection>): Promise<StreamStats> {
    const peers: Record<string, PeerStats> = {};
    for (const [id, pc] of pcs) {
      const result = empty();
      result.ice = pc.iceConnectionState;
      result.connection = pc.connectionState;
      const reports = await pc.getStats();
      const video: any[] = [];
      reports.forEach(report => {
        const item = report as any;
        if (item.type === 'candidate-pair' && item.state === 'succeeded' && item.currentRoundTripTime != null)
          result.rtt = Math.round(item.currentRoundTripTime * 1000);
        if ((item.type === 'outbound-rtp' || item.type === 'inbound-rtp') && item.kind === 'video') video.push(item);
      });
      for (const item of video) {
        if (item.frameWidth && item.frameHeight) result.resolution = item.frameWidth + '×' + item.frameHeight;
        result.fps = Math.max(result.fps, Math.round(item.framesPerSecond || 0));
        result.lost += item.packetsLost || 0;
        const codec = item.codecId && reports.get(item.codecId) as any;
        if (codec?.mimeType) result.codec = codec.mimeType;
        const bytes = item.bytesSent ?? item.bytesReceived;
        const key = id + ':' + item.id;
        const old = this.previous.get(key);
        if (old && bytes != null && item.timestamp > old.time)
          result.bitrate += Math.max(0, (bytes - old.bytes) * 8 / (item.timestamp - old.time) / 1000);
        if (bytes != null) this.previous.set(key, { bytes, time: item.timestamp });
      }
      result.bitrate = Math.round(result.bitrate * 10) / 10;
      peers[id] = result;
    }
    return { peers, totalBitrate: Math.round(Object.values(peers).reduce((sum, peer) => sum + peer.bitrate, 0) * 10) / 10 };
  }
}
