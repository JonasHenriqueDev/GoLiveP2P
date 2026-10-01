import { describe, expect, it } from 'vitest';
import { parsePeerAddresses } from './discovery';
describe('peer discovery', () => {
  it('keeps online Tailscale IPv4 peers only', () => {
    const peers = parsePeerAddresses(JSON.stringify({ Peer: {
      one: { HostName: 'host', Online: true, TailscaleIPs: ['100.80.1.2', 'fd7a::1'] },
      two: { HostName: 'offline', Online: false, TailscaleIPs: ['100.80.1.3'] }
    } }));
    expect(peers).toEqual([{ ip: '100.80.1.2', name: 'host' }]);
  });
});
