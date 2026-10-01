import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { PORT } from '../../shared/protocol';
import { findTailscaleCommand } from './status';

const exec = promisify(execFile);
export type DiscoveredHost = { ip: string; name: string; participants: number; full: boolean };

export function parsePeerAddresses(raw: string): { ip: string; name: string }[] {
  const status = JSON.parse(raw) as { Peer?: Record<string, { HostName?: string; TailscaleIPs?: string[]; Online?: boolean }> };
  return Object.values(status.Peer ?? {})
    .filter(peer => peer.Online !== false)
    .flatMap(peer => (peer.TailscaleIPs ?? [])
      .filter(ip => /^100\.(?:\d{1,3}\.){2}\d{1,3}$/.test(ip))
      .map(ip => ({ ip, name: peer.HostName || ip })));
}

export async function discoverHosts(): Promise<DiscoveredHost[]> {
  const command = await findTailscaleCommand();
  if (!command) return [];
  let peers: { ip: string; name: string }[];
  try {
    const result = await exec(command, ['status', '--json'], { timeout: 5000, windowsHide: true });
    peers = parsePeerAddresses(result.stdout).slice(0, 100);
  } catch { return []; }
  const results: DiscoveredHost[] = [];
  // Probe a bounded number at once; an offline peer cannot delay the whole scan.
  for (let i = 0; i < peers.length; i += 10) {
    const batch = await Promise.all(peers.slice(i, i + 10).map(async peer => {
      try {
        const response = await fetch('http://' + peer.ip + ':' + PORT + '/discover', {
          signal: AbortSignal.timeout(1200), redirect: 'error'
        });
        if (!response.ok) return null;
        const reader = response.body?.getReader();
        if (!reader) return null;
        const chunks: Uint8Array[] = [];
        let length = 0;
        while (true) {
          const part = await reader.read();
          if (part.done) break;
          length += part.value.byteLength;
          if (length > 4096) { await reader.cancel(); return null; }
          chunks.push(part.value);
        }
        const body = new Uint8Array(length);
        let offset = 0;
        for (const chunk of chunks) { body.set(chunk, offset); offset += chunk.byteLength; }
        const data = JSON.parse(new TextDecoder().decode(body)) as { app?: string; participants?: number; full?: boolean };
        if (data.app !== 'golive-p2p' || !Number.isInteger(data.participants)) return null;
        return { ...peer, participants: data.participants!, full: !!data.full };
      } catch { return null; }
    }));
    results.push(...batch.filter((entry): entry is DiscoveredHost => !!entry));
  }
  return results;
}
