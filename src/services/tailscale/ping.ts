import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { tailscaleIpSchema } from '../../shared/protocol';
import { findTailscaleCommand } from './status';
const exec = promisify(execFile);
export type PingResult = { ms: number | null; route: 'direct' | 'DERP' | 'unknown' };
export function parsePing(output: string): PingResult {
  const duration = /\bin\s+([\d.]+)\s*(?:ms|milliseconds)\b/i.exec(output);
  return {
    ms: duration ? Math.round(Number(duration[1])) : null,
    route: /\bDERP\b/i.test(output) ? 'DERP' : /\bdirect\b|\bvia\s+\d+\.\d+\.\d+\.\d+/i.test(output) ? 'direct' : 'unknown'
  };
}
export async function pingPeer(ip: string): Promise<PingResult> {
  if (!tailscaleIpSchema.safeParse(ip).success) throw new Error('IP Tailscale inválido');
  const command = await findTailscaleCommand();
  if (!command) return { ms: null, route: 'unknown' };
  try {
    const result = await exec(command, ['ping', '--c=1', '--timeout=3s', ip], { timeout: 5000, windowsHide: true });
    return parsePing(result.stdout);
  } catch { return { ms: null, route: 'unknown' }; }
}
