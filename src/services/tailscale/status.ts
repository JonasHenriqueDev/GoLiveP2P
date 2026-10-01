import { execFile } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join } from 'node:path';
import { promisify } from 'node:util';

const exec = promisify(execFile);
export type TailscaleStatus = { installed: boolean; connected: boolean; ip: string | null; message: string };

export function parseTailscaleStatus(raw: string, ipRaw: string): TailscaleStatus {
  let state: unknown;
  try { state = JSON.parse(raw); }
  catch { return { installed: true, connected: false, ip: null, message: 'Resposta inválida do Tailscale' }; }
  const data = state as { BackendState?: string; TailscaleIPs?: string[] };
  const ip = ipRaw.trim().split(/\s+/).find(v => /^100\.(?:\d{1,3}\.){2}\d{1,3}$/.test(v))
    ?? data.TailscaleIPs?.find(v => v.startsWith('100.')) ?? null;
  const connected = data.BackendState === 'Running' && !!ip;
  return { installed: true, connected, ip: connected ? ip : null, message: connected ? 'Conectado' : data.BackendState || 'Tailscale desconectado' };
}

export function tailscaleCommands(platform = process.platform, env = process.env): string[] {
  const commands = [platform === 'win32' ? 'tailscale.exe' : 'tailscale'];
  if (platform === 'win32') {
    for (const root of [env.ProgramFiles, env['ProgramFiles(x86)'], env.LOCALAPPDATA]) {
      if (root) commands.push(join(root, 'Tailscale', 'tailscale.exe'));
    }
  }
  return commands;
}

export async function findTailscaleCommand(): Promise<string | null> {
  let command: string | null = null;
  for (const candidate of tailscaleCommands()) {
    if (candidate.includes('/') || candidate.includes('\\')) {
      if (existsSync(candidate)) { command = candidate; break; }
    } else {
      try { await exec(candidate, ['version'], { timeout: 5000, windowsHide: true }); command = candidate; break; }
      catch (error) { if ((error as NodeJS.ErrnoException).code !== 'ENOENT') { command = candidate; break; } }
    }
  }
  return command;
}

export async function getTailscaleStatus(): Promise<TailscaleStatus> {
  const command = await findTailscaleCommand();
  if (!command) return { installed: false, connected: false, ip: null, message: 'Tailscale não instalado' };
  try {
    const [status, ip] = await Promise.all([
      exec(command, ['status', '--json'], { timeout: 5000, windowsHide: true }),
      exec(command, ['ip', '-4'], { timeout: 5000, windowsHide: true })
    ]);
    return parseTailscaleStatus(status.stdout, ip.stdout);
  } catch {
    return { installed: true, connected: false, ip: null, message: 'Tailscale desconectado ou indisponível' };
  }
}
